# S&P HAT Driver Module
This repository contains the source code to the parport_sphat driver which
supports Elesar's _Raspberry Pi serial and parallel port HAT_ ([EA-981-8](http://shop.elesar.co.uk/index.php?route=product/product&product_id=76))
under Linux.

Precompiled versions of this code can be downloaded by running the
script which is held in the HAT's on board EEPROM:

    bash /proc/device-tree/hat/custom_0

If you want to make changes to the driver or build your own module for a
kernel version not supported by Elesar then you should use these sources
as a starting point. The _S&P HAT Quick Start Guide_ provided with the
hardware explains how to subsequently load the modules.

With the modules loaded, a new character device appears as

    /dev/lp0

which, with a suitable parallel printer, will accept print jobs in the usual
manner.

## Prerequisites

One convenient way to get the sources which correspond to the kernerl and
firmware which are currently running on your Pi is to use rpi-source available
from [this repository](https://github.com/RPi-Distro/rpi-source). Having downloaded that type:

    rpi-source

In turn, this downloads the appropriate snapshot of the Linux source code.

## Enabling parallel support in the kernel

Firstly, write out a default set of configuration for the particular model of Raspberry Pi you have:

    cd linux
    KERNEL=<target kernel>
    make <target chip>_defconfig

the values to type for `<target kernel>` and `<target chip>` can be looked up in the [ table of current models](https://www.raspberrypi.com/documentation/computers/linux_kernel.html#natively-build-a-kernel) plus your choice of whether the processor is running the kernel in 32 or 64 bit mode.

By default, parallel port support is not enabled on the Raspberry Pi. To enable:

    make menuconfig

When the interactive menu is displayed, enable the following 2 modules:

    Device Drivers > Parallel port support
    Character Devices > Parallel printer support

by pressing 'M' next to the respective menu item.

Note that kernels prior to [October 2022](https://github.com/raspberrypi/linux/commit/eede0a75b96c7cfc3a61a4b01dd3674e8742039f) also required the dummy selection of the AX88796
driver in order to ensure that `CONFIG_PARPORT_NOT_PC` was defined. This configuration option
governs whether the parallel port knows to call the lower level hardware code rather than assume direct access to a PC-style chip is possible. If using a kernel before this change ensure the 3 modules:

    Device Drivers > Parallel port support
    Device Drivers > Parallel port support > AX88796
    Character Devices > Parallel printer support

are selected.

## Building the modules

To build the Linux kernel do the following:

    cd linux
    make modules

When this completes change back to the directory containing these sources and type:

    make

You now have the 3 loadable kernel object files `parport.ko` and `parport_sphat.ko`
and `lp.ko`. These now need to be loaded (in that order) using the `insmod` command.
