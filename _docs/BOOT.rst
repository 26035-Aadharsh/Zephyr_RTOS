======================================================================
Architectural Deep Dive: ESP32 DevKit-C and Zephyr RTOS Board Bring-Up
======================================================================

When an embedded microcontroller initializes from a completely unpowered state (Cold Boot) or recovers via an electrical pulse on its external Reset pin (Warm Boot), the central processing unit (CPU) cannot immediately execute high-level software.

Silicon hardware operates deterministically: the program counter (PC) must map to a physical hardware address pre-programmed into non-volatile, read-only memory (ROM) during chip fabrication. This early silicon-level configuration environment handles the foundational validation of the hardware before handing execution over to user-modifiable software.

#################
MCU Boot Sequence
#################

1. Load Program Counter and Stack Pointer deterministically
2. The Program Counter addressess a 1st Stage Bootloader that initializes hardware

============================================
Notes on the ESP32 WROOM DA Module Processor
============================================

Underneath the protective silver metal shield of an ESP32 module lies a compact System-in-Package consisting of the actual silicon microprocessor die, a separate external flash memory chip interfaced to the microprocessor by a slow speed SPI communication protocol, a quartz crystal oscillator clock, and stabilizing passive components, all enclosed within this Faraday cage to prevent the module's 2.4 GHz Wi-Fi and Bluetooth radio emissions from interfering with external electronics while simultaneously shielding the sensitive internal communication lines from external electromagnetic noise.

Because the main microprocessor lacks high-capacity internal flash storage, it must communicate with this adjacent external flash chip inside the shield to run its operating system, a architectural constraint that makes its boot process significantly more complex than a standard ARM Cortex-M processor.

-----------------
Start Up Sequence
-----------------

`Modern MCUs use Flash Memory to Store Firmware, but the Tradition Stuck`. The Flash Memory is used interchangably with ROM.

2 Common Start-Up Mechanisms

 1. Hardwired Start Address : In many architectures such as the Arduino's AVR and 8051, the PC is set to a fixed address after reset.

 2. Vector Table | Stored Reset Handler : More advanced MCUs such as the ARM Cortex M in the STM32 and ESP32 WROOM DA Module in the ESP32 load the initial PC from memory, not a fixed literal.

ARM MCUs Example
+-------------+---------------------------+
| 0x0000_0000 |   Progrem Counter Address |
| 0x0000_0004 |   Stack Pointer Address   |
+-------------+---------------------------+

The more modern MCUs thus allow flexible initialization of PCs and allow execution of instructions stored at any memory address after MCU boot.

-----------------------
ESP32 ROM Boot Sequence
-----------------------

The ESP32 is a dual-core **Xtensa LX6 microprocessor**. Upon releasing the internal reset line, the secondary core (App CPU or Core 1) is immediately placed into a low-power stall mode by hardware gating.

The primary core (Protocol CPU, Pro CPU, or Core 0) becomes active, sets its Program Counter to the hardware reset vector address 0x40000400, and executes the hardcoded **First-Stage Bootloader** residing entirely within the internal 448 KB ROM.

The primary roles of the ROM-based boot engine include:

1. **Basic Hardware Initialization:** The internal ROM code configures basic CPU clock frequencies, maps parts of the internal Static RAM (SRAM) memory, and initializes fundamental peripheral access.
   SRAM is extremey fast and does not require DRAM initialization routines that require complex logic and large memory to execute, it is executed by the Primary Boot, and is later used for complex code executions.
2. **Strapping Pin Evaluation:** The hardware samples physical pins to determine how the chip should proceed.
   In this step, the primary bootloader samples the BOOT pin's configuration and decides if it must read code over UART or execute code from flash memory.
**Primary Selection Workflow:** The bootloader decides whether to parse the physical external flash memory that holds the next stages of firmware code or drop into a serial download console.

BOOT Boot Strapping Pins and Boot Source Selection Logic in ESP32
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ESP32 samples five specific General Purpose Input/Output (GPIO) pins during the rising edge of the chip-enable | reset signal (CHIP_PU). These are known as the **Strapping Pins**:

GPIO0, GPIO2, GPIO5, GPIO12, and GPIO15.

`sample boot strapping pin voltages to enter corresponding boot mode`

