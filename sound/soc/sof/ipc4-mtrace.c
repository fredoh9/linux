// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Intel Corporation. All rights reserved.
//
// Author: Rander Wang <rander.wang@linux.intel.com>
//

#include <linux/debugfs.h>
#include <linux/sched/signal.h>
#include <sound/sof/ipc4/header.h>
#include "sof-priv.h"
#include "ipc4-priv.h"

#define SOF_IPC4_BASE_FW		0

#define INVALID_SLOT_OFFSET		0xffffffff
#define MEMORY_WINDOW_SLOTS_COUNT	15
#define MAX_ALLOWED_LIBRARIES   	16
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

/* ipc4 mtrace */
enum sof_mtrace_level {
	L_CRITICAL = BIT(0),
	L_ERROR = BIT(1),
	L_WARNING = BIT(2),
	L_INFO = BIT(3),
	L_VERBOSE = BIT(4),
	L_DEFAULT = L_CRITICAL | L_ERROR |L_INFO
};

enum sof_mtrace_source {
	S_INFRA = BIT(5),
	S_HAL = BIT(6),
	S_MODULE  = BIT(7),
	S_AUDIO = BIT(8),
	S_DEFAULT = S_INFRA | S_HAL | S_MODULE |S_AUDIO
};

struct sof_log_state_info {
    uint32_t aging_timer_period;
    uint32_t fifo_full_timer_period;
    uint32_t enable;
    uint32_t logs_priorities_mask[MAX_ALLOWED_LIBRARIES];
} __packed;

struct sof_mtrace_slot {
	struct snd_sof_dev *sdev;

	wait_queue_head_t trace_sleep;
	u32 slot_offset;
	u32 host_read_ptr;
	u32 dsp_write_ptr;
	bool missed_update;
};

struct sof_mtrace_priv {
	struct snd_sof_dev *sdev;
	bool mtrace_is_enabled;
	u32 fifo_full_timer_period;
	u32 aging_timer_period;
	u32 log_features;

// 	wait_queue_head_t trace_sleep;
// 	u32 host_read_ptr;
// 	u32 dsp_write_ptr;

	struct sof_mtrace_slot slots[];
};

static bool sof_wait_mtrace_avail(struct sof_mtrace_slot *slot_data)
{
	wait_queue_entry_t wait;

	/* data immediately available */
	if (slot_data->host_read_ptr != slot_data->dsp_write_ptr)
		return TRUE;

	/* wait for available trace data from FW */
	init_waitqueue_entry(&wait, current);
	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&slot_data->trace_sleep, &wait);

	if (!signal_pending(current)) {
		/* set timeout to max value, no error code */
		schedule_timeout(MAX_SCHEDULE_TIMEOUT);
	}
	remove_wait_queue(&slot_data->trace_sleep, &wait);

	if (slot_data->host_read_ptr != slot_data->dsp_write_ptr)
		return TRUE;

	return false;
}

