=============================
GPIO Interrupts and Callbacks
=============================

In standalone, pure C programming targeting an embedded microcontroller, checking if a button has been pressed typically involves a technique called **polling**. Inside your main execution thread, you create an infinite loop (while(1)) and continuously read the state of a General Purpose Input/Output (GPIO) pin by examining a specific hardware register via a volatile memory pointer.

**BTW, Why are Interrupts Required?** While straightforward, polling exposes severe architectural limitations: the CPU wastes billions of clock cycles constantly reading a register that rarely changes state, causing massive power inefficiencies. Furthermore, if the microcontroller is busy executing a long mathematical computation, an analog sensor read, or a heavy delay routine, it might completely miss a brief button press.

------------------------------
What are Interrupt Controllers
------------------------------

To solve this, hardware manufacturers build Interrupt Controllers (such as the Nested Vectored Interrupt Controller, or NVIC, in ARM Cortex-M processors). Instead of the software repeatedly checking the hardware, the hardware alerts the software when an external physical event occurs, such as a voltage transition on a pin.

When a button forces a change from a low voltage to a high voltage (a rising edge), the hardware instantly pauses the current CPU execution pipeline, saves the processor registers to a stack, and branches to a specific memory location called an **Interrupt Service Routine (ISR)**.

-----------------------
GPIO Interrupt Callback
-----------------------

The Zephyr Real-Time Operating System (RTOS) completely abstracts this hardware-specific vector patching through a standardized, highly structured event-driven subsystem known as **GPIO Interrupt Callbacks**.

Architectural Model: How Zephyr Manages Callbacks and Interrupt Layers
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

In a pure C application on bare metal, you hook a function up to an interrupt by writing its address directly into a designated slot in a hardware vector table or using compiler-specific function tags (like __attribute__((interrupt))).

In Zephyr, this direct manipulation is heavily discouraged and restricted to maximize portability across hundreds of microcontrollers. Zephyr introduces a *decoupled, multi-tier callback management system*.

When an external hardware interrupt triggers on a physical GPIO pin, the execution flows through three distinct layers:

 1. **The Core Hardware ISR**: The processor traps the interrupt and branches to an internal, driver-level ISR provided by Zephyr's silicon vendor layer (e.g., the low-level STMicroelectronics, NXP, or Espressif GPIO driver code).

 2. **The Subsystem Dispatcher**: This vendor driver inspects its internal status registers to determine which exact pin triggered the event. It then dispatches control to a linked list of software structures registered for that specific GPIO port.

 3. **The User Callback Function**: The registered user function executes to handle the event (e.g., toggling your LED).

To bridge your user code to this underlying dispatch framework without dynamically wasting heap memory, Zephyr uses a dedicated container structure called **struct gpio_callback**. This structure does not copy data; it acts as a listener node within an `intrusive singly-linked list` managed directly by the GPIO driver core.

------------------------------------------------------------------------
Understanding the Key Mechanics: struct `gpio_callback` and Linked Lists
------------------------------------------------------------------------

To understand why Zephyr manages interrupts this way, look closely at the definition of struct gpio_callback found within Zephyr's driver API core headers. The structure acts as an intrusive node in a linked list, tracking the targeted pin bitmask and matching user callback function pointer.

.. code-block:: c
    /**
     * Container of Singly-Linked List Nodes;
     * sys_snode_t node; Points to the next node, the **Next Node** is the 1st member of the next gpio_callback.
     * gpio_callback can therefore be dynamic in length, and modified anytime without interfering in Linked List Traversal Logic.
    */
    struct gpio_callback {
        sys_snode_t node;               /* Internal singly-linked list node tracking pointer */
        gpio_callback_handler_t handler; /* The function pointer to your user C function */
        gpio_port_pins_t pin_mask;
        /* A 32-bit bitmask indicating which pins trigger this handler */
    };

Because microcontrollers typically group physical pins into logical "Ports" (such as Port A, Port B, or GPIO0, GPIO1), an interrupt is physically wired to the port as a whole. When your application registers a callback, Zephyr appends your struct gpio_callback container into a singly-linked list dedicated to that specific port.

When any pin on that port generates a hardware interrupt, the core dispatcher iterates through this linked list, performing a bitwise AND operation between the pin that fired and your pin_mask. If a match is found, your handler is executed.

This architecture offers two primary benefits:

 * **Memory Efficiency:** You allocate the memory for struct gpio_callback yourself (usually as a global or static variable). The operating system does not have to allocate dynamic memory for tracking handlers.

 * **Flexibility:** A single callback structure can listen to multiple pins simultaneously if you combine pin bitmasks (e.g., BIT(PIN_A) | BIT(PIN_B)), routing multiple physical buttons to a single intelligent handler function.

======================================================
Implementation Workflow: Step-by-Step Code Walkthrough
======================================================

To move your existing button-toggling application from a synchronous polling loop to an asynchronous, interrupt-driven model, you must execute four steps in your application code.

1. Define the Callback Container and Handler Function
First, declare your callback container structure as a persistent global or static variable so its memory context remains intact throughout your application lifecycle. Then, author your callback handler matching Zephyr's standard signature:

