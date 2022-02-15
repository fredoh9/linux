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

#define SOF_IPC4_BASE_FW	0

#define INVALID_SLOT_RESOURCE_ID    0xffffffff
#define MEMORY_WINDOW_SLOTS_COUNT   15
#define MEMORY_WINDOW_SLOT_SIZE     0x1000
#define SLOT_DEBUG_LOG	0x474f4c00
#define SLOT_DEBUG_LOG_MASK	GENMASK(31, 8)
#define MAX_ALLOWED_LIBRARIES   16

/* ipc4 mtrace */
enum sof_mtrace_level
{
	L_CRITICAL = BIT(0),
	L_ERROR = BIT(1),
	L_WARNING = BIT(2),
	L_INFO = BIT(3),
	L_VERBOSE = BIT(4),
	L_DEFAULT = L_CRITICAL | L_ERROR |L_INFO
};

enum sof_mtrace_source
{
	S_INFRA = BIT(5),
	S_HAL = BIT(6),
	S_MODULE  = BIT(7),
	S_AUDIO = BIT(8),
	S_DEFAULT = S_INFRA | S_HAL | S_MODULE |S_AUDIO
};

struct sof_log_setting
{
    uint32_t aging_timer_period;
    uint32_t fifo_full_timer_period;
    uint32_t enable;
    uint32_t logs_priorities_mask[MAX_ALLOWED_LIBRARIES];
};

struct sof_mtrace_priv {
	wait_queue_head_t trace_sleep;
	bool mtrace_is_enabled;
	u32 mtrace_setting;
	atomic_t use_count;
	u32 host_read_ptr;
	u32 dsp_write_ptr;
};

static bool sof_wait_mtrace_avail(struct snd_sof_dev *sdev)
{
	struct sof_mtrace_priv *priv = sdev->fw_trace_data;
	wait_queue_entry_t wait;

	/* data immediately available */
	if (priv->host_read_ptr != priv->dsp_write_ptr)
		return TRUE;

	/* wait for available trace data from FW */
	init_waitqueue_entry(&wait, current);
	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&priv->trace_sleep, &wait);

	if (!signal_pending(current)) {
		/* set timeout to max value, no error code */
		schedule_timeout(MAX_SCHEDULE_TIMEOUT);
	}
	remove_wait_queue(&priv->trace_sleep, &wait);

	if (priv->host_read_ptr != priv->dsp_write_ptr)
		return TRUE;

	return false;
}

static ssize_t sof_ipc4_mtrace_read(struct file *file, char __user *buffer,
				       size_t count, loff_t *ppos)
{
	struct snd_sof_dfsentry *dfse = file->private_data;
	struct snd_sof_dev *sdev = dfse->sdev;
	struct sof_mtrace_priv *priv = sdev->fw_trace_data;
	u32 read_ptr, write_ptr;
	loff_t lpos = *ppos;
	u32 avail;
	int ret;

	/* check pos and count */
	if (lpos < 0)
		return -EINVAL;
	if (!count)
		return 0;

	/* get available count based on current host offset */
	if (!sof_wait_mtrace_avail(sdev)) {
		dev_dbg(sdev->dev, "got unexpected error");
		return 0;
	}

	read_ptr = priv->host_read_ptr;
	write_ptr = priv->dsp_write_ptr;
	// ring buffer
	if (read_ptr < write_ptr)
	{
		// check if output buffer is sufficient in size
		if ((write_ptr - read_ptr) > MEMORY_WINDOW_SLOT_SIZE)
		{
			dev_err(sdev->dev, "Output log buffer is insufficient");
			return -ENOMEM;
		}
		else
		{
			avail = write_ptr -read_ptr;

			ret = copy_to_user(buffer, &avail, sizeof(avail));
			if (ret)
				return -EFAULT;

			ret = copy_to_user(buffer + sizeof(avail), ((u8 *)(dfse->buf) + read_ptr), avail);
			if (ret)
				return -EFAULT;

			// update read_ptr to write ptr for fw to update log
			memcpy_toio(dfse->io_mem - 8, &write_ptr, sizeof(write_ptr));
		}
	}
	else
	{
		u32 slotBufferSize = MEMORY_WINDOW_SLOT_SIZE - sizeof(u32) - sizeof(u32);
		// check if output buffer is sufficient in size
		if ((slotBufferSize + write_ptr - read_ptr) > MEMORY_WINDOW_SLOT_SIZE)
		{
			dev_err(sdev->dev, "Output log buffer is insufficient");
			return -ENOMEM;
		}
		else
		{
			/* skip the 8 bytes of read&write pointer in debug memory box */
			avail = slotBufferSize - read_ptr + write_ptr -8;
			ret = copy_to_user(buffer, &avail, sizeof(avail));
			if (ret)
				return -EFAULT;

			ret = copy_to_user(buffer + sizeof(avail), (u8 *)(dfse->buf) + read_ptr, (slotBufferSize - read_ptr));
			if (ret)
				return -EFAULT;

			ret = copy_to_user(buffer + sizeof(avail) + (slotBufferSize - read_ptr), (u8 *)(dfse->buf), write_ptr);
			if (ret)
				return -EFAULT;

			memcpy_toio(dfse->io_mem - 8, &write_ptr, sizeof(write_ptr));
		}
	}

	priv->host_read_ptr = write_ptr;
	*ppos += MEMORY_WINDOW_SLOT_SIZE;

	/* move debugfs reading position */
	return MEMORY_WINDOW_SLOT_SIZE;
}

