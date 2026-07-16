####################################################################
Architectural and Physical Foundations of Direct Memory Access (DMA)
####################################################################

In classic Von Neumann or Harvard microcontroller architectures, the Central Processing Unit (CPU) acts as the sole orchestrator of the system buses. When a peripheral — such as a Universal Asynchronous Receiver-Transmitter (UART), an Serial Peripheral Interface (SPI) controller, or an Analog-to-Digital Converter (ADC)—receives data, it places that data into a local, hardware-level hardware FIFO or register. To move this data into internal Static Random-Access Memory (SRAM) for application processing, the CPU must execute an explicit polling loop or respond to an Interrupt Service Routine (ISR).

Under an interrupt-driven paradigm, every arriving byte or word triggers an electrical interrupt signal. The CPU must halt its current execution pipeline, save its program counter and core registers to the stack (context-save overhead), branch to the ISR vector table, fetch the data from the peripheral register via a load instruction, write the data to an SRAM memory address via a store instruction, increment the tracking pointer, and finally restore its previous state (context-restore overhead). When operating at high data rates — such as megabit-per-second SPI streams or high-fidelity audio I2S feeds this process repeats millions of times per second. The CPU becomes entirely consumed by the trivial mechanical task of moving data bits across the data bus, leading to severe pipeline starvation, missed real-time deadlines, and excessive power draw.

============
Key Concepts
============

1. DMA operation
    a. Bus Mastering and Arbitration
    b. Scatter-Gather Architecture and Linked List Descriptors
2. Scatter-Gather Architecture in ESP32 DevKit c
    a. lldesc_t [Linked List Descriptor] (Must be in SRAM and not the Flash | PSRAM for Reliable Parsing)
3. Why data must be in the DRAM for a ESP32 (or Equivalent DMA Capable Memory Location in any MCU) for DMA Access? [
       * Prevent address resolution failure
   ]
4. Memory pathways
       * MEMORY_TO_MEMORY
       * MEMORY_TO_PERIPHERAL
       * PERIPHERAL_TO_MEMORY
5. Why `Semaphores` are used inside ISRs and `Mutexes` are not?

============
Introduction
============

Direct Memory Access (DMA) is a dedicated hardware subsystem engineered to solve this precise computational bottleneck. A DMA controller is an *autonomous bus master* containing its own specialized:

1. address generation logic,
2. loop counters, and
3. state machines.

It operates in parallel with the CPU, taking over the data and address buses to transfer blocks of data directly between peripherals and memory, or between separate memory regions, entirely without CPU intervention.

