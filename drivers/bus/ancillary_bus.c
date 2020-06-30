// SPDX-License-Identifier: GPL-2.0
/*
 * ancillary_bus.c - lightweight software based bus for ancillary devices
 *
 * Copyright (c) 2019-20 Intel Corporation
 *
 * Please see Documentation/driver-api/ancillary_bus.rst for
 * more information
 */

#include <linux/string.h>
#include <linux/ancillary_bus.h>
#include <linux/of_irq.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <linux/acpi.h>
#include <linux/device.h>

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Ancillary Bus");
MODULE_AUTHOR("David Ertman <david.m.ertman@intel.com>");
MODULE_AUTHOR("Kiran Patil <kiran.patil@intel.com>");

static DEFINE_IDA(ancillary_dev_ida);
#define ANCILLARY_INVALID_ID	0xFFFFFFFF

static const
struct ancillary_dev_id *ancillary_match_id(const struct ancillary_dev_id *id,
					struct ancillary_device *adev)
{
	while (id->name[0]) {
		if (!strcmp(adev->match_name, id->name))
			return id;
		id++;
	}
	return NULL;
}

static int ancillary_match(struct device *dev, struct device_driver *drv)
{
	struct ancillary_driver *adrv = to_ancillary_drv(drv);
	struct ancillary_device *adev = to_ancillary_dev(dev);

	return ancillary_match_id(adrv->id_table, adev) != NULL;
}

static int ancillary_suspend(struct device *dev, pm_message_t state)
{
	if (!dev->driver->suspend)
		return 0;

	return dev->driver->suspend(dev, state);
}

static int ancillary_resume(struct device *dev)
{
	if (!dev->driver->resume)
		return 0;

	return dev->driver->resume(dev);
}

struct bus_type ancillary_bus_type = {
	.name = "ancillary",
	.match = ancillary_match,
	.suspend = ancillary_suspend,
	.resume = ancillary_resume,
};

/**
 * ancillary_release_device - Destroy a ancillary device
 * @_dev: device to release
 */
static void ancillary_release_device(struct device *_dev)
{
	struct ancillary_device *adev = to_ancillary_dev(_dev);
	u32 ida = adev->id;

	adev->release(adev);
	if (ida != ANCILLARY_INVALID_ID)
		ida_simple_remove(&ancillary_dev_ida, ida);
}

/**
 * ancillary_register_device - add a ancillary bus device
 * @adev: ancillary bus device to add
 */
int ancillary_register_device(struct ancillary_device *adev)
{
	int ret;

	if (WARN_ON(!adev->release))
		return -EINVAL;

	/* All error paths out of this function after the device_initialize
	 * must perform a put_device() so that the .release() callback is
	 * called for an error condition.
	 */
	device_initialize(&adev->dev);

	adev->dev.bus = &ancillary_bus_type;
	adev->dev.release = ancillary_release_device;

	/* All device IDs are automatically allocated */
	ret = ida_simple_get(&ancillary_dev_ida, 0, 0, GFP_KERNEL);

	if (ret < 0) {
		adev->id = ANCILLARY_INVALID_ID;
		dev_err(&adev->dev, "get IDA idx for ancillary device failed!\n");
		goto device_add_err;
	}

	adev->id = ret;

	ret = dev_set_name(&adev->dev, "%s.%d", adev->match_name, adev->id);
	if (ret) {
		dev_err(&adev->dev, "dev_set_name failed for device\n");
		goto device_add_err;
	}

	dev_dbg(&adev->dev, "Registering ancillary device '%s'\n",
		dev_name(&adev->dev));

	ret = device_add(&adev->dev);
	if (ret)
		goto device_add_err;

	return 0;

device_add_err:
	dev_err(&adev->dev, "Add device to ancillary failed!: %d\n", ret);
	put_device(&adev->dev);

	return ret;
}
EXPORT_SYMBOL_GPL(ancillary_register_device);

static int ancillary_probe_driver(struct device *_dev)
{
	struct ancillary_driver *adrv = to_ancillary_drv(_dev->driver);
	struct ancillary_device *adev = to_ancillary_dev(_dev);
	int ret;

	ret = dev_pm_domain_attach(_dev, true);
	if (ret) {
		dev_warn(_dev, "Failed to attach to PM Domain : %d\n", ret);
		return ret;
	}

	ret = adrv->probe(adev);
	if (ret) {
		dev_err(&adev->dev, "Probe returned error\n");
		dev_pm_domain_detach(_dev, true);
	}

	return ret;
}

static int ancillary_remove_driver(struct device *_dev)
{
	struct ancillary_driver *adrv = to_ancillary_drv(_dev->driver);
	struct ancillary_device *adev = to_ancillary_dev(_dev);
	int ret = 0;

	ret = adrv->remove(adev);
	dev_pm_domain_detach(_dev, true);

	return ret;
}

static void ancillary_shutdown_driver(struct device *_dev)
{
	struct ancillary_driver *adrv = to_ancillary_drv(_dev->driver);
	struct ancillary_device *adev = to_ancillary_dev(_dev);

	adrv->shutdown(adev);
}

static int ancillary_suspend_driver(struct device *_dev, pm_message_t state)
{
	struct ancillary_driver *adrv = to_ancillary_drv(_dev->driver);
	struct ancillary_device *adev = to_ancillary_dev(_dev);

	if (!adrv->suspend)
		return 0;

	return adrv->suspend(adev, state);
}

static int ancillary_resume_driver(struct device *_dev)
{
	struct ancillary_driver *adrv = to_ancillary_drv(_dev->driver);
	struct ancillary_device *adev = to_ancillary_dev(_dev);

	if (!adrv->resume)
		return 0;

	return adrv->resume(adev);
}

/**
 * __ancillary_register_driver - register a driver for ancillary bus devices
 * @adrv: ancillary_driver structure
 * @owner: owning module/driver
 */
int __ancillary_register_driver(struct ancillary_driver *adrv, struct module *owner)
{
	if (!adrv->probe || !adrv->remove || !adrv->shutdown || !adrv->id_table)
		return -EINVAL;

	adrv->driver.owner = owner;
	adrv->driver.bus = &ancillary_bus_type;
	adrv->driver.probe = ancillary_probe_driver;
	adrv->driver.remove = ancillary_remove_driver;
	adrv->driver.shutdown = ancillary_shutdown_driver;
	adrv->driver.suspend = ancillary_suspend_driver;
	adrv->driver.resume = ancillary_resume_driver;

	return driver_register(&adrv->driver);
}
EXPORT_SYMBOL_GPL(__ancillary_register_driver);

static int __init ancillary_bus_init(void)
{
	return bus_register(&ancillary_bus_type);
}

static void __exit ancillary_bus_exit(void)
{
	bus_unregister(&ancillary_bus_type);
	ida_destroy(&ancillary_dev_ida);
}

module_init(ancillary_bus_init);
module_exit(ancillary_bus_exit);