static ssize_t sof_ipc4_mtrace_write(struct file *file,
		const char __user *from, size_t count, loff_t *ppos)
{
	struct snd_sof_dfsentry *dfse = file->private_data;
	struct snd_sof_dev *sdev = dfse->sdev;
	struct sof_mtrace_priv *priv = sdev->fw_trace_data;
	char *buf;
	int ret;

	buf = kmalloc(count + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = simple_write_to_buffer(buf, count, ppos, from, count);
	if (ret != count) {
		ret = ret >= 0 ? -EIO : ret;
		goto exit;
	}

	buf[count] = '\0';
	ret = kstrtouint(buf, 0, &priv->mtrace_setting);
	dev_dbg(sdev->dev, "set mtrace config %x", priv->mtrace_setting);

	ret = ret ? 0 : sizeof(int);
exit:
	kfree(buf);
	return ret;
}

static int sof_dfsentry_mtrace_release(struct inode *inode, struct file *file)
{
	struct snd_sof_dfsentry *dfse = file->private_data;
	struct snd_sof_dev *sdev = dfse->sdev;
	struct sof_mtrace_priv *priv = sdev->fw_trace_data;

	priv->mtrace_is_enabled = false;
	return 0;
}

static const struct file_operations sof_dfs_mtrace_fops = {
	.open = simple_open,
	.read = sof_ipc4_mtrace_read,
	.write = sof_ipc4_mtrace_write,
	.llseek = default_llseek,
	.release = sof_dfsentry_mtrace_release,
};

/*
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
	struct snd_sof_dfsentry *dfse;

	if (!sdev)
		return -EINVAL;

	dfse = devm_kzalloc(sdev->dev, sizeof(*dfse), GFP_KERNEL);
	if (!dfse)
		return -ENOMEM;

	dfse->type = SOF_DFSENTRY_TYPE_IOMEM;
	dfse->io_mem = sdev->bar[sdev->mailbox_bar] + sdev->debug_box.offset +
		MEMORY_WINDOW_SLOT_SIZE + 8;
	dfse->size = MEMORY_WINDOW_SLOT_SIZE - 8;
	dfse->sdev = sdev;

	debugfs_create_file("mtrace", 0444, sdev->debugfs_root, dfse,
			    &sof_dfs_mtrace_fops);

	return 0;
}

static int ipc4_mtrace_init(struct snd_sof_dev *sdev)
{
	struct sof_mtrace_priv *priv;

	if (sdev->fw_trace_data) {
		dev_err(sdev->dev, "fw_trace_data has been already allocated\n");
		return -EBUSY;
	}

	priv = devm_kzalloc(sdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	sdev->fw_trace_data = priv;

	/* default enable default trace setting */
	priv->mtrace_setting = L_DEFAULT | S_DEFAULT;

	if (sdev->first_boot) {
		int ret = mtrace_debugfs_create(sdev);
		if (ret < 0)
			return ret;
	}

	init_waitqueue_head(&priv->trace_sleep);

	return 0;
}