The voltage levels (Logic High vs. Logic Low) on these pins are latched into internal hardware registers (GPIO_STRAPPING_REG) during boot-time. Peripherals and devices must not be connected to these GPIOs during BOOT to ensure reliable booting.

The combination of GPIO0 and GPIO2 controls the target operational boot mode:

`Flash Larger Code on ESP32 by Leveraging a SPI based Larger External Flash`
 * SPI Flash Boot Mode (Normal Execution): GPIO0 must be pulled High (via an internal or external pull-up resistor) and GPIO2 must be pulled Low. In this configuration, the ROM engine assumes that a valid partition table and second-stage bootloader reside on an external chip connected via the Serial Peripheral Interface (SPI).

 * UART Download Mode (Flashing Engine): GPIO0 is held Low while GPIO2 is held Low. The ROM bypasses flash execution and configures internal UART0 controllers to listen for incoming binary chunks from a host PC (using the esptool.py protocol), loading incoming binaries directly into internal RAM for flashing. This is the default flash procedure for ESP32 based boards.

+-----------+-----------+---------------------------------------+
|   GPIO0   |   GPIO2   |   Boot Mode                           |
|   Low     |   Low     |   UART Flash Firware Download         |
|   High    |   Low     |   SPI Flash Boot Mode for External    |
+-----------+-----------+---------------------------------------+

If GPIOS 0 and 2 are High, MCU executes firmware code as usual, else the flash fails and esptool or corresponding flashing tool raises an error.

The other strapping pins alter hardware details: GPIO5 and GPIO15 dictate the timing and signal characteristics of the external flash interface (e.g., SDIO vs. SPI configurations), while GPIO12 alters the operational voltage supply (1.8V or 3.3V) for the flash memory lines.

-----------------------
Second-Stage Bootloader
-----------------------

External flash memory cannot execute code out-of-the-box via native pointers because the physical SPI lines require transaction commands to extract bytes. The boot code includes a minimal SPI driver capable of mapping a small portion of external flash into the CPU's Instruction RAM (IRAM) address space.

This brings us to the **Second-Stage Bootloader** (often derived from the ESP-IDF framework and wrapped inside the Zephyr build infrastructure, so the application developer does not have to re-invent the second-stage bootloader). The boot engine copies the second-stage bootloader binary from physical flash address 0x0000 1000 into the internal SRAM regions (0x4008 0000), as the external flash is extremely slow.

Once completed, Core 0 jumps out of flash space and transfers execution directly to this second-stage software bootloader. The second-stage bootloader is responsible for establishing a stable operational environment. Its execution follows a strict sequence:

 1. **Read the Partition Table:** It accesses a fixed flash address (typically 0x0000 8000) to parse the system layout.
 2. **Locate the Application Partition:** It determines the exact starting offset of the active Zephyr RTOS application image.
 3. **Parse Image Headers:** The application binary begins with a standardized Espressif header containing information about code segments, target load addresses in IRAM/DRAM, and validation metadata.
 4. **Verify Integrity and Authenticity:** The bootloader computes a SHA-256 checksum over the entire application image and compares it against a hash appended to the end of the binary. If secure boot features are active, it verifies RSA or ECDSA digital signatures using public keys stored in physical eFuses.
 5. **Load to RAM:** Once validated, the bootloader uses hardware memory mapping registers (MMU) to load the `text` and `data segments` into their respective target RAM zones. It then configures flash cache parameters so the CPU can execute additional application instructions directly out of external flash through cache hits.
 6. **Handover Control:** Core 0 jumps directly to the entry address defined in the application image header, transferring control to the *Zephyr RTOS* startup code.

-------------------
Flash Memory Layout
-------------------

The ESP32 uses an external flash chip, which typically provides 4 MB of space on standard ESP32 DevKit-C boards. This memory is divided into distinct structural zones managed via the partition table. Below is the standard memory map layout generated when compiling a Zephyr RTOS image for this hardware target:

+------------------+-------------------------------------------------------+
| Physical Address | Partition / Section Functionality                     |
+------------------+-------------------------------------------------------+
| 0x0000_0000      | Reserved / Primary Espressif Vector Tables            |
| 0x0000_1000      | Second-Stage Bootloader Binary Target Area            |
| 0x0000_8000      | Partition Table (System Geography Database)           |
| 0x0000_9000      | NVS Storage / Non-Volatile Data Subsystem             |
| 0x0001_0000      | Zephyr Application Slot 0 (Primary Boot Target)       |
| 0x0020_0000      | Zephyr Application Slot 1 (Secondary OTA Backup Slot) |
| 0x003F_0000      | Core Dump Storage Region / Factory Diagnostics Zone   |
+------------------+-------------------------------------------------------+

