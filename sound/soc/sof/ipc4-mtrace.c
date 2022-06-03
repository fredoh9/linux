// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2022 Intel Corporation. All rights reserved.

#include <linux/debugfs.h>
#include <linux/sched/signal.h>
#include <sound/sof/ipc4/header.h>
#include "sof-priv.h"
#include "ipc4-priv.h"

/*
 * debug info window is orginized in 16 slots.
 *
 * The first slot contains descriptors for the remaining 15 cores
 *
 * The slot descriptor is:
 * u32 res_id;
 * u32 type;
 * u32 vma;
 *
 * Log buffer slots have the following layout:
 * u32 host_read_ptr;
 * u32 dsp_write_ptr;
 * u8 buffer[];
 *
 * The two pointers are offsets within the buffer.
 */

#define FW_EPOCH_DELTA			11644473600LL

#define INVALID_SLOT_OFFSET		0xffffffff
#define MAX_ALLOWED_LIBRARIES		16
#define MAX_MTRACE_SLOTS		16

#define SOF_MTRACE_SLOT_SIZE		0x1000

/* debug log slot types */
#define SOF_MTRACE_SLOT_UNUSED		0x00000000
#define SOF_MTRACE_SLOT_CRITICAL_LOG	0x54524300 /* byte 0: core ID */
#define SOF_MTRACE_SLOT_DEBUG_LOG	0x474f4c00 /* byte 0: core ID */
#define SOF_MTRACE_SLOT_GDB_STUB	0x42444700
#define SOF_MTRACE_SLOT_TELEMETRY	0x4c455400
#define SOF_MTRACE_SLOT_BROKEN		0x44414544
 /* for debug and critical types */
#define SOF_MTRACE_SLOT_CORE_MASK	GENMASK(7, 0)
#define SOF_MTRACE_SLOT_TYPE_MASK	GENMASK(31, 8)

/* ipc4 log level and source definitions for logs_priorities_mask */
#define SOF_MTRACE_LOG_LEVEL_CRITICAL	BIT(0)
#define SOF_MTRACE_LOG_LEVEL_ERROR	BIT(1)
#define SOF_MTRACE_LOG_LEVEL_WARNING	BIT(2)
#define SOF_MTRACE_LOG_LEVEL_INFO	BIT(3)
#define SOF_MTRACE_LOG_LEVEL_VERBOSE	BIT(4)
#define SOF_MTRACE_LOG_SOURCE_INFRA	BIT(5)
#define SOF_MTRACE_LOG_SOURCE_HAL	BIT(6)
#define SOF_MTRACE_LOG_SOURCE_MODULE	BIT(7)
#define SOF_MTRACE_LOG_SOURCE_AUDIO	BIT(8)
#define SOF_MTRACE_LOG_DEFAULTS		(SOF_MTRACE_LOG_LEVEL_CRITICAL | \
					 SOF_MTRACE_LOG_LEVEL_ERROR |	 \
					 SOF_MTRACE_LOG_LEVEL_WARNING |	 \
					 SOF_MTRACE_LOG_LEVEL_INFO |	 \
					 SOF_MTRACE_LOG_SOURCE_INFRA |	 \
					 SOF_MTRACE_LOG_SOURCE_HAL |	 \
					 SOF_MTRACE_LOG_SOURCE_MODULE |	 \
					 SOF_MTRACE_LOG_SOURCE_AUDIO)

struct sof_log_state_info {
	u32 aging_timer_period;
	u32 fifo_full_timer_period;
	u32 enable;
	u32 logs_priorities_mask[MAX_ALLOWED_LIBRARIES];
} __packed;

struct sof_mtrace_core_data {
	struct snd_sof_dev *sdev;

	int id;
	u32 slot_offset;
	u32 host_read_ptr;
	u32 dsp_write_ptr;
	bool missed_update;
	wait_queue_head_t trace_sleep;
};

struct sof_mtrace_priv {
	struct snd_sof_dev *sdev;
	bool mtrace_is_enabled;
	struct sof_log_state_info state_info;

	struct sof_mtrace_core_data cores[];
};

static bool sof_wait_mtrace_avail(struct sof_mtrace_core_data *core_data)
{
	wait_queue_entry_t wait;

	/* data immediately available */
	if (core_data->host_read_ptr != core_data->dsp_write_ptr)
		return TRUE;

	/* wait for available trace data from FW */
	init_waitqueue_entry(&wait, current);
	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&core_data->trace_sleep, &wait);

	if (!signal_pending(current)) {
		/* set timeout to max value, no error code */
		schedule_timeout(MAX_SCHEDULE_TIMEOUT);
	}
	remove_wait_queue(&core_data->trace_sleep, &wait);

	if (core_data->host_read_ptr != core_data->dsp_write_ptr)
		return TRUE;

	return false;
}