static ssize_t sof_ipc4_mtrace_read(struct file *file, char __user *buffer,
				    size_t count, loff_t *ppos)
{
	struct sof_mtrace_slot *slot_data = file->private_data;
	struct snd_sof_dev *sdev = slot_data->sdev;
	u32 log_data_offset = slot_data->slot_offset + 8;
	u32 log_data_size = SOF_MTRACE_SLOT_SIZE - 8;
	u32 read_ptr, write_ptr;
	loff_t lpos = *ppos;
	void *log_data;
	u32 avail;
	int ret;

	/* check pos and count */
	if (lpos < 0)
		return -EINVAL;
	if (!count)
		return 0;

	/* get available count based on current host offset */
	if (!sof_wait_mtrace_avail(slot_data))
		return 0;

	if (slot_data->slot_offset == INVALID_SLOT_OFFSET)
		return 0;

	read_ptr = slot_data->host_read_ptr;
	write_ptr = slot_data->dsp_write_ptr;

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

	dev_dbg(sdev->dev, "%s: host read %#x, dsp write %#x, avail %#x", __func__,
		read_ptr, write_ptr, avail);

	log_data = kzalloc(avail, GFP_KERNEL);
	if (!log_data)
		return -ENOMEM;
	// ring buffer
	if (read_ptr < write_ptr) {
		/* Read from read pointer to write pointer */
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

	ret = copy_to_user(buffer, &avail, sizeof(avail));
	if (ret)
		return -EFAULT;

	ret = copy_to_user(buffer + sizeof(avail), log_data, avail);
	if (ret)
		return -EFAULT;

	sof_mailbox_write(sdev, slot_data->slot_offset, &write_ptr, sizeof(write_ptr));

	slot_data->host_read_ptr = write_ptr;

	/* move debugfs reading position */
	*ppos += avail;

	return avail;
}

static const struct file_operations sof_dfs_mtrace_fops = {
	.open = simple_open,
	.read = sof_ipc4_mtrace_read,
	.llseek = default_llseek,
};

/*
 * debug info window is orginized in 16 slots.
 *
 * The first slot contains descriptors for the remaining 15 slots
 *
 * debug memory windows layout at debug_box offset:
 * u32 resouce0 id | u32 slot0 id | u32 vma0
 * u32 resouce1 id | u32 slot1 id | u32 vma1
 * ...
 * u32 resouce14 id | u32 slot14 id | u32 vma14
 * ...    after 0x1000
 * read_ptr0 | write_ptr0 | log of 0x1000 - 8
 * read_ptr1 | write_ptr1 | log of 0x1000 - 8
 * ...
 * read_ptr14 | write_ptr14 | log of 0x1000 - 8
 *
 * the first slot is for base fw and others are for loadable
 * modules. Now only fw log of base fw is supported
*/
static int mtrace_debugfs_create(struct snd_sof_dev *sdev)
{
	struct sof_mtrace_priv *priv = sdev->fw_trace_data;
	struct dentry *dfs_root;
	int i;

	if (!sdev)
		return -EINVAL;

	dfs_root = debugfs_create_dir("mtrace_root", sdev->debugfs_root);
	if (IS_ERR_OR_NULL(dfs_root))
		return 0;

	debugfs_create_u32("aging_timer_period", 0644, dfs_root,
			   &priv->aging_timer_period);
	debugfs_create_u32("fifo_full_timer_period", 0644, dfs_root,
			   &priv->fifo_full_timer_period);
	debugfs_create_x32("log_features", 0644, dfs_root,
			   &priv->log_features);

	for (i = 0; i < sdev->num_cores; i++) {
		char dfs_name[100];

		snprintf(dfs_name, sizeof(dfs_name), "core%d", i);
		debugfs_create_file(dfs_name, 0444, dfs_root, &priv->slots[i],
				    &sof_dfs_mtrace_fops);
		if (i == 0)
			debugfs_create_symlink("mtrace", sdev->debugfs_root, "mtrace_root/core0");
	}

	return 0;
}
/*
 * debug memory windows layout at debug_box offset:
 * u32 resouce0 id | u32 slot0 type | u32 vma0
 * u32 resouce1 id | u32 slot1 type | u32 vma1
 * ...
 * u32 resouce14 id | u32 slot14 id | u32 vma14
 */
static void sof_mtrace_find_core_slots(struct snd_sof_dev *sdev)
{
	struct sof_mtrace_priv *priv = sdev->fw_trace_data;
	struct sof_mtrace_slot *slot_data;
	u32 debug_address, val, core, slot;
	int i;

	debug_address = sdev->debug_box.offset;
	for (i = 0; i < MAX_MTRACE_SLOTS - 1; i++) {
		/* Read the slot type */
		sof_mailbox_read(sdev, debug_address + 4, &val, 4);
		if ((val & SOF_MTRACE_SLOT_TYPE_MASK) == SOF_MTRACE_SLOT_DEBUG_LOG) {
			core = val & SOF_MTRACE_SLOT_CORE_MASK;

			if (core >= sdev->num_cores)
				continue;

			sof_mailbox_read(sdev, debug_address, &slot, 4);
			/* slot == 0 is the 2nd slot in the debug window */
			slot++;
			if (!slot || slot >= MAX_MTRACE_SLOTS)
				continue;

			slot_data = &priv->slots[core];
			slot_data->slot_offset = sdev->debug_box.offset;
			slot_data->slot_offset += SOF_MTRACE_SLOT_SIZE * slot;
			if (slot_data->missed_update) {
				sof_ipc4_mtrace_update_pos(sdev, core);
				slot_data->missed_update = false;
			}
		}

		debug_address += 12;
	}
}

static int ipc4_mtrace_enable(struct snd_sof_dev *sdev)
{
	struct sof_mtrace_priv *priv = sdev->fw_trace_data;
	const struct sof_ipc_ops *iops = sdev->ipc->ops;
	struct sof_log_state_info state_info;
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
	kt = ktime_add_us(ktime_get_real(), 11644473600 * USEC_PER_SEC);
	system_time = ktime_to_us(kt);
	msg.data_size = sizeof(system_time);
	msg.data_ptr = &system_time;
	if (iops->set_get_data(sdev, &msg, msg.data_size, true)) {
		sdev->fw_trace_is_supported = false;
		return 0;
	}

	msg.extension = SOF_IPC4_MOD_EXT_MSG_PARAM_ID(SOF_IPC4_FW_PARAM_ENABLE_LOGS);

	state_info.enable = 1;
	state_info.aging_timer_period = priv->aging_timer_period;
	state_info.fifo_full_timer_period = priv->fifo_full_timer_period;
	memzero_explicit(&state_info.logs_priorities_mask,
			 sizeof(state_info.logs_priorities_mask));
	/* Only enable basefw logs for now */
	state_info.logs_priorities_mask[SOF_IPC4_BASE_FW] = priv->log_features;

	msg.data_size = sizeof(state_info);
	msg.data_ptr = &state_info;
	if (iops->set_get_data(sdev, &msg, msg.data_size, true)) {
		sdev->fw_trace_is_supported = false;
		return 0;
	}

	sof_mtrace_find_core_slots(sdev);

	priv->mtrace_is_enabled = true;

	return 0;
}

static void ipc4_mtrace_disable(struct snd_sof_dev *sdev)
{
	struct sof_mtrace_priv *priv = sdev->fw_trace_data;
	const struct sof_ipc_ops *iops = sdev->ipc->ops;
	struct sof_log_state_info state_info = { 0 };
	struct sof_ipc4_msg msg;
	int i;

	if (!priv->mtrace_is_enabled)
		return;

	msg.primary = SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);
	msg.primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	msg.primary |= SOF_IPC4_MOD_ID(SOF_IPC4_MOD_INIT_BASEFW_MOD_ID);
	msg.primary |= SOF_IPC4_MOD_INSTANCE(SOF_IPC4_MOD_INIT_BASEFW_INSTANCE_ID);
	msg.extension = SOF_IPC4_MOD_EXT_MSG_PARAM_ID(SOF_IPC4_FW_PARAM_ENABLE_LOGS);

	msg.data_size = sizeof(state_info);
	msg.data_ptr = &state_info;
	if (iops->set_get_data(sdev, &msg, msg.data_size, true))
		sdev->fw_trace_is_supported = false;

	priv->mtrace_is_enabled = false;

	for (i = 0; i < sdev->num_cores; i++) {
		struct sof_mtrace_slot *slot_data = &priv->slots[i];

		slot_data->host_read_ptr = 0;
		slot_data->dsp_write_ptr = 0;
		slot_data->slot_offset = INVALID_SLOT_OFFSET;
		wake_up(&slot_data->trace_sleep);
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

	if (!ipc4_data->mtrace_log_bytes) {
		sdev->fw_trace_is_supported = false;
		return 0;
	}

	switch (ipc4_data->mtrace_type) {
	case SOF_IPC4_MTRACE_INTEL_TGL:
		break;
	case SOF_IPC4_MTRACE_INTEL_APL:
	case SOF_IPC4_MTRACE_INTEL_SKL:
		dev_dbg(sdev->dev, "%s: mtrace type %d is not supported\n",
			__func__, ipc4_data->mtrace_type);
		fallthrough;
	default:
		sdev->fw_trace_is_supported = false;
		return 0;
	}

	priv = devm_kzalloc(sdev->dev, struct_size(priv, slots, sdev->num_cores),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	sdev->fw_trace_data = priv;

	/* default enable default trace state_info */
	priv->log_features = L_DEFAULT | S_DEFAULT;
	priv->aging_timer_period = 10;
	priv->fifo_full_timer_period = 10;

	for (i = 0; i < sdev->num_cores; i++) {
		struct sof_mtrace_slot *slot_data = &priv->slots[i];

		init_waitqueue_head(&slot_data->trace_sleep);
		slot_data->sdev = sdev;
	}

	ret = mtrace_debugfs_create(sdev);
	if (ret < 0)
		return ret;

	return ipc4_mtrace_enable(sdev);
}

static void ipc4_mtrace_free(struct snd_sof_dev *sdev)
{
	ipc4_mtrace_disable(sdev);
}

int sof_ipc4_mtrace_update_pos(struct snd_sof_dev *sdev, int core)
{
	struct sof_mtrace_priv *priv = sdev->fw_trace_data;
	struct sof_mtrace_slot *slot_data;

	if (!sdev->fw_trace_is_supported)
		return 0;

	if (core >= sdev->num_cores)
		return -EINVAL;

	slot_data = &priv->slots[core];

	if (slot_data->slot_offset == INVALID_SLOT_OFFSET) {
		slot_data->missed_update = true;
		return 0;
	}

	sof_mailbox_read(sdev, slot_data->slot_offset + 4,
			 &slot_data->dsp_write_ptr, 4);
	slot_data->dsp_write_ptr -= slot_data->dsp_write_ptr % 4;

	dev_dbg(sdev->dev, "%s: core%d, host read %#x, dsp write %#x", __func__,
		core, slot_data->host_read_ptr, slot_data->dsp_write_ptr);

	wake_up(&slot_data->trace_sleep);

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