.. code-block:: c
    #include <zephyr/kernel.h>
    #include <zephyr/drivers/gpio.h>

    /* Step 1a: Allocate the persistent callback container structure */
    static struct gpio_callback button_cb_data;

    /* Step 1b: Define the callback handler function : Function Signature is TypeDef */
    void button_pressed_handler(
        const struct device *port,
        struct gpio_callback *cb,
        uint32_t pins)
    {
        /** WARNING: Follow Safe ISR Rules.
         * This code runs inside an Interrupt Service Routine context!
         * Keep it incredibly fast, deterministic, and non-blocking.
         * For instance, LOG can be deferred instead of Printing.
        */
        printk("Interrupt triggered! Pin Bitmask that Fired: 0x%08X\n", pins);
        
        /* Place your LED toggle logic here */
    }

2. Initialize the Callback Structure
Inside your initialization routine (such as main()), use the helper macro gpio_init_callback() to clear the memory footprint, attach your function pointer, and specify which pin should listen.

.. code-block:: c
    /* Assuming 'button_gpio_spec' is a valid struct gpio_dt_spec resolved from Device Tree */
    gpio_init_callback(&button_cb_data, button_pressed_handler, BIT(button_gpio_spec.pin));

*Note: The macro shifts the integer pin index into a 32-bit logical bitmask using the standard BIT() macro.*

.. code-block:: c
    #define BIT 1UL << (n)

3. Attach the Callback Node to the Port Linked List
Use the Zephyr API function gpio_add_callback() to register your structure with the designated driver instance. This appends your handler into the active dispatch pipeline.

.. code-block:: c
    /* Link a ISR callback to a certain Port | Device */
    int ret = gpio_add_callback(button_gpio_spec.port, &button_cb_data);
    if (ret != 0) {
        printk("Error: Failed to register callback structure (Error Code: %d)\n", ret);
        return ret;
    }

4. Configure the Hardware Interrupt Trigger Flags
Finally, tell the physical silicon controller exactly what electrical conditions should trigger the interrupt on that pin. Use gpio_pin_interrupt_configure_dt().

.. code-block:: c
    ret = gpio_pin_interrupt_configure_dt(&button_gpio_spec, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0) {
        printk("Error: Failed to configure hardware interrupt triggers (%d)\n", ret);
        return ret;
    }

The configuration flags dictate electrical interpretation:
 * GPIO_INT_EDGE_TO_ACTIVE: Fires an interrupt when the pin moves from its passive electrical state to its active state (e.g., low-to-high on an active-high pin).

 * GPIO_INT_EDGE_BOTH: Fires on both rising and falling edges, useful if you want to detect both the initial press and the final release of a button.

 * GPIO_INT_LEVEL_ACTIVE: Fires continuously as long as the pin remains in its active state. *Use with extreme caution, as it can overwhelm the CPU if the state persists.*

--------------------------------------------------------
Critical Constraints: The Cardinal Rules of ISR Contexts
--------------------------------------------------------

Writing code inside an interrupt handler is fundamentally different than writing code inside standard C threads or a main() function. When your button_pressed_handler executes, it is operating within **ISR Context**.

Because interrupts preempt the standard scheduler, the normal thread-scheduling mechanisms are paused. You must follow these strict rules to avoid crashing your system or corrupting memory:

 * **Never Block:** You cannot call functions that place the current context to sleep or wait for a resource. Functions like k_msleep(), k_sem_take() (with a non-zero timeout), or k_mutex_lock() are strictly prohibited. Attempting to block will trigger a fatal kernel panic.

 * **Keep Execution Ultra-Short:** An interrupt blocks lower-priority system tasks and other interrupts. Doing heavy text processing, string parsing, or long loops inside a callback can cause the system to drop network packets, miss sensor signals, or fail real-time deadlines.

 * **Offload Work to Threads:** If a button press triggers a heavy task (like sending a payload over Wi-Fi), use the callback handler solely to signal a processing thread using light signaling mechanisms, such as calling k_sem_give() or submitting a work item to a system work queue.

===============
Further Reading
===============

To deepen your architectural mastery of Zephyr's hardware abstraction layers and execution mechanics, explore these source file paths inside your local Zephyr project repository and their corresponding reference structures:
 1. **GPIO Core Driver Header**: Open zephyr/include/zephyr/drivers/gpio.h to read the complete structural documentation for struct gpio_callback and inspect the complete list of interrupt configuration bitmasks (GPIO_INT_*) `GPIO link`.

 .. _GPIO link: https://docs.zephyrproject.org/latest/hardware/peripherals/gpio.html

 2. **Singly-Linked List Architecture**: Inspect zephyr/include/zephyr/sys/sflist.h to see how Zephyr implements its low-overhead intrusive singly-linked data structures used to link callback arrays without dynamic heap management.

 3. **Official Reference Manuals**:
   * Consult the official Zephyr Project GPIO Driver API Specification for detailed definitions of gpio_add_callback() and error-handling conditions.
   * Read the Zephyr Kernel Interrupt Service Routines Guide to deeply understand the conceptual differences between thread context and ISR execution context.