static ssize_t sof_ipc4_mtrace_read(struct file *file, char __user *buffer,
				    size_t count, loff_t *ppos)
{
	struct sof_mtrace_core_data *core_data = file->private_data;
	struct snd_sof_dev *sdev = core_data->sdev;
	u32 log_data_offset = core_data->slot_offset + 8;
	u32 log_data_size = SOF_MTRACE_SLOT_SIZE - 8;
	u32 read_ptr, write_ptr;
	loff_t lpos = *ppos;
	void *log_data;
	u32 avail;
	int ret;

	/* check pos and count */
	if (lpos < 0)
		return -EINVAL;
	if (!count || count < sizeof(avail))
		return 0;

	/* get available count based on current host offset */
	if (!sof_wait_mtrace_avail(core_data))
		return 0;

	if (core_data->slot_offset == INVALID_SLOT_OFFSET)
		return 0;

	read_ptr = core_data->host_read_ptr;
	write_ptr = core_data->dsp_write_ptr;

	if (read_ptr < write_ptr)
		avail = write_ptr - read_ptr;
	else
		avail = log_data_size - read_ptr + write_ptr;

	if (!avail)
		return 0;

	if (avail > log_data_size)
		avail = log_data_size;

	/* Need space for the initial u32 of the avail */
	if (avail > count - sizeof(avail))
		avail = count - sizeof(avail);

	if (sof_debug_check_flag(SOF_DBG_PRINT_DMA_POSITION_UPDATE_LOGS))
		dev_dbg(sdev->dev,
			"%s: core%d, host read: %#x, dsp write: %#x, avail: %#x",
			__func__, core_data->id, read_ptr, write_ptr, avail);

	log_data = kmalloc(avail, GFP_KERNEL);
	if (!log_data)
		return -ENOMEM;

	if (read_ptr < write_ptr) {
		/* Read data between read pointer and write pointer */
		sof_mailbox_read(sdev, log_data_offset + read_ptr, log_data, avail);
	} else {
		/* read from read pointer to end of the slot */
		sof_mailbox_read(sdev, log_data_offset + read_ptr, log_data,
				 avail - write_ptr);
		/* read from slot start to write pointer */
		if (write_ptr)
			sof_mailbox_read(sdev, log_data_offset,
					 (u8 *)(log_data) + avail - write_ptr,
					 write_ptr);
	}

	/* first write the number of bytes we have gathered */
	ret = copy_to_user(buffer, &avail, sizeof(avail));
	if (ret)
		return -EFAULT;

	/* Followed by the data itself */
	ret = copy_to_user(buffer + sizeof(avail), log_data, avail);
	if (ret)
		return -EFAULT;

	/* Update the host_read_ptr in the slot for this core */
	read_ptr += avail;
	if (read_ptr >= log_data_size)
		read_ptr -= log_data_size;
	sof_mailbox_write(sdev, core_data->slot_offset, &read_ptr, sizeof(read_ptr));

	core_data->host_read_ptr = read_ptr;

	/*
	 * Ask for a new buffer from user space for the next chunk, not
	 * streaming due to the heading number of bytes value.
	 */
	*ppos += count;

	return count;
}

static const struct file_operations sof_dfs_mtrace_fops = {
	.open = simple_open,
	.read = sof_ipc4_mtrace_read,
	.llseek = default_llseek,
};

static int mtrace_debugfs_create(struct snd_sof_dev *sdev)
{
	struct sof_mtrace_priv *priv = sdev->fw_trace_data;
	struct dentry *dfs_root;
	char dfs_name[100];
	int i;

	if (!sdev)
		return -EINVAL;

	dfs_root = debugfs_create_dir("mtrace", sdev->debugfs_root);
	if (IS_ERR_OR_NULL(dfs_root))
		return 0;

	/* Create files for the logging parameters */
	debugfs_create_u32("aging_timer_period", 0644, dfs_root,
			   &priv->state_info.aging_timer_period);
	debugfs_create_u32("fifo_full_timer_period", 0644, dfs_root,
			   &priv->state_info.fifo_full_timer_period);
	/* Separate priorities mask file per library, index 0 is basefw */
	for (i = 0; i < MAX_ALLOWED_LIBRARIES; i++) {
		snprintf(dfs_name, sizeof(dfs_name), "logs_priorities_mask_%d", i);
		debugfs_create_x32(dfs_name, 0644, dfs_root,
				   &priv->state_info.logs_priorities_mask[i]);
	}

	/* Separate log files per core */
	for (i = 0; i < sdev->num_cores; i++) {
		snprintf(dfs_name, sizeof(dfs_name), "core%d", i);
		debugfs_create_file(dfs_name, 0444, dfs_root, &priv->cores[i],
				    &sof_dfs_mtrace_fops);
	}

	return 0;
}

