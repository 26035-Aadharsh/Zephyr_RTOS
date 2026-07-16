############################################
Pin Control in Zephyr RTOS | nRF Connect SDK
############################################

Pin maps and multiplexing MACROs are defined in the :

    `$(ZEPHYRPROJECT)zephyr\include\zephyr\dt-bindings\pinctrl`

directory.

.. code-block:: dts
	pinctrl: pin-controller {
		compatible = "espressif,esp32-pinctrl";
		status = "okay";
	};

The hardware blocks that control `pin multiplexing` and `pin configuration` parameters such as pin direction, pull-up/down resistors, etc. are named pin controllers. The pin controller's main users are SoC hardware peripherals, since the controller enables exposing peripheral signals, like for example, 

Map I2C0 SDA signal to pin PX0.

Not only that, but it usually allows `configuring certain pin settings` that are necessary for the correct functioning of a peripheral, for example, the slew-rate depending on the operating frequency. The available configuration options are vendor/SoC dependent and can range from simple `pull-up/down options` to more advanced settings such as debouncing, low-power modes, etc.

The way pin control is implemented in hardware is vendor/SoC specific. It is common to find a centralized approach, that is, all pin configuration parameters are controlled by a single hardware block (typically named `pinmux`), including signal mapping.

=========================================
Centralized and Decentralized Pin Control
=========================================

STM32 and nRF (Nordic Semiconductor) boards handle pin control similarly in terms of software, but they utilize two fundamentally different hardware architectures under the hood.In both cases, the pinctrl Device Tree nodes map directly to physical hardware registers, but the underlying routing design differs between a centralized switchboard and a distributed multiplexer.

-----------------------------------------------------
Centralized Multiplexing : STM32 (STMicroelectronics)
-----------------------------------------------------

The Centralized SwitchboardSTM32 microcontrollers use a highly structured, rigid, and centralized hardware block for pin routing.The Hardware RealityInside an STM32 chip, every single GPIO port (e.g., GPIOA, GPIOB) contains dedicated Alternate Function (AF) multiplexer registers.  Each pin has a hardwired multiplexer that typically allows a choice between standard GPIO or up to 16 specific "Alternate Functions" (AF0 through AF15).

It is not possible to route a peripheral to just any pin. If you want to use UART1 TX, the chip's internal layout states it can only be routed to specific physical pins (e.g., PA9 or PB6). The Software Representation (st,stm32-pinctrl)When you define an STM32 pinctrl node, your code passes an index telling the physical hardware exactly which mux lane to open:

.. code-block:: dts
    /* Example of what happens under the hood for STM32 */
    &pinctrl {
        uart1_pins: uart1_default {
            pinmux = <STM32_PINMUX('A', 9, AF7)>;
            /* Physical Pin PA9, set to Mux Lane 7 */
        };
    };

The driver takes that AF7 value and physically writes it into the `GPIOA->AFR` (Alternate Function Register) hardware memory address

--------------------------------------------------------------
Decentralized Multiplexing : nRF Boards (Nordic Semiconductor)
--------------------------------------------------------------

Nordic chips (like the nRF52840 or nRF5340) handle pin routing in a completely opposite way. They are famous for having an incredibly flexible, fully distributed crossbar switch.

Nordic microcontrollers do not force peripherals onto specific pins. Instead, the multiplexer is built directly into the peripheral itself, rather than a central GPIO block.If you activate the UARTE0 peripheral hardware module, that module has its own physical registers called PSEL.TXD and PSEL.RXD (Pin Selection registers).

You can write almost any physical pin number into `UARTE0->PSEL.TXD`, and the hardware will dynamically connect the internal UART transmitter to that physical pad. Because the hardware is completely flexible, the nRF pinctrl snippet uses a macro that directly maps the peripheral function to any arbitrary port/pin.

.. code-block:: dts
    /* Example of what happens under the hood for nRF */
    &pinctrl {
        uart0_default: uart0_default {
            group1 {
                psels = <NRF_PSEL(UART_TX, 0, 6)>; /* Route UART_TX to Port 0, Pin 6 */
            };
        };
    };

When this code runs, the driver locates the physical memory map for the UART0 peripheral and writes the integer representing Pin 6 into its hardware PSEL register.

