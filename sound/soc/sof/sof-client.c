// SPDX-License-Identifier: GPL-2.0
//
// Copyright(c) 2020 Intel Corporation. All rights reserved.
//
// Author: Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
//

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/virtual_bus.h>
#include "sof-client.h"

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
	vdev->name = name;
	vdev->dev.parent = sdev->dev;
	vdev->release = sof_client_virtdev_release;

	/* register virtbus device for the client */
	ret = virtbus_register_device(vdev);
	if (ret < 0)
		return ret;

	/* make sure the probe is complete before updating client list */
	timeout = msecs_to_jiffies(SOF_CLIENT_PROBE_TIMEOUT_MS);
	time = wait_for_completion_timeout(&cdev->probe_complete, timeout);
	if (!time) {
		dev_err(sdev->dev, "error: probe of virtbus dev %s timed out\n",
			name);
		kfree(cdev);
		return -ETIMEDOUT;
	}

	/* add to list of SOF client devices */
	mutex_lock(&sdev->client_mutex);
	list_add(&cdev->list, &sdev->client_list);
	mutex_unlock(&sdev->client_mutex);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(sof_client_dev_register, SND_SOC_SOF_CLIENT);

MODULE_AUTHOR("Ranjani Sridharan <ranjani.sridharan@linux.intel.com>");
MODULE_LICENSE("GPL v2");