######################################
Flash Layout Mapping to Address Spaces
######################################

The ESP32 relies on an internal Memory Management Unit (MMU) to overcome physical memory limitations. The Xtensa CPU uses a 32-bit linear address space (4 GB). Since physical internal SRAM is limited to 520 KB, external flash memory must be mapped into this address space in 64 KB chunks called pages.

When the bootloader or Zephyr configures the MMU, the flash sections are divided based on access type:

 * **Instruction Cache (ICache):** Code executed directly from flash is mapped into the 0x40000000 - 0x44000000 virtual memory range. When the CPU encounters an instruction pointer in this region, it fetches the data through an internal cache line connected to the external SPI flash.

 * **Data Cache (DCache):** Read-only constants, string literals, and look-up tables (.rodata) are mapped into the 0x3F400000-0x3F800000 virtual memory space. This allows standard C data pointers to read flash data without using explicit SPI transaction functions.

---------------------------------
Zephyr Binary Placement Mechanics
---------------------------------

During compilation, Zephyr uses a linker script customized for the ESP32 (linker.ld). The build pipeline strips the absolute Executable and Linkable Format (zephyr.elf) file down into a format that fits the ESP32 memory controller.

The linker segregates code blocks using explicit section attributes:

 * **.iram1.text:** Critical interrupt vectors, low-level cache management code, and real-time ISR routines are placed here. They are loaded directly into internal SRAM to ensure zero-wait-state execution, bypassing the flash cache completely.

 * **.flash.text:** The majority of application logic and higher-level driver libraries reside here. This code remains in external flash and is accessed on-demand via the Instruction Cache.
  
=======================
Zephyr Startup Sequence
=======================

----------------------------------------
Transfer of Control and The Reset Vector
----------------------------------------

When the second-stage bootloader completes its validation tasks, it performs an absolute assembly jump to the application entry point. This entry point matches the CPU Reset Vector address assigned inside the application image header. In Zephyr, this entry point routes directly to an architectural initialization file written in Xtensa assembly language, typically named crt1-wrenv.S or window_vectors.S.

----------------------------------------------
Low-Level Assembly and Register Initialization
----------------------------------------------

The Xtensa LX6 architecture uses a register window rotating mechanism designed to accelerate function calls by reducing stack push-and-pop actions. Upon entry, the assembly code performs several critical tasks:

 1. **Disable Interrupts:** It writes to the Special Register INTENABLE to clear all pending maskable interrupts. This ensures that no peripheral interrupt can pre-empt the processor while the runtime stack is uninitialized.

 2. **Initialize Windows and Stack Pointer:** It configures the Window Base (WINDOWBASE) and Window Start (WINDOWSTART) registers, reset-clears the internal general-purpose working registers (a0 through a15), and loads the primary stack pointer (a1) with the memory address of the boot-time stack. This stack area is statically reserved within the internal Data RAM (DRAM) section of the chip.

-----------------------------------
Data, .BSS, and Vector Table Setups
-----------------------------------

With a valid stack established, the assembly routine prepares the C execution environment:

 * **The BSS Section Initialization:** It loops through the Block Started by Symbol (.bss) memory bounds and zeroes out every byte. This guarantees that all uninitialized global and static C variables are set to zero before code execution begins.

 * **The Data Section Initialization:** It copies initialized global variables from their read-only storage locations in flash over to their active runtime execution zones in internal DRAM (.data section relocation).

 * **Vector Table Configuration:** The code writes the base address of the exception vector table to the Special Register VECBASE. This table defines the branch entry targets for hardware errors, system calls, and peripheral interrupts.

Once these environments are configured, the assembly loop exits by calling _cstart(), a generic C entry function inside the Zephyr kernel pipeline.

-----------------------
Hardware Initialization
-----------------------