static int ipc4_mtrace_enable(struct snd_sof_dev *sdev)
{
	struct sof_mtrace_priv *priv = sdev->fw_trace_data;
	const struct sof_ipc_ops *iops = sdev->ipc->ops;
	struct sof_ipc4_msg msg;
	u64 system_time;
	ktime_t kt;

	if (priv->mtrace_is_enabled)
		return 0;

	msg.primary = SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);
	msg.primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	msg.primary |= SOF_IPC4_MOD_ID(SOF_IPC4_MOD_INIT_BASEFW_MOD_ID);
	msg.primary |= SOF_IPC4_MOD_INSTANCE(SOF_IPC4_MOD_INIT_BASEFW_INSTANCE_ID);
	msg.extension = SOF_IPC4_MOD_EXT_MSG_PARAM_ID(SOF_IPC4_FW_PARAM_SYSTEM_TIME);

	/* The system time is in usec, UTC, epoch is 1601-01-01 00:00:00 */
	kt = ktime_add_us(ktime_get_real(), FW_EPOCH_DELTA * USEC_PER_SEC);
	system_time = ktime_to_us(kt);
	msg.data_size = sizeof(system_time);
	msg.data_ptr = &system_time;
	if (iops->set_get_data(sdev, &msg, msg.data_size, true)) {
		sdev->fw_trace_is_supported = false;
		return 0;
	}

	msg.extension = SOF_IPC4_MOD_EXT_MSG_PARAM_ID(SOF_IPC4_FW_PARAM_ENABLE_LOGS);

	priv->state_info.enable = 1;

	msg.data_size = sizeof(priv->state_info);
	msg.data_ptr = &priv->state_info;
	if (iops->set_get_data(sdev, &msg, msg.data_size, true)) {
		sdev->fw_trace_is_supported = false;
		return 0;
	}

	priv->mtrace_is_enabled = true;

	return 0;
}

static void ipc4_mtrace_disable(struct snd_sof_dev *sdev)
{
	struct sof_mtrace_priv *priv = sdev->fw_trace_data;
	const struct sof_ipc_ops *iops = sdev->ipc->ops;
	struct sof_ipc4_msg msg;
	int i;

	if (!priv->mtrace_is_enabled)
		return;

	msg.primary = SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);
	msg.primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	msg.primary |= SOF_IPC4_MOD_ID(SOF_IPC4_MOD_INIT_BASEFW_MOD_ID);
	msg.primary |= SOF_IPC4_MOD_INSTANCE(SOF_IPC4_MOD_INIT_BASEFW_INSTANCE_ID);
	msg.extension = SOF_IPC4_MOD_EXT_MSG_PARAM_ID(SOF_IPC4_FW_PARAM_ENABLE_LOGS);

	priv->state_info.enable = 0;

	msg.data_size = sizeof(priv->state_info);
	msg.data_ptr = &priv->state_info;
	if (iops->set_get_data(sdev, &msg, msg.data_size, true))
		sdev->fw_trace_is_supported = false;

	priv->mtrace_is_enabled = false;

	for (i = 0; i < sdev->num_cores; i++) {
		struct sof_mtrace_core_data *core_data = &priv->cores[i];

		core_data->host_read_ptr = 0;
		core_data->dsp_write_ptr = 0;
		wake_up(&core_data->trace_sleep);
	}
}

/*
 * Parse the slot descriptors at debug_box offset, we are only interested in the
 * type of the slot to map it to a core
 */