To execute these autonomous transfers safely, the architecture relies on several core concepts:

 * **Bus Mastering and Arbitration** Because both the CPU and the DMA controller reside on the same system bus (such as an Advanced High-performance Bus, or AHB), they cannot simultaneously drive address and data signals onto the wire without causing electrical collisions. The system features a hardware module known as a Bus Arbiter.

  When a peripheral signals that data is ready, the DMA controller requests bus mastership. The arbiter evaluates the request against current CPU cycles and temporarily yields control of the bus to the DMA engine. This data exchange can occur via:
   1. Burst mode (where the DMA controller locks the bus and transfers a continuous block of data)
   2. Cycle-stealing mode (where the DMA engine interleaves single-word transfers with the CPU's instruction fetches, minimizing the performance impact on the running thread).

 * **Scatter-Gather Architecture and Linked List Descriptors** Standard primitive DMA engines require a single, contiguous block of physical memory defined by a rigid start address and a static byte counter. However, real-time operating systems frequently manage memory using fragmented blocks or non-contiguous network buffers. High-performance DMA subsystems utilize a technique called "Scatter-Gather".
 Instead of relying on a single set of hardware configuration registers, the DMA controller fetches its transfer instructions directly out of a chain of structured control blocks stored in SRAM. These control blocks, known as Linked List Descriptors, contain pointers to the next transfer block, target source/destination addresses, and control flags. This mechanism allows the DMA hardware to automatically step through non-contiguous fragments of memory, "scattering" incoming peripheral data into multiple independent buffers or "gathering" data from separate fragments into a single outbound peripheral stream without pausing to ask the CPU for new instructions.

 Instead of giving the DMA controller a single start and stop address, you give it a pointer to a Linked List of Descriptors (or a lookup table) stored in memory.
    1. Each descriptor in the list contains:A starting memory address for a specific buffer.
    2. The length/size of that specific buffer.A pointer to the next descriptor in the chain.
    3. The DMA controller reads the first descriptor, transfers that specific block of data, and then automatically moves to the next descriptor in the list without involving or interrupting the CPU.

====================================
ESP32 Silicon-Level DMA Architecture
====================================

The Espressif ESP32 System-on-Chip (SoC) implements a highly distributed, peripheral-centric DMA architecture rather than relying exclusively on a single centralized general-purpose DMA block. High-speed communication modules on the ESP32 specifically the SPI modules (SPI1, HSPI/SPI2, VSPI/SPI3), the I2S modules (I2S0, I2S1), the Ethernet MAC, and the cryptographic accelerators contain embedded, native DMA engines directly inside their peripheral register boundaries. These internal engines interface with the chip's internal SRAM via the Advanced High-performance Bus (AHB) matrix.

On the original ESP32 dual-core Xtensa LX6 architecture, these localized peripheral DMA controllers utilize a standardized, hardware-parsed linked list descriptor format to execute scatter-gather transfers. The system layout dictates that the hardware descriptors themselves must reside within internal SRAM; they cannot be safely parsed if placed in external flash memory or external PSRAM.

The physical structure of an ESP32 DMA hardware descriptor is strictly defined as a 128-bit (`16-byte`) C Struct aligned to a 4-byte boundary. Its structural composition is mapped as follows:

.. code-block:: c
    // DMA Linked List Descriptor in ESP32
    struct lldesc_t {
        uint32_t size:12,        /* Maximum size of the data buffer pointed to by 'buf' */
                length:12,       /* Actual number of valid bytes currently held in the buffer */
                offset:5,        /* Offset relative to the start of the buffer (typically 0) */
                unknown:1,       /* Reserved internal hardware bit */
                eof:1,           /* End-of-Frame marker flag */
                owner:1;         /* Ownership bit: 1 = DMA Hardware Engine, 0 = CPU Core */
        void     *buf;           /* 32-bit physical pointer to the data buffer in SRAM */
        struct lldesc_t *next;   /* 32-bit physical pointer to the next descriptor block (NULL/0 to terminate) */
    };

The operation of this descriptor chain is governed by a strict hardware-software contract enforced by the owner bit. When the CPU configures a transfer, it allocates memory for the `descriptors and the data buffers, populates the fields`, and explicitly `writes a 1 to the owner bit` of each descriptor in the chain before triggering the `peripheral's DMA start bit`.

Once the peripheral DMA engine is active, the hardware loops through the descriptors.

If the hardware reads a descriptor where owner == 1, it takes control, reads or writes data to the address stored in the buf pointer, updates the length field with the total number of bytes transferred, and automatically flips the owner bit back to 0.

+---------------+----------------------+------------------------------------------------------------------------+
| Bit State     | Who Owns the Buffer? | What it means                                                          |
+---------------------------------------------------------------------------------------------------------------+
|   owner == 1  |   Hardware (DMA)     | Software must NOT touch the data. Hardware is allowed to process it.   |
+---------------+----------------------+------------------------------------------------------------------------+
|   owner == 0  |   Software (CPU)     | Hardware will halt if it touches it. Software can safely read/write it.|
+---------------+----------------------+------------------------------------------------------------------------+

Flipping this bit serves as an unblock signal to the CPU, indicating that the specific buffer fragment has been processed and is safe for application read/write tasks. If the DMA engine encounters a descriptor where owner == 0 before reaching a termination condition, it halts execution and fires a `descriptor empty interrupt`, preventing memory corruption. [
    If the DMA engine moves to the next descriptor and sees owner == 0, it knows the CPU still owns it (or hasn't prepared it yet).
]

==========================================
Memory Constraints and Silicon Limitations
==========================================

The architecture of the original ESP32 places rigorous constraints on which regions of memory can participate in DMA transfers. The ESP32 utilizes a highly specialized internal memory mapping layout where the internal SRAM is split into separate Instruction RAM (IRAM) and Data RAM (DRAM) regions.

Crucially, only the DRAM address space (ranging from 0x3FFA_E000 to 0x3FFF_FFFF, i.e. 8KB of DRAM) is physically wired to the AHB bus matrix lines accessed by the peripheral DMA controllers. If an application developer attempts to pass a pointer residing within the `text segment`, a thread stack allocated out of IRAM, or an external serial pseudo-static RAM (PSRAM) address to a standard ESP32 DMA configuration register, the hardware will experience an address resolution failure. This results in silent data corruption, zero-byte transfers, or a hard peripheral fault. **Any buffer targeted by a DMA engine must be explicitly allocated from DMA-capable internal memory**.

============================================
Zephyr RTOS DMA Driver Subsystem Abstraction
============================================

Zephyr RTOS abstracts these intricate silicon-level variations by utilizing a unified, standardized DMA device driver subsystem interface defined across the core header architecture in `<zephyr/drivers/dma.h>`. Instead of forcing application code to manually instantiate `lldesc_t` structures or directly toggle ESP32 hardware registers, Zephyr provides a high-level API that maps generic configuration blocks down to the vendor-specific driver implementations at compile time.

==================================
Devicetree Hardware Representation
==================================

In Zephyr, the hardware topology of the DMA controller is captured entirely within the Devicetree engine. The low-level driver implementation for the Espressif chip family defines the global DMA nodes. An application overlay or board-level definition links a peripheral to a specific DMA channel using specialized devicetree phandle arrays. Consider the following structural snippet representing an abstract high-speed peripheral binding to a DMA controller:

.. code-block:: dts
    / {
        soc {
            dma0: dma-controller@3ff00000 {
                compatible = "espressif,esp32-dma";
                reg = <0x3ff00000 DT_SIZE_K(1)>;    // 1KB long DMA mapped memory
                #dma-cells = <1>;
                status = "okay";
            };
        };
    };

    /* Configure ADC 0 to use DMA Channel 3 for Direct Memory Reads */
    &adc0 {
        status = "okay";
        dmas = <&dma 3>;       /* Points to &dma, passing '3' as the channel cell */
    };

The** #dma-cells = <1>;** property informs the Devicetree compiler that exactly one integer cell (the channel index) must follow the controller's phandle reference whenever a device hooks into the DMA bus. In the C/C++ application layer, these properties are safely extracted using standard compile-time macros such as DMA_DT_SPEC_GET(DT_NODELABEL(my_high_speed_peripheral)), which populates a structured struct dma_dt_spec metadata container holding the underlying controller device pointer and the assigned channel settings.

=================================
Critical Configuration Structures
=================================

To initiate an actual data movement pipeline, the software relies on two vital struct types defined by the Zephyr API: `struct dma_block_config` and `struct dma_config`. The relationships and internal fields of these structures dictate how the underlying low-level driver code constructs its hardware descriptors:

.. code-block:: c
    /**
     * struct dma_block_config (The Data Map)
     * Configures a specific physical memory segment.
     * It dictates where the data is coming from, where it is going, and how much data to move in a single burst.
    */
    struct dma_block_config {
        uint32_t source_address;
        /* Hard memory or register address of the data source */
        uint32_t dest_address;
        /* Hard memory or register address of the data destination */
        uint32_t block_size;
        /* The total length of the data block to be transferred in bytes */
        struct dma_block_config *next_block;
        /* Pointer to next block config, enabling scatter-gather lists */
        uint32_t source_gather_interval;
        uint32_t dest_scatter_interval;
        uint16_t dest_scatter_count;
        uint16_t source_gather_count;
    };

    /**
     * struct dma_config (The Channel Director)
     * Configures the global behavior of the entire DMA channel.
     * It dictates how data moves, who to notify when it's done (the callback), and manages the transfer overall.
    */
    struct dma_config {
        uint32_t channel_direction;
        /* Direction configuration enum (e.g., MEMORY_TO_PERIPHERAL) */
        uint32_t source_data_size;     /* Word width of the source side (1, 2, 4, or 8 bytes) */
        uint32_t dest_data_size;       /* Word width of the destination side */
        uint32_t source_burst_length;
        /* Number of continuous data words transferred per bus lock */
        uint32_t dest_burst_length;
        uint32_t channel_priority;     /* Hardware priority ranking for bus arbitration loops */
        uint32_t dma_callback_arg;
        /* User-supplied context pointer passed to the ISR callback */
        dma_callback_t dma_callback;
        /* Function pointer triggered upon transfer completions or faults */
        struct dma_block_config *head_block;
        /* Pointer to the first element of the transfer blocks list */
        uint8_t block_count;
        /* Total count of block nodes currently linked in the sequence */
    };

--------------------------
Channel Direction Topology
--------------------------

The channel_direction field specifies the transfer topology, altering how the hardware generates read/write control signals. It supports three distinct structural pathways:

 * MEMORY_TO_PERIPHERAL: The DMA engine increments its source address pointer across internal SRAM while keeping the destination address locked onto the physical, stationary input register of a peripheral (e.g., the transmit register of an SPI bus).
    In a Memory-to-Memory transfer (like our main() example moving data from tx_buffer to rx_buffer), calling dma_start() acts as an immediate trigger. The hardware begins moving bytes the exact microsecond you call the function, and the callback fires moments later.
 * PERIPHERAL_TO_MEMORY: The source address remains stationary, reading continuously from a peripheral's output register, while the destination address increments across a target SRAM buffer.
 * MEMORY_TO_MEMORY: Both source and destination addresses increment continuously across distinct SRAM buffers, executing a high-speed memory copy operation that completely offloads the work from the CPU core.

---------------
Note for Reader
---------------

The RTOS doesn't just throw suspended tasks it into a random blocked queue. It places them into a specific Blocked List associated with that semaphore. Each semaphore is associated with its own queue.

========================================================
Lower-Level Software Execution Flow & Execution Contexts
========================================================

When a Zephyr application invokes the DMA subsystem, the execution model flows through a highly integrated pathway dividing compile-time validation, synchronous supervisor preparation, asynchronous hardware execution, and high-priority interrupt handling.

 * **Driver Instantiation and Configuration**: The application thread initializes a struct dma_config and hooks it into a struct dma_block_config array. It then invokes the core driver API function `dma_config(dma_device, channel_id, &config)`. This call instantly routes down to the internal Espressif HAL driver layer. The driver maps the channel ID, verifies that the requested configuration variables match the structural limits of the silicon, and registers the application's asynchronous `dma_callback pointer` into its localized tracking array.
 * **Hardware Descriptor Compilation**: When the application triggers dma_start(dma_device, channel_id), the lower-level driver processes the linked list of dma_block_config blocks. For each block, the driver claims an internal lldesc_t structure out of a pre-allocated pool in DRAM. It copies the source_address or dest_address directly into the descriptor's buf field, splits the requested block_size across multiple descriptors if it exceeds the maximum byte capacity of a single descriptor (typically 4095 bytes due to the 12-bit hardware limit), chains the nodes together via the next pointers, and flags the final descriptor block with the eof (End-of-Frame) bit. Finally, it sets the owner bits to 1.
 * **Initiating the Bus Master Transfer**: The driver executes a memory barrier instruction to ensure all descriptor writes are committed to the physical SRAM cells, then writes directly to the channel's hardware enablement register. The CPU thread that invoked dma_start() completes its configuration routine and returns immediately. It does not block. The calling thread can now transition to an idle state or continue executing unrelated logical loops, leaving the physical data migration entirely up to the independent DMA hardware engine.
 * **The Interrupt Service Routine (ISR) Callback Context**: As the hardware DMA engine processes the data bits over the AHB bus, it reaches the final descriptor in the chain where the eof bit is set. Upon successfully transferring the final byte, the DMA channel hardware triggers an electrical interrupt line wired into the ESP32 Interrupt Matrix. The CPU core instantly suspends its current thread context and executes the low-level vendor driver ISR handler.

------------------------------------------------------------------------------
The Cardinal Rule of DMA Callbacks [**Callbacks are Executed in ISR Context**]
------------------------------------------------------------------------------

The driver's internal ISR clears the underlying peripheral hardware interrupt flags, processes any error flags, and then directly executes the user-supplied dma_callback function.

Because this callback executes inside an explicit Interrupt Service Routine context (ISR execution level), it is subject to the strict real-time constraints of the operating system kernel. The code inside the callback function must never execute blocking operations, loop indefinitely, or call any kernel APIs that trigger thread rescheduling (such as `k_sleep()`, `k_mutex_lock()`, or blocking queue fetches).

Instead, the callback must be restricted to minimal, ultra-fast signaling tasks. It should cleanly acknowledge the transfer status, update an application state machine flag, or unblock a sleeping application-level processing thread by releasing a high-speed kernel primitive, such as invoking `k_sem_give()` or submitting a work item to a system workqueue via `k_work_submit()`.

-------------------------------------------------
Memory Management Precautions and Cache Coherency
-------------------------------------------------

Developing low-level software using DMA requires strict attention to memory placement and variable alignment. In modern embedded SoCs featuring high-speed instruction and data caches (L1 caches), the CPU core does not always read directly from physical SRAM cells. Instead, it pulls lines of data into its local cache.

If a peripheral DMA engine writes new data directly into physical SRAM, the CPU's internal cache may become mismatched with the underlying physical RAM states—a classic hardware state condition known as a Cache Coherency Paradox. While the original ESP32 dual-core Xtensa architecture maintains data cache coherency for internal DRAM across its hardware bus matrix interfaces, developers must still enforce precise memory alignment.

To ensure that variables do not cross bus line granularities or overlap with adjacent thread data sections, any buffer or descriptor structure allocated for a DMA transaction must be explicitly decorated with alignment attributes. In Zephyr, this is handled via toolchain compiler macros:

.. code-block:: c
    /* Forces the compiler to align the buffer boundary onto a native bus-width line, 
    and places it explicitly into a DMA-accessible internal memory section */
    __aligned(4) uint8_t dma_buffer[256] __attribute__((section(".dma")));

By enforcing static compilation boundaries, explicitly separating ISR-level signaling code from thread-level processing code, and allowing the devicetree to handle bus routing, your software achieves high throughput and low jitter, exploiting the full performance capabilities of the ESP32 silicon architecture under Zephyr RTOS.