1. Clock Tree and Central Processor Provisioning
 The _cstart() function calls low-level hardware initialization code before starting the kernel scheduler. The architecture initialization layer triggers the clock controller subsystem to transition the ESP32 from its internal 8\text{ MHz} oscillator to its external crystal oscillator (typically 40\text{ MHz}). It then activates the internal Phase-Locked Loop (PLL) multiplier to drive the main CPU cores at their rated speeds of 160\text{ MHz} or 240\text{ MHz}.
2. Memory Map System Locking
 During this window, the internal Memory Management Unit (MMU) is locked into its final application state. Flash caching structures are fully initialized, and the heap layout is defined within the leftover gaps of internal DRAM. This allows the system allocator to handle dynamic allocation demands within supervisor space.
3. Devicetree Processing Model
 In Zephyr, hardware configuration details are parsed at compile time rather than runtime. The system does not scan an active hardware bus to discover peripherals; instead, the system architecture is described via a structured **Devicetree** description file (.dts).
 The Zephyr build engine uses Python scripts to parse these Devicetree source files and combine them with YAML hardware bindings. This pipeline transforms text-based hardware descriptions into a generated header file named devicetree_generated.h.
 When the C compiler builds the kernel, it evaluates these generated preprocessor macros to instantiate data structures that contain the physical base registers, interrupt lines, and pin assignments for each peripheral. Consequently, if a peripheral is marked as status = "disabled"; in the Devicetree, its initialization code is omitted from the compiled binary.

`STM32 and other MCUs based on the ARM Platfrom greatly reduce this software complexity as most initialization is performed on hardware.`

=====================
Kernel Initialization
=====================

Once the core hardware configuration is established, Zephyr initializes its internal components using a multi-tier startup process. This architecture prevents dependency loops during system boot-up.

[ Assembly Init ] ---> [ PRE_KERNEL_1 ] ---> [ PRE_KERNEL_2 ] ---> [ POST_KERNEL ] ---> [ APPLICATION ]

The system steps through four sequential initialization tiers, which are ordered using specialized linker sections:

1. PRE_KERNEL_1
 * **Characteristics:** Executed extremely early in the boot sequence. At this stage, the kernel scheduler is completely inactive, and kernel primitives like mutexes, semaphores, or system timers are unavailable.
 * **Components Initialized:** Low-level hardware drivers that do not require kernel services. Examples include basic pin multiplexing controllers (pinctrl), system clock management chips, and early serial console components (UART) used to output debug information.
2. PRE_KERNEL_2
 * **Characteristics:** Executed immediately after PRE_KERNEL_1. The scheduler is still offline, but foundational memory structures are available.
 * **Components Initialized:** Complex hardware subsystems that depend on basic drivers but do not require thread-level synchronization. This includes power management units, internal DMA controllers, and shared communication buses (such as I2C or SPI bus orchestrators).
3. POST_KERNEL
 * **Characteristics:** At this threshold, the kernel has initialized its core tracking engines. Thread definitions, timing structures, and synchronization mechanisms are fully functional, though execution remains confined to a single boot thread context.
 * **Components Initialized:** Drivers and high-level abstract subsystems that require kernel primitives to operate safely. This includes sensor drivers that block threads while waiting for an I2C transaction to complete, network stack interfaces, and external flash storage controllers.
4. APPLICATION
 * **Characteristics:** Executed after the primary kernel structures are operational.
 * **Components Initialized:** High-level software components, third-party middleware packages, and custom logic layers that require a fully functional runtime environment.

----------------------------------------
Macro Driver Initialization Linker Table
----------------------------------------

Zephyr enforces this initialization order using compile-time structural registration macros, such as DEVICE_DT_DEFINE() and SYS_INIT().

#define DEVICE_DT_DEFINE(node_id, init_fn, pm, data, config, level, priority, api)

When a developer compiles a driver and specifies a target level (e.g., PRE_KERNEL_1) and a relative numeric priority (from 0 to 99), the compiler does not generate an explicit startup function call tree. Instead, the macro wraps the initialization metadata into a dedicated configuration structure tagged with an execution attribute pointer:
```c
static const struct device __device_dt_id = {
    .name = DT_PROP(node_id, label),
    .init = init_fn,
    .config = config,
    .data = data,
};

```
The compiler places these registration blocks into separate sub-sections of the final ELF binary (.init_PRE_KERNEL_1, .init_PRE_KERNEL_2, etc.). During the boot sequence, the kernel runs a simple loop that iterates through these memory regions, automatically executing the initialization functions in their designated order.

