Types of Pin Ctrl
1. Distributed nRF
2. Centralized STM32, NXP

Pin Ctrl and GPIO

Some functionality covered by a pin controller driver overlaps with GPIO drivers. For example, pull-up/down resistors can usually be enabled by both the pin control driver and the GPIO driver. In Zephyr context, the pin control driver purpose is to perform peripheral signal multiplexing and configuration of other pin parameters required for the correct operation of that peripheral. Therefore, the main users of the pin control driver are SoC peripherals. In contrast, GPIO drivers are for general purpose control of a pin, that is, when its logic level is read or controlled manually.

Standard States
1. default
2. sleep

It is Possible to create custom states

n most situations, the states defined in Devicetree will be the ones used in the compiled firmware. However, there are some cases where certain states will be conditionally used depending on a compilation flag. A typical case is the sleep state. This state is only used in practice if CONFIG_PM or CONFIG_PM_DEVICE is enabled.

States can be Otionally excluded to save ROM Space

Dynamic pin ctrl

The main reason to use dynamic pin control is hardware abstraction for multi-board firmware.Instead of compiling separate firmware binaries for "Board Revision A" and "Board Revision B" (which might use different physical pins for the status LED or the main UART line), you compile a single binary. At early boot, the code detects the board version (via a GPIO strap or EEPROM value) and dynamically overrides the pin map to match that specific hardware layout.

Not used if a fixed board is used

How to Apply Pin Configs?