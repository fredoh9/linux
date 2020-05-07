===============================
Virtual Bus Devices and Drivers
===============================

See <linux/virtual_bus.h> for the models for virtbus_device and virtbus_driver.
This bus is meant to be a minimalist software based bus to attach generic
devices and drivers to so that a chunk of data can be passed between them.

Memory Allocation Lifespan and Model
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Originating KO - The kernel object that is defining either the
virtbus_device or the virtbus_driver and registering it with the virtual_bus.

The originating KO is expected to allocate memory for the virtbus_device or
virtbus_driver before registering it with the virtual_bus.

For a virtbus_device, this memory is expected to remain viable until the
device's mandatory .release() callback is accessed.  The memory cleanup will
ideally be performed within this callback.

For a virtbus_driver, this memory is expected to remain viable until the
driver's .remove() or .shutdown() callbacks are accessed.  The memory cleanup
will ideally be performed with these callbacks.

Device Enumeration
~~~~~~~~~~~~~~~~~~

Enumeration is handled automatically by the bus infrastructure.

Device naming and driver binding
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The virtbus_device.dev.name is the canonical name for the device. It is
built from two other parts:

        - virtbus_device.match_name (used for matching).
        - virtbus_device.id (generated automatically from ida_simple calls)

Virtbus device's sub-device names are always in "<name>.<instance>" format.

For a virtbus_device to be matched with a virtbus_driver, the device's match
name must be populated, as this is what will be evaluated to perform the match.

Common Usage and Structure Design
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The two (or more) Originating KO's (one for the driver and one or more for
the devices) will have to have a common header file, so that a struct can
be defined that they both use.  This allows for the following design.

In the common header file outside of the virtual_bus infrastructure, define
struct virtbus_object:

                 virtbus_object
                        |
                -------------------------
                |                       |
        virtbus_device          private_struct/data

The virtbus_device cannot be a pointer so that a container_of can be
performed by the driver.

The device originating KO will allocate a virtbus_object and then populate
virtbus_device->match_name and register the virtbus_device with virtbus.

When the virtbus_driver registers with the virtual_bus and a match is made,
the .probe() callback of the driver will be called with the virtbus_device
struct as a parameter.  This allows the virtbus_driver to perform a
contain_of call to get access to the virtbus_object, and to the pointer to
the private_struct/data.

This is the main goal of virtual_bus, to get a common pointer available
to both of the originating KOs involved in a match.

Mandatory Elements
~~~~~~~~~~~~~~~~~

virtbus_device:
        .relase() callback must not be NULL and is expected to perform
                memory cleanup.
        .match_name must be populated to be able to match with a driver

virtbus_driver:
        .probe() callback must not be NULL
        .remove() callback must not be NULL
        .shutdown() callback must not be NULL
        .id_table must not be NULL, used to perform matching