.. image:: zephyr/apps/_docs/Pin Control/Images/Centralized_PinCtrl.png
   :alt: PX0 can be mapped to UART0_TX, I2C0_SCK or SPI0_MOSI depending on the AF control bits. Other configuration parameters such as pull-up/down are controlled in the same block via CONFIG bits. This model is used by several SoC families, such as many from NXP and STM32.
   :scale: 100
   :align: center

.. image:: zephyr/apps/_docs/Pin Control/Images/Distributed_PinCtrl.png
   :alt: Other vendors/SoCs use a distributed approach. In such case, the pin mapping and configuration are controlled by multiple hardware blocks. The figure below illustrates a distributed approach where pin mapping is controlled by peripherals, such as in Nordic nRF SoCs.
   :scale: 100
   :align: center

====================
Notes and References
====================

1. Default states in Zephyr (as of 12 Jul 2026)
.. code-block:: C
    /*
    PINCTRL_STATE_DEFAULT
    PINCTRL_STATE_SLEEP
    */

    #define PINCTRL_STATE_DEFAULT   0U
    #define PINCTRL_STATE_SLEEP     1U

2. Custom states can be created in Zephyr

3. Pin Control states can be skipped if needed based on Kconfig and other conditionals:
.. code-block:: C
    #if !defined(CONFIG_PM) && !defined(CONFIG_PM_DEVICE)
    /** Out of power management configurations, ignore "sleep" state. */
    #define PINCTRL_SKIP_SLEEP 1
    #endif

4. Dynamic Pin Control

Dynamic pin control refers to the capability of changing pin configuration at runtime. This feature can be useful in situations where the same firmware needs to run onto slightly different boards, each having a peripheral routed at a different set of pins. This feature can be enabled by setting `CONFIG_PINCTRL_DYNAMIC`.

One of the effects of enabling dynamic pin control is that `pinctrl_dev_config` will be stored in RAM instead of ROM (not states or pin configurations, though). The user can then use `pinctrl_update_states()` to update the states stored in pinctrl_dev_config with a new set. This effectively means that the device driver will apply the pin configurations stored in the updated states when it applies a state.

**Note** << Advanced Users >>: Dynamic pin control should only be used on devices that have not been initialized. Changing pin configurations while a device is operating may lead to unexpected behavior. Since Zephyr does not support device de-initialization yet, this functionality should only be used during early boot stages.

============================
Pin Configurations in Zephyr
============================

.. code-block:: dts
    /* board-pinctrl.dtsi */
    #include <vnd-soc-pkgxx.h>

    &pinctrl {
        /* Node with pin configuration for default state */
        periph0_default: periph0_default {
            group1 {
                /* Mappings: PERIPH0_SIGA -> PX0, PERIPH0_SIGC -> PZ1 */
                pinmux = <PERIPH0_SIGA_PX0>, <PERIPH0_SIGC_PZ1>;
                /* Pins PX0 and PZ1 have pull-up enabled */
                bias-pull-up;
            };
            ...
            groupN {
                /* Mappings: PERIPH0_SIGB -> PY7 */
                pinmux = <PERIPH0_SIGB_PY7>;
            };
        };
    };

=========================================================================
Vendor's Auto-Generated SoC Pin Multiplexing `.dtsi` File Model in Zephyr
=========================================================================

This "node-per-pin" Devicetree model exists to accommodate automated configuration tools provided by silicon vendors (like STMicroelectronics or NXP), which generate massive matrices of every possible pin combination for a chip.

While too verbose to write manually, you must understand it because you will encounter it in vendor files, where the `/omit-if-no-ref/`compiler optimization is used to automatically purge any unused pin nodes and save flash memory. Ultimately, this architecture teaches you to keep baseline pin mappings separate from electrical characteristics (like pull-ups), ensuring you only apply board-specific electrical properties in your application files to prevent hidden bugs and bus distortion.

=============================
How to Implement Pin Control?
=============================

===============
Reference Links
===============

Introduction to Pin Control in nRF SDK or Zephyr : 
https://nrfconnectdocs.nordicsemi.com/ncs/latest/zephyr/hardware/pinctrl/index.html

