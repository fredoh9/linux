// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2020 Intel Corporation. All rights reserved.
//
// Author: Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
//

#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/virtual_bus.h>
#include "sof-client.h"
#include "sof-priv.h"
#include "ops.h"

static void sof_client_virtdev_release(struct virtbus_device *vdev)
{
	struct sof_client_dev *cdev = virtbus_dev_to_sof_client_dev(vdev);

	kfree(cdev);
}

int sof_client_dev_register(struct snd_sof_dev *sdev,
			    const char *name)
{
	struct sof_client_dev *cdev;
	struct virtbus_device *vdev;
	unsigned long time, timeout;
	int ret;

	cdev = kzalloc(sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return -ENOMEM;

	cdev->sdev = sdev;
	init_completion(&cdev->probe_complete);
	vdev = &cdev->vdev;
	vdev->match_name = name;
	vdev->dev.parent = sdev->dev;
	vdev->release = sof_client_virtdev_release;

	/*
	 * Register virtbus device for the client.
	 * The error path in virtbus_register_device() calls put_device(),
	 * which will free cdev in the release callback.
	 */
	ret = virtbus_register_device(vdev);
	if (ret < 0)
		return ret;

	/* make sure the probe is complete before updating client list */
	timeout = msecs_to_jiffies(SOF_CLIENT_PROBE_TIMEOUT_MS);
	time = wait_for_completion_timeout(&cdev->probe_complete, timeout);
	if (!time) {
		dev_err(sdev->dev, "error: probe of virtbus dev %s timed out\n",
			name);
		virtbus_unregister_device(vdev);
		return -ETIMEDOUT;
	}

	/* add to list of SOF client devices */
	mutex_lock(&sdev->client_mutex);
	list_add(&cdev->list, &sdev->client_list);
	mutex_unlock(&sdev->client_mutex);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(sof_client_dev_register, SND_SOC_SOF_CLIENT);

int sof_client_ipc_tx_message(struct sof_client_dev *cdev, u32 header,
			      void *msg_data, size_t msg_bytes,
			      void *reply_data, size_t reply_bytes)
{
	return sof_ipc_tx_message(cdev->sdev->ipc, header, msg_data, msg_bytes,
				  reply_data, reply_bytes);
}
EXPORT_SYMBOL_NS_GPL(sof_client_ipc_tx_message, SND_SOC_SOF_CLIENT);

/* host PCM ops */
int sof_client_pcm_open(struct snd_soc_component *component,
			struct snd_pcm_substream *substream)
{
	return sof_pcm_open(component, substream);
}
EXPORT_SYMBOL_NS(sof_client_pcm_open, SND_SOC_SOF_CLIENT);

/* disconnect pcm substream to a host stream */
int sof_client_pcm_close(struct snd_soc_component *component,
			 struct snd_pcm_substream *substream)
{
	return sof_pcm_close(component, substream);
}
EXPORT_SYMBOL_NS(sof_client_pcm_close, SND_SOC_SOF_CLIENT);

/* host stream hw params */
int sof_client_pcm_hw_params(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	return sof_pcm_hw_params(component, substream, params);
}
EXPORT_SYMBOL_NS(sof_client_pcm_hw_params, SND_SOC_SOF_CLIENT);

int sof_client_pcm_prepare(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream)
{
	return sof_pcm_prepare(component, substream);
}
EXPORT_SYMBOL_NS(sof_client_pcm_prepare, SND_SOC_SOF_CLIENT);

/* host stream hw free */
int sof_client_pcm_hw_free(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream)
{
	return sof_pcm_hw_free(component, substream);
}
EXPORT_SYMBOL_NS(sof_client_pcm_hw_free, SND_SOC_SOF_CLIENT);

/* host stream trigger */
int sof_client_pcm_trigger(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream, int cmd)
{
	return sof_pcm_trigger(component, substream, cmd);
}
EXPORT_SYMBOL_NS(sof_client_pcm_trigger, SND_SOC_SOF_CLIENT);

/* host stream pointer */
snd_pcm_uframes_t
sof_client_pcm_pointer(struct snd_soc_component *component,
		       struct snd_pcm_substream *substream)
{
	return sof_pcm_pointer(component, substream);
}
EXPORT_SYMBOL_NS(sof_client_pcm_pointer, SND_SOC_SOF_CLIENT);


int sof_client_pcm_probe(struct snd_soc_component *component)
{
	return sof_pcm_probe(component);
}
EXPORT_SYMBOL_NS(sof_client_pcm_probe, SND_SOC_SOF_CLIENT);

void sof_client_pcm_remove(struct snd_soc_component *component)
{
	sof_pcm_remove(component);
}
EXPORT_SYMBOL_NS(sof_client_pcm_remove, SND_SOC_SOF_CLIENT);

int sof_client_pcm_new(struct snd_soc_component *component,
		       struct snd_soc_pcm_runtime *rtd)
{
	return sof_pcm_new(component, rtd);
}
EXPORT_SYMBOL_NS(sof_client_pcm_new, SND_SOC_SOF_CLIENT);

int sof_client_pcm_dai_link_fixup(struct snd_soc_pcm_runtime *rtd,
				  struct snd_pcm_hw_params *params)
{
	return sof_pcm_dai_link_fixup(rtd, params);
}
EXPORT_SYMBOL_NS(sof_client_pcm_dai_link_fixup, SND_SOC_SOF_CLIENT);

struct dentry *sof_client_get_debugfs_root(struct sof_client_dev *cdev)
{
	return cdev->sdev->debugfs_root;
}
EXPORT_SYMBOL_NS_GPL(sof_client_get_debugfs_root, SND_SOC_SOF_CLIENT);

struct snd_soc_dai_driver *sof_client_get_dai_drv(struct sof_client_dev *cdev)
{
	struct snd_sof_dev *sdev = cdev->sdev;

	return sof_ops(sdev)->drv;
}
EXPORT_SYMBOL_NS_GPL(sof_client_get_dai_drv, SND_SOC_SOF_CLIENT);

int sof_client_get_num_dai_drv(struct sof_client_dev *cdev)
{
	struct snd_sof_dev *sdev = cdev->sdev;

	return sof_ops(sdev)->num_drv;
}
EXPORT_SYMBOL_NS_GPL(sof_client_get_num_dai_drv, SND_SOC_SOF_CLIENT);

MODULE_AUTHOR("Ranjani Sridharan <ranjani.sridharan@linux.intel.com>");
MODULE_LICENSE("GPL v2");
