===============================
Virtual Bus Devices and Drivers
===============================

See <linux/virtual_bus.h> for the models for virtbus_device and virtbus_driver.

This bus is meant to be a minimalist software-based bus used for splitting the
functionality of a core PCI device into orthogonal parts. The virtual devices
share a common parent PCI device and establish a shared communication channel
with the parent. MFD is not suitable for this purpose because it utilizes the
platform bus to create and attach the individual function devices and it is not
recommended to abuse the platform bus in the PCI subsystem.

Virtual Bus Devices
~~~~~~~~~~~~~~~~~~~

Virtual bus devices are meant to be devices that represent a part of the core
PCI device's functionality. The virtual bus devices attached to the virtual bus
may be from multiple subsystems and the only thing they may have in common is
the shared parent PCI device. Virtual bus devices are given a match_name that
is used for driver binding and a release callback that is invoked when the
device is unregistered.

.. code-block:: c

	struct virtbus_device {
		struct device dev;
		const char *match_name;
		void (*release)(struct virtbus_device *);
		u32 id;
	};

Virtual Bus Drivers
~~~~~~~~~~~~~~~~~~~

Virtual bus drivers follow the standard driver model convention, where
discovery/enumeration is handled outside the drivers, and drivers
provide probe() and remove() methods. They support power management
and shutdown notifications using the standard conventions.

.. code-block:: c

	struct virtbus_driver {
		int (*probe)(struct virtbus_device *);
		int (*remove)(struct virtbus_device *);
		void (*shutdown)(struct virtbus_device *);
		int (*suspend)(struct virtbus_device *, pm_message_t);
		int (*resume)(struct virtbus_device *);
		struct device_driver driver;
		const struct virtbus_device_id *id_table;
	};

Virtual bus drivers register themselves by calling virtbus_register_driver().

Device Enumeration
~~~~~~~~~~~~~~~~~~

The virtbus device is enumerated when it is attached to the bus. The device
is assigned a unique ID automatically that will be appended to its name making
it unique.  If two virtbus_devices both named "foo" are registered onto the
bus, they will have a sub-device names of "foo.x" and "foo.y" where x and y are
unique integers.

Driving Binding
~~~~~~~~~~~~~~~

When a virtual bus device is registered, the driver core iterates through the
drivers in the virtual bus to identify a match between the device's match_name
and the names in the driver's id_table. Once a successful match is found, the
driver's probe is invoked binding the device to the driver upon success.

Example Usage
~~~~~~~~~~~~~

Virtual bus devices represent a part of a multi-function PCI device. Therefore,
they are typically encapsulated within a structure defined by
parent device which contains the virtual bus device and any associated shared
data/functions needed to establish the connection with the parent.

.. code-block:: c

        struct foo {
                struct virtbus_device vdev;
                void *data;
        };

The parent PCI device would then register the virtual bus device by calling
virtbus_register_device() with the pointer to the virtual device.

Similarly, the virtual bus drivers can also be encapsulated by custom drivers
that extend the virtual bus drivers by adding additional ops that are needed
for the parent device to communicate with the child virtual bus devices.

An example of this usage would be:

.. code-block:: c

	struct custom_driver custom_drv = {
		.virtbus_drv = {
			.driver = {
				.name = "myvirtbusdrv",
			},
			.id_table = custom_virtbus_id_table,
			.probe = custom_probe,
			.remove = custom_remove,
			.shutdown = custom_shutdown,
		},
		.ops = custom_ops,
	};
