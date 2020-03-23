// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// Copyright(c) 2020 Intel Corporation. All rights reserved.
//
// Author: Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
//

#include <linux/completion.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/virtual_bus.h>
#include "sof-client.h"

#define SOF_IPC_CLIENT_SUSPEND_DELAY_MS 3000

/* TODO: dummy probe/remove callbacks for now */
static int sof_ipc_test_probe(struct virtbus_device *vdev)
{
	struct sof_client_dev *cdev = virtbus_dev_to_sof_client_dev(vdev);

	/* enable runtime PM */
	pm_runtime_set_autosuspend_delay(&vdev->dev,
					 SOF_IPC_CLIENT_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(&vdev->dev);
	pm_runtime_enable(&vdev->dev);
	pm_runtime_mark_last_busy(&vdev->dev);
	pm_runtime_put_autosuspend(&vdev->dev);

	/* complete client device registration */
	complete(&cdev->probe_complete);

	return 0;
}

static int sof_ipc_test_remove(struct virtbus_device *vdev)
{
	pm_runtime_disable(&vdev->dev);

	return 0;
}

static void sof_ipc_test_shutdown(struct virtbus_device *vdev)
{
	pm_runtime_disable(&vdev->dev);
}

static const struct virtbus_dev_id sof_ipc_virtbus_id_table[] = {
	{"sof-ipc-test", 0},
	{},
};

static struct sof_client_drv sof_ipc_test_client_drv = {
	.name = "sof-ipc-test-client-drv",
	.type = SOF_CLIENT_IPC,
	.virtbus_drv = {
		.driver = {
			.name = "sof-ipc-test-virtbus-drv",
		},
		.id_table = sof_ipc_virtbus_id_table,
		.probe = sof_ipc_test_probe,
		.remove = sof_ipc_test_remove,
		.shutdown = sof_ipc_test_shutdown,
	},
};

module_sof_client_driver(sof_ipc_test_client_drv);

MODULE_DESCRIPTION("SOF IPC Test Client Driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(SND_SOC_SOF_CLIENT);
MODULE_ALIAS("virtbus:sof-ipc-test");
