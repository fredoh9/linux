===============================
Ancillary Bus Devices and Drivers
===============================

See <linux/ancillary_bus.h> for the models for ancillary_device and
ancillary_driver.

This bus is meant to be a minimalist software-based bus used for
connecting devices (that may not physically exist) to be able to
communicate with each other.


Memory Allocation Lifespan and Model
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The host object that will register the ancillary_device or ancillary_driver
will need allocate the memory for them before registering on the ancillary bus.

The lifespan of the memory for an ancillary_device will be until the
ancillary_device's mandatory .release() callback is invoked when the device
is unregistered by calling ancillary_unregister_device().  The memory will
be freed in this callback.

The lifespan of the memory associated with a ancillary_driver will be at
leaat until the driver's .remove() or .shutdown() callbacks are invoked. 

Device Enumeration
~~~~~~~~~~~~~~~~~~

The ancillary device is enumerated when it is attached to the bus. The
device is assigned a unique ID that will be appended to its name
making it unique.  If two ancillary_devices both named "foo" are
registered onto the bus, they will have a sub-device names of "foo.x"
and "foo.y" where x and y are unique integers.

Common Usage and Structure Design
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The ancillary_device and ancillary_driver need to have a common header
file.

In the common header file outside of the ancillary_bus infrastructure,
define struct ancillary_object:

.. code-block:: c

        struct ancillary_object {
                ancillary_device vdev;
                struct my_private_struct *my_stuff;
        }

When the ancillary_device vdev is passed to the ancillary_driver's probe
callback, it can then get access to the struct my_stuff.

An example of the driver encapsulation:

.. code-block:: c

	struct custom_driver {
		struct ancillary_driver ancillary_drv;
		const struct custom_driver_ops ops;
	}

An example of this usage would be :

.. code-block:: c

	struct custom_driver custom_drv = {
		.ancillary_drv = {
			.driver = {
				.name = "sof-ipc-test-ancillary-drv",
			},
			.id_table = custom_ancillary_id_table,
			.probe = custom_probe,
			.remove = custom_remove,
			.shutdown = custom_shutdown,
		},
		.ops = custom_ops,
	};

Mandatory Elements
~~~~~~~~~~~~~~~~~~

ancillary_device:

- .release() callback must not be NULL and is expected to perform memory cleanup.
- .match_name must be populated to be able to match with a driver

ancillary_driver:

- .probe() callback must not be NULL
- .remove() callback must not be NULL
- .shutdown() callback must not be NULL
- .id_table must not be NULL, used to perform matching