static void sof_mtrace_find_core_slots(struct snd_sof_dev *sdev)
{
	struct sof_mtrace_priv *priv = sdev->fw_trace_data;
	struct sof_mtrace_core_data *core_data;
	u32 slot_desc_offset, type, core;
	int i;

	slot_desc_offset = sdev->debug_box.offset;
	for (i = 0; i < MAX_MTRACE_SLOTS - 1; i++) {
		/* Read the slot type */
		sof_mailbox_read(sdev, slot_desc_offset + 4, &type, 4);
		if ((type & SOF_MTRACE_SLOT_TYPE_MASK) == SOF_MTRACE_SLOT_DEBUG_LOG) {
			core = type & SOF_MTRACE_SLOT_CORE_MASK;

			if (core >= sdev->num_cores)
				continue;

			core_data = &priv->cores[core];
			core_data->slot_offset = sdev->debug_box.offset;
			core_data->slot_offset += SOF_MTRACE_SLOT_SIZE * (i + 1);
			dev_dbg(sdev->dev, "%s: slot%d is used for core%u\n",
				__func__, i, core);
			if (core_data->missed_update) {
				sof_ipc4_mtrace_update_pos(sdev, core);
				core_data->missed_update = false;
			}
		} else if (type) {
			dev_dbg(sdev->dev, "%s: slot%d is not a log slot (%#x)\n",
				__func__, i, type);
		}

		slot_desc_offset += sizeof(u32) * 3;
	}
}

static int ipc4_mtrace_init(struct snd_sof_dev *sdev)
{
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	struct sof_mtrace_priv *priv;
	int i, ret;

	if (sdev->fw_trace_data) {
		dev_err(sdev->dev, "fw_trace_data has been already allocated\n");
		return -EBUSY;
	}

	if (!ipc4_data->mtrace_log_bytes ||
	    ipc4_data->mtrace_type != SOF_IPC4_MTRACE_INTEL_TGL) {
		sdev->fw_trace_is_supported = false;
		return 0;
	}

	priv = devm_kzalloc(sdev->dev, struct_size(priv, cores, sdev->num_cores),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	sdev->fw_trace_data = priv;

	/* default enable default trace state_info */
	priv->state_info.aging_timer_period = 10;
	priv->state_info.fifo_full_timer_period = 10;
	/* Only enable basefw logs initially (index 0 is always basefw) */
	priv->state_info.logs_priorities_mask[0] = SOF_MTRACE_LOG_DEFAULTS;

	for (i = 0; i < sdev->num_cores; i++) {
		struct sof_mtrace_core_data *core_data = &priv->cores[i];

		init_waitqueue_head(&core_data->trace_sleep);
		core_data->sdev = sdev;
		core_data->id = i;
	}

	ret = mtrace_debugfs_create(sdev);
	if (ret < 0)
		return ret;

	ret = ipc4_mtrace_enable(sdev);
	if (ret)
		return ret;

	sof_mtrace_find_core_slots(sdev);

	return 0;
}

static void ipc4_mtrace_free(struct snd_sof_dev *sdev)
{
	ipc4_mtrace_disable(sdev);
}

int sof_ipc4_mtrace_update_pos(struct snd_sof_dev *sdev, int core)
{
	struct sof_mtrace_priv *priv = sdev->fw_trace_data;
	struct sof_mtrace_core_data *core_data;

	if (!sdev->fw_trace_is_supported)
		return 0;

	if (core >= sdev->num_cores)
		return -EINVAL;

	core_data = &priv->cores[core];

	if (core_data->slot_offset == INVALID_SLOT_OFFSET) {
		core_data->missed_update = true;
		return 0;
	}

	/* Read out the dsp_write_ptr from the slot for this core */
	sof_mailbox_read(sdev, core_data->slot_offset + sizeof(u32),
			 &core_data->dsp_write_ptr, 4);
	core_data->dsp_write_ptr -= core_data->dsp_write_ptr % 4;

	if (sof_debug_check_flag(SOF_DBG_PRINT_DMA_POSITION_UPDATE_LOGS))
		dev_dbg(sdev->dev, "%s: core%d, host read: %#x, dsp write: %#x",
			__func__, core, core_data->host_read_ptr,
			core_data->dsp_write_ptr);

	wake_up(&core_data->trace_sleep);

	return 0;
}

static int ipc4_mtrace_resume(struct snd_sof_dev *sdev)
{
	return ipc4_mtrace_enable(sdev);
}

static void ipc4_mtrace_suspend(struct snd_sof_dev *sdev, pm_message_t pm_state)
{
	ipc4_mtrace_disable(sdev);
}

const struct sof_ipc_fw_tracing_ops ipc4_mtrace_ops = {
	.init = ipc4_mtrace_init,
	.free = ipc4_mtrace_free,
	.suspend = ipc4_mtrace_suspend,
	.resume = ipc4_mtrace_resume,
};