int sof_ipc4_mtrace_update_pos(struct snd_sof_dev *sdev)
{
	struct sof_mtrace_priv *priv = sdev->fw_trace_data;
	int debug_address;
	int res, slot;

	if (!sdev->fw_trace_is_supported)
		return 0;

	if (!priv->mtrace_is_enabled)
		return 0;

	debug_address = sdev->debug_box.offset;

	sof_mailbox_read(sdev, debug_address, &res, 4);
	sof_mailbox_read(sdev, debug_address + 4, &slot, 4);

	dev_dbg(sdev->dev, "resource id %x, slot id %x", res, slot);

	if ((slot & SLOT_DEBUG_LOG_MASK) != SLOT_DEBUG_LOG) {
		dev_dbg(sdev->dev, "invalid log msg");
		return 0;
	}

	if (res == INVALID_SLOT_RESOURCE_ID) {
		dev_dbg(sdev->dev, "invalid cpu id");
		return 0;
	}

	debug_address += MEMORY_WINDOW_SLOT_SIZE;
	sof_mailbox_read(sdev, debug_address + 4, &priv->dsp_write_ptr, 4);
	priv->dsp_write_ptr -= priv->dsp_write_ptr % 4;

	dev_vdbg(sdev->dev, "host read %x, dsp write %x", priv->host_read_ptr,
		priv->dsp_write_ptr);

	wake_up(&priv->trace_sleep);

	return 0;
}

static int ipc4_mtrace_resume(struct snd_sof_dev *sdev)
{
	struct sof_mtrace_priv *priv = sdev->fw_trace_data;
	const struct sof_ipc_ops *iops = sdev->ipc->ops;
	struct sof_log_setting setting;
	ktime_t current_time;
	struct sof_ipc4_msg msg;

	atomic_inc(&priv->use_count);

	if (priv->mtrace_is_enabled)
		return 0;

	priv->mtrace_is_enabled = true;
	current_time = ktime_get_real();

	msg.primary = 0x44000000;
	msg.extension = 0x21400000;
	msg.data_size = sizeof(current_time);
	msg.data_ptr = &current_time;
	if (iops->set_get_data(sdev, &msg, msg.data_size, true)) {
		sdev->fw_trace_is_supported = false;
		return 0;
	}

	setting.enable = 1;
	setting.aging_timer_period = 0x400;
	setting.fifo_full_timer_period = 0x1000;
	memzero_explicit(&setting.logs_priorities_mask, 16*sizeof(uint32_t));
	setting.logs_priorities_mask[SOF_IPC4_BASE_FW] = priv->mtrace_setting;

	msg.extension = 0x20600000;
	msg.data_size = sizeof(setting);
	msg.data_ptr = &setting;
	if (iops->set_get_data(sdev, &msg, msg.data_size, true)) {
		sdev->fw_trace_is_supported = false;
		return 0;
	}

	return 0;
}

static void ipc4_mtrace_suspend(struct snd_sof_dev *sdev, pm_message_t pm_state)
{
	struct sof_mtrace_priv *priv = sdev->fw_trace_data;
	const struct sof_ipc_ops *iops = sdev->ipc->ops;
	struct sof_log_setting setting;
	struct sof_ipc4_msg msg;

	if (!priv->mtrace_is_enabled)
		return;

	if (!atomic_dec_and_test(&priv->use_count))
		return;

	priv->host_read_ptr = priv->dsp_write_ptr = 0;

	setting.enable = 0;
	setting.aging_timer_period = 0;
	setting.fifo_full_timer_period = 0;
	memzero_explicit(&setting.logs_priorities_mask, 16*sizeof(uint32_t));

	msg.primary = 0x44000000;
	msg.extension = 0x20600000;
	msg.data_size = sizeof(setting);
	msg.data_ptr = &setting;
	if (iops->set_get_data(sdev, &msg, msg.data_size, true))
		sdev->fw_trace_is_supported = false;

	priv->mtrace_is_enabled = false;
	wake_up(&priv->trace_sleep);
}

const struct sof_ipc_fw_tracing_ops ipc4_mtrace_ops = {
	.init = ipc4_mtrace_init,
	.suspend = ipc4_mtrace_suspend,
	.resume = ipc4_mtrace_resume,
};