=====================
Application Execution
=====================

Once the POST_KERNEL initialization layer finishes executing, the single-thread boot configuration initializes the core multitasking engine. The kernel constructs its primary system thread: the **Main Thread**.

The kernel assigns a stack space and execution priority to this main thread, then calls k_sched_start(). This switches the processor out of its single-thread boot context and enables the multi-threaded scheduling engine. The scheduler selects the highest-priority runnable thread, which is typically the Main Thread since the secondary background idle threads are initialized at the lowest possible priority.

-------------------------------------------
Thread Creation, Scheduling, and Main Entry
-------------------------------------------

The newly spawned Main Thread performs final initialization tasks, steps through the APPLICATION tier initialization functions, and then executes the user application's entry point:

.. code-block:: c
    void main(void).

If your application uses additional threads, they are typically declared statically using the `K_THREAD_DEFINE()` macro:

.. code-block:: c
    K_THREAD_DEFINE(my_worker_id, STACK_SIZE, worker_entry, NULL, NULL, NULL, 5, 0, 0);

The compiler places these thread definition blocks into a dedicated .static_threads linker section. During kernel initialization, the scheduler processes this array and places each thread into the active ready queue based on its defined priority level, enabling multitasking execution alongside the main application code.

################################
Porting Zephyr to a Custom Board
################################

When designing a custom PCB that utilizes an ESP32 chip, you cannot reuse the default ESP32 DevKit-C board profile without modifying its pin mappings and peripheral configurations. Porting Zephyr to your custom layout requires establishing a structured board directory within the workspace.

--------------------------------
Directory Structure Requirements
--------------------------------

A standalone out-of-tree board definition requires the following file layout:

.. code-block:: markdown
    boards/architecture/custom_esp32_board/
    ├── Kconfig.board          # Hardware enablement compile target definition
    ├── Kconfig.defconfig      # Default kernel configuration overrides
    ├── custom_esp32_board.dts # Global physical Devicetree target file
    └── board.cmake            # Flashing tooling instruction scripts

--------------------------------------------------------
Devicetree Mapping Construction (custom_esp32_board.dts)
--------------------------------------------------------

The .dts file defines your board's physical configuration by inheriting properties from the base SoC file:

.. code-block:: dts
    /dts-v1/;
    #include <espressif/esp32.dtsi>
    #include "custom_esp32_board-pinctrl.dtsi"

    / {
        model = "Custom ESP32 Deployment Module";
        compatible = "vendor,custom-esp32";

        aliases {
            led0 = &status_led;
            uart-0 = &uart0;
        };

        chosen {
            zephyr,sram = &sram0;
            zephyr,console = &uart0;
            zephyr,shell = &uart0;
        };

        leds {
            compatible = "gpio-leds";
            status_led: led_0 {
                gpios = <&gpio0 15 GPIO_ACTIVE_HIGH>;
                label = "System Runtime Status LED";
            };
        };
    };

    &uart0 {
        status = "okay";
        current-speed = <115200>;
        pinctrl-0 = <&uart0_default>;
        pinctrl-names = "default";
    };

----------------------------------------------------------------
Pin Multiplexing Configuration (custom_esp32_board-pinctrl.dtsi)
----------------------------------------------------------------

The ESP32 routes its internal hardware controllers to physical pins using an internal pin matrix. You define these pin mappings within a pinctrl Devicetree file:

.. code-block:: dts
    &pinctrl {
        uart0_default: uart0_default {
            group1 {
                pinmux = <UART0_TX_GPIO1>;
                output-enable;
            };
            group2 {
                pinmux = <UART0_RX_GPIO3>;
                input-enable;
                bias-pull-up;
            };
        };
    };

--------------------------
Kconfig Target Definitions
--------------------------

To expose your new board definition to the west build tool, you must configure its target characteristics within Kconfig.board:
.. code-block:: kconfig
    config BOARD_CUSTOM_ESP32
        bool "Custom ESP32 Production Module"
        depends on SOC_ESP32

You can then define standard feature overrides inside the Kconfig.defconfig file:

.. code-block:: kconfig
    if BOARD_CUSTOM_ESP32

    config REBOOT
        default
