####################################################
Software Design Principles for Embedded Applications
####################################################

+---------------------+----------------------+---------------------------------------------------------------------------------+------------------------------------------+
| Layer               | Design Pattern Used  | Responsibility                                                                  | Zephyr Mechanism                         |
+=====================+======================+=================================================================================+==========================================+
| Hardware / Drivers  | Factory Adapter      | Abstracts specific hardware (e.g., I2C vs SPI sensor variants) into a unified   | Zephyr Device Tree (DT_DRV_COMPAT) &     |
|                     |                      | sensor API.                                                                     | Device API                               |
+---------------------+----------------------+---------------------------------------------------------------------------------+------------------------------------------+
| Data Distribution   | Observer (Zbus)      | Broadcasts newly read sensor data without knowing who is listening.             | k_msgq or zbus_chan                      |
+---------------------+----------------------+---------------------------------------------------------------------------------+------------------------------------------+
| Application Control | State Pattern (SMF)  | Governs how the device behaves based on its current mode and sensor inputs.     | Zephyr State Machine Framework           |
+---------------------+----------------------+---------------------------------------------------------------------------------+------------------------------------------+
| Action Execution    | Command Pattern      | Queues and serializes external inputs (BLE, Cloud, Buttons) to safely alter the | k_msgq + Function Pointers               |
|                     |                      | application state.                                                              |                                          |
+---------------------+----------------------+---------------------------------------------------------------------------------+------------------------------------------+
| System Services     | Facade Pattern       | Provides simple, testable wrappers around storage, flash, and networking.       | Custom C API wrapping Zephyr Subsystems  |
+---------------------+----------------------+---------------------------------------------------------------------------------+------------------------------------------+

#######################################
Work Queues and Thread Based Scheduling
#######################################

Core Scheduler vs. Work Queue ArchitectureAn RTOS Scheduler dictates which thread gets CPU time based on real-time priorities and timing constraints, operating at the core of the OS to manage thread states (Ready, Running, Blocked) and perform context switches. In contrast, a Work Queue Scheduler is a deferred execution mechanism that handles non-urgent processing by managing a queue of functions or "work items" executed by a dedicated, pre-allocated background thread context. In Zephyr RTOS, the main thread scheduler uses a priority-based, preemptive algorithm to manage cooperative (negative priority) and preemptible (zero/positive priority) threads, making it the perfect choice for independent, continuous while(1) loops or long-running logic. Conversely, Zephyr’s work queue system allows an Interrupt Service Routine (ISR) or thread to use k_work_submit() to append a tiny data structure pointer to a queue, waking a background thread—such as the built-in system work queue (k_sys_work_q)—to execute the function to completion and immediately go back to sleep.Why Work Queues Eclipse Spawning New ThreadsSubmitting a task to an existing work queue is vastly superior to spawning a new thread because thread creation is an expensive, heavy, and risky operation in resource-constrained embedded systems. Spawning a new thread requires allocating a dedicated stack via K_THREAD_STACK_DEFINE (often 512 bytes to several kilobytes), initializing Thread Control Blocks (TCBs), and adding the thread to the main scheduler's ready queue—an intensive process that will crash your system if attempted inside an ISR due to blocking memory allocations. Work queues provide zero allocation overhead, functioning as a lightning-fast, non-blocking O(1) pointer-append operation that is perfectly safe for ISR offloading while offering massive RAM savings by letting dozens of drivers sequentially share a single stack. Furthermore, work queues inherently prevent CPU "context switching thrashing" by automatically serializing rapid back-to-back events (like 10 noisy button bounces in a millisecond) into a clean line executed one by one, while providing built-in life cycle management where completed work items are naturally forgotten without requiring manual thread cleanup or deletion code.The Ultimate Firmware Design RulesThis architectural distinction leads to clear design patterns: things that must run periodically and forever belong in a dedicated thread, while things that run sequentially and one after another belong in a work queue. For example, a Bluetooth stack requires its own thread because it is highly complex, handles radio timing, and must naturally wait or block for radio events and connection handshakes; putting it in the shared system work queue would freeze the entire queue and stall the system. For lightweight, periodic processes like blinking an LED every 500ms or reading a sensor, Zephyr provides Delayable Work Items (k_work_delayable) which flip a GPIO pin in microseconds and then use k_work_reschedule() to schedule themselves to run again later. This powerful mechanism completely blurs the lines by creating periodic behavior without wasting hundreds of bytes of RAM on a permanent thread stack, provided you strictly obey the golden rule of the default system work queue: never block or sleep inside a system work item.

####################################################
Zephyr Bus : Observer (Publisher Subscriber) Pattern
####################################################

Zephyr's Message Bus (Zbus) is the gold standard for implementing the Observer (Publish-Subscribe) pattern in Zephyr RTOS. It provides a thread-safe, type-safe, and highly decoupled way to pass data between threads using the Publisher and Subscriber Pattern without them needing to know about each other's existence.

==================================
Zephyr Bus : The Core Architecture
==================================

In Zbus, the architecture revolves around Channels, Publishers, and Listeners/Subscribers:

[ Sensor Thread ]  ---> ( Publishes ) ---> [  Zbus Channel  ] 
                                                  |
                         +------------------------+------------------------+
                         | (Notifies)                                      | (Notifies)
                         v                                                 v
           [ Business Logic Thread ]                             [ Telemetry / Cloud Thread ]
          (Synchronous / Queue Listener)                         (Asynchronous / Msg Queue)

===========================
Step-by-Step Implementation
===========================

**Step 1**: Enable Zbus in `prj.conf`

First, ensure Zbus is enabled in your Zephyr Project configuration file:

.. code-block:: Kconfig
    CONFIG_ZBUS=y
    # Optional but Highly Recommended for Debugging
    CONFIG_ZBUS_LOG_LEVEL_INF=y

**Step 2**: Define your Data Structure & Channel

Create a shared header file (e.g., messages.h) that defines the data structure your Factory Adapter will output, and declare the Zbus channel.

`messages.h`
.. code-block:: C
    #ifndef MESSAGES_H_
    #define MESSAGES_H_

    #include <zephyr/zbus/zbus.h>

    // 1. The payload structure
    struct sensor_data_msg {
        float temperature;
        float humidity;
        uint64_t timestamp;
    };

    // 2. Declare the channel so other files can see it
    ZBUS_CHAN_DECLARE(sensor_data_chan);

    #endif /* MESSAGES_H_ */

In your main or a dedicated C file, define the channel:

`messages.c`
.. code-block:: C
    #include "messages.h"

    // Define the channel: Name, Payload Type, Validator (NULL), Observers (NULL for now), Runtime Observers (enabled)
    ZBUS_CHAN_DEFINE(sensor_data_chan,
                    struct sensor_data_msg,
                    NULL, 
                    NULL,
                    ZBUS_OBSERVERS_EMPTY,
                    ZBUS_CHAN_KEEP_LATEST
    );

**Step 3**: The Publisher (Sensor Thread + Factory Adapter)

Your sensor thread uses your Factory Adapter to read the hardware, packs it into the struct, and publishes it.

.. code-block:: C
    #include <zephyr/kernel.h>
    #include "messages.h"
    #include "sensor_factory.h" // Your Factory Adapter header

    void sensor_thread_entry(void *p1, void *p2, void *p3)
    {
        // Instantiated sensor via your Factory Adapter pattern
        struct sensor_adapter *sensor = sensor_factory_create(TEMPERATURE_SENSOR_ID);

        struct sensor_data_msg msg;

        while (1) {
            // 1. Read data using the adapter
            msg.temperature = sensor->read_temp(sensor);
            msg.humidity = sensor->read_humidity(sensor);
            msg.timestamp = k_uptime_get();

            // 2. Publish to the Zbus channel (Thread-safe)
            // Arguments: Channel, Data Pointer, Timeout if locked
            int err = zbus_chan_pub(&sensor_data_chan, &msg, K_MSEC(10));
            if (err) {
                printk("Failed to publish sensor data: %d\n", err);
            }

            k_msleep(1000); // Poll every second
        }
    }

    K_THREAD_DEFINE(
        sensor_thread_id,
        1024,
        sensor_thread_entry,
        NULL, NULL, NULL,
        5, 0, 0);

Step 4: The Subscriber (Business Logic)

There are two primary ways to listen to a Zbus channel:
 * Listeners (immediate callback context)
 * Subscribers (asynchronous, queue-backed).

For complex Business Logic/State Machines, Subscribers are best because they prevent blocking the publisher.

Here is how your Business Logic thread subscribes using a Zephyr message queue managed by Zbus:

.. code-block:: C
    #include <zephyr/kernel.h>
    #include "messages.h"

    // 1. Define a Zbus Subscriber (creates an internal message queue)
    ZBUS_SUBSCRIBER_DEFINE(bus_logic_sub, 4); // Queue depth of 4 messages

    void business_logic_thread_entry(void *p1, void *p2, void *p3)
    {
        // 2. Dynamically attach this subscriber to the channel at runtime
        // (Alternatively, you can hardcode it in ZBUS_CHAN_DEFINE)
        zbus_chan_add_obs(&sensor_data_chan, &bus_logic_sub, K_FOREVER);

        const struct zbus_channel *chan;
        struct sensor_data_msg received_data;

        while (1) {
            // 3. Block until a new message arrives in the subscriber's queue
            // This acts exactly like waiting on a k_msgq
            int err = zbus_sub_wait(&bus_logic_sub, &chan, K_FOREVER);
            
            if (err == 0 && chan == &sensor_data_chan) {
                // 4. Read the data out of the channel
                zbus_chan_read(chan, &received_data, K_MSEC(5));

                // 5. Route to your Business Logic State Machine
                printk("[Biz Logic] Received Temp: %.2f C\n", received_data.temperature);
                
                if (received_data.temperature > 40.0f) {
                    // Trigger an Alarm State change!
                }
            }
        }
    }
    K_THREAD_DEFINE(
        biz_logic_thread_id,
        2048,
        business_logic_thread_entry,
        NULL, NULL, NULL,
        5, 0, 0);

True Decoupling: Your sensor_thread doesn't know who needs the temperature data. It just publishes it. If you add a Telemetry Thread next week to send data to AWS IoT via MQTT, you zero-change your sensor thread. You simply add a second subscriber.

Thread Safety Built-in: Zbus handles the internal mutexes and critical sections. Your Factory Adapter only worries about hardware registers, while Zbus handles OS thread isolation.

Zero Copies (Almost): Zbus passes references when possible and handles memory allocation inside its macro definitions seamlessly, which keeps it extremely lightweight for microcontrollers.

#################################################
Action Execution : Command Pattern in Zephyr RTOS
#################################################

The Command Pattern encapsulates a request or action as an object. In a C-based RTOS like Zephyr, a **command object** translates to a thread-safe struct containing a function pointer and a data payload.

This pattern is incredibly powerful when your device needs to process actions arriving asynchronously from multiple varying sources—such as a Bluetooth (BLE) write callback, a UART Command Line Interface (CLI), a Cloud MQTT message, or a Physical Button Interrupt (ISR)—without causing race conditions or blocking the calling thread.

=====================
The Core Architecture
=====================

Instead of having a BLE callback directly modify variables or execute long-running tasks inside an interrupt/system context, it packages the request into a struct command and pushes it into a shared Zephyr Message Queue (k_msgq). Your core Business Logic thread sequentially processes these commands.

[ BLE Callback ]      ---\
[ UART CLI ]          ----> (Pushes struct command) ---> [ Zephyr Message Queue (k_msgq) ]
[ Button ISR ]        ---/                                             |
                                                                       v
                                                           [ Business Logic Thread ]
                                                       (Pulls & Executes sequentially)

===========================
Step-by-Step Implementation
===========================

**Step 1**: Define the Command Structure
Create a shared header file that defines the command structure and the message queue.

`commands.h`
.. code-block:: C
    #ifndef COMMANDS_H_
    #define COMMANDS_H_

    #include <zephyr/kernel.h>

    // 1. Forward declaration of the context (your Business Logic State Machine/Context)
    struct app_ctx;

    // 2. Define the function pointer type that all commands must match
    typedef void (*command_func_t)(struct app_ctx *ctx, void *payload);

    // 3. The Command Struct (The Command Object)
    struct app_command {
        command_func_t execute;  // What to do
        void *payload;           // Data needed to do it (optional)
    };

    // 4. Declare a thread-safe Zephyr Message Queue for commands
    // Array size: 8 commands maximum queued up at once
    extern struct k_msgq command_queue;

    #endif /* COMMANDS_H_ */

In a matching .c file, define the queue:

`commands.c`
.. code-block:: C
    #include "commands.h"

    // Define the queue: Queue variable, size of item, maximum items, alignment
    K_MSGQ_DEFINE(command_queue, sizeof(struct app_command), 8, 4);

Step 2: Implement Concrete Commands

Create the actual functions that represent individual actions. These functions will execute safely inside the Business Logic thread's context, meaning they can safely alter states or trigger hardware.

`concrete_commands.c`
.. code-block:: C
    #include "commands.h"
    #include "biz_logic_sm.h" // Assumes the State Pattern context from earlier

    // Command 1: Force an emergency system reset
    void command_emergency_stop(struct app_ctx *ctx, void *payload)
    {
        printk("[Command] Executing Emergency Stop!\n");
        // Change state machine directly, shut down motors, etc.
        // smf_set_state(SMF_CTX(ctx), &app_states[STATE_ALARM]);
    }

    // Command 2: Change a configuration parameter (takes a payload)
    void command_update_threshold(struct app_ctx *ctx, void *payload)
    {
        float *new_threshold = (float *)payload;
        printk("[Command] Updating temperature threshold to: %.2f\n", *new_threshold);
        
        // Safely update the value in your application context
        // ctx->temp_threshold = *new_threshold;
    }

**Step 3**: Invoking Commands (The Producers)

Now, look at how different input threads or ISRs easily instantiate and enqueue these commands without needing to know how the business logic handles them.

.. code-block:: C
    #include "commands.h"
    #include "concrete_commands.h"

    // Example A: Triggered from a Button GPIO Interrupt (ISR context!)
    void button_isr_handler(
        const struct device *port,
        struct gpio_callback
        *cb, uint32_t pins)
    {
        struct app_command cmd = {
            .execute = command_emergency_stop,
            .payload = NULL
        };

        // Push to queue from ISR (using K_NO_WAIT is mandatory here!)
        k_msgq_put(&command_queue, &cmd, K_NO_WAIT);
    }

    // Example B: Triggered from a Bluetooth GATT Write Callback (System Workqueue context)
    static float remote_temp_setting = 38.5f; 

    void ble_mesh_or_gatt_callback(void)
    {
        struct app_command cmd = {
            .execute = command_update_threshold,
            .payload = &remote_temp_setting
        };

        // Push to queue, wait up to 10ms if the queue happens to be full
        k_msgq_put(&command_queue, &cmd, K_MSEC(10));
    }

**Step 4**: The Command Consumer (Business Logic Thread)

Your Business Logic thread simply sits in a loop, blocking on the queue. When a command arrives, it pops it off and executes it, passing its own context into the function.

.. code-block:: C
    #include <zephyr/kernel.h>
    #include "commands.h"
    #include "biz_logic_sm.h"

    void business_logic_thread_entry(void *p1, void *p2, void *p3)
    {
        // Our local application state context
        struct app_ctx my_app_ctx; 
        
        struct app_command incoming_cmd;

        while (1) {
            // Block indefinitely until a command arrives in the queue
            int err = k_msgq_get(&command_queue, &incoming_cmd, K_FOREVER);
            
            if (err == 0) {
                // Execute the command! 
                // The command function runs completely inside this thread's context.
                if (incoming_cmd.execute != NULL) {
                    incoming_cmd.execute(&my_app_ctx, incoming_cmd.payload);
                }
            }
        }
    }
    K_THREAD_DEFINE(
        biz_logic_thread_id,
        2048,
        business_logic_thread_entry,
        NULL, NULL, NULL,
        5, 0, 0);

============================================
Why This is Highly Effective in Zephyr RTOS?
============================================

Defeats Concurrency Hazards: Because every action is funneled into a single queue and executed sequentially by a single thread, you eliminate the need for complex Mutexes (k_mutex) protecting your business variables.

ISR-Safe Offloading: Zephyr strictly limits what you can do inside an Interrupt Service Routine (ISR). The Command Pattern allows an ISR to quickly queue up a highly complex operation to be executed later in thread context.

Macro/CLI Extensibility: This architecture maps 1:1 to Zephyr's Shell Subsystem. If you want to create a debugging command line interface over UART, your shell commands just create a struct app_command and dump it in the queue.

############################################
State Machine Pattern for Zephyr RTOS States
############################################

To implement the State Pattern in a Zephyr RTOS application, you should use Zephyr’s native State Machine Framework (SMF).

Instead of writing a massive, unmaintainable switch-case block inside a thread loop, the State Pattern isolates the behavior of each state into separate, dedicated functions. The SMF handles state execution, entry/exit actions, and hierarchical transitions elegantly.

============
Architecture
============

Your Business Logic thread will maintain a State Machine Context. When sensor data arrives via Zbus (or another event occurs), it passes that event to the state machine, which executes logic specific to the current state.

                  [ Event Received (e.g., Temp > 40°C) ]
                                    |
                                    v
                       [ SMF Engine: Run State ]
                                    |
       +----------------------------+----------------------------+
       | If State = IDLE            | If State = ALARM           |
       v                            v                            v
[ Transition to ALARM ]     [ Ignore / Sound Siren ]     [ Transition to LOW_POWER ]

===========================
Step-by-Step Implementation
===========================

**Step 1**: Enable SMF in prj.conf
Ensure the State Machine Framework is enabled in your project configuration:

.. code-block:: KConfig
    CONFIG_SMF=y

**Step 2**: Define the Context and States
Create your state machine definition. You need a custom structure that embeds `struct smf_ctx` and an enum representing your states.


`biz_logic_sm.h`
.. code-block:: c
    #ifndef BIZ_LOGIC_SM_H_
    #define BIZ_LOGIC_SM_H_

    #include <zephyr/smf.h>

    // 1. Define the possible states
    enum app_state {
        STATE_IDLE,
        STATE_MONITORING,
        STATE_ALARM
    };

    // 2. Define the execution context (holds SMF tracking and custom app variables)
    struct app_ctx {
        struct smf_ctx ctx;        // MUST be the first member
        bool high_temp_detected;   // Custom variable/event trigger
        float last_read_temp;      // Cache for business logic use
    };

    // Export the state definitions
    extern const struct smf_state app_states[];

    #endif /* BIZ_LOGIC_SM_H_ */

Step 3: Implement State Behaviors

In your C file, implement the Entry, Run, and Exit actions for each individual state.

 * Entry: Runs once when entering the state
        (good for turning on LEDs, initializing timers).
 * Run: Runs repeatedly while in the state (processes events).
 * Exit: Runs once when leaving the state (good for cleanup).

.. code-block:: C
    // biz_logic_sm.c
    #include <zephyr/kernel.h>
    #include "biz_logic_sm.h"

    /* ------------------ STATE: IDLE ------------------ */
    static void state_idle_entry(void *obj) {
        printk("Entering State: IDLE. System stands by.\n");
    }
    static void state_idle_run(void *obj) {
        struct app_ctx *app = (struct app_ctx *)obj;
        
        // Condition to transition out of IDLE
        if (app->last_read_temp > 0.0f) { 
            smf_set_state(SMF_CTX(app), &app_states[STATE_MONITORING]);
        }
    }

    /* ---------------- STATE: MONITORING ---------------- */
    static void state_monitoring_entry(void *obj) {
        printk("Entering State: MONITORING. Checking thresholds...\n");
    }
    static void state_monitoring_run(void *obj) {
        struct app_ctx *app = (struct app_ctx *)obj;

        if (app->high_temp_detected) {
            smf_set_state(SMF_CTX(app), &app_states[STATE_ALARM]);
        } else if (app->last_read_temp == 0.0f) {
            smf_set_state(SMF_CTX(app), &app_states[STATE_IDLE]);
        }
    }

    /* ------------------ STATE: ALARM ------------------ */
    static void state_alarm_entry(void *obj) {
        printk("!!! ALERT !!! Entering State: ALARM. Activating buzzer.\n");
        // e.g., turn_on_buzzer_gpio();
    }
    static void state_alarm_run(void *obj) {
        struct app_ctx *app = (struct app_ctx *)obj;

        // Remain in alarm until condition clears
        if (!app->high_temp_detected) {
            smf_set_state(SMF_CTX(app), &app_states[STATE_MONITORING]);
        }
    }
    static void state_alarm_exit(void *obj) {
        printk("Clearing ALARM state. Silencing buzzer.\n");
        // e.g., turn_off_buzzer_gpio();
    }

    /* ------------------ POPULATE STATES ------------------*/
    // Populate the SMF state table matching the enum order
    const struct smf_state app_states[] = {
        [STATE_IDLE]       = SMF_CREATE_STATE(state_idle_entry,       state_idle_run,       NULL),
        [STATE_MONITORING] = SMF_CREATE_STATE(state_monitoring_entry, state_monitoring_run, NULL),
        [STATE_ALARM]      = SMF_CREATE_STATE(state_alarm_entry,      state_alarm_run,      state_alarm_exit),
    };

Step 4: Hook it up to the Zbus Business Logic Thread

Now tie the State Machine into the Zbus thread we built previously. When Zbus receives a sensor update, it updates the state machine's context variables and triggers a state execution.

`main.c`
.. code-block:: C
    #include <zephyr/kernel.h>
    #include "messages.h"      // From the Zbus example
    #include "biz_logic_sm.h"

    ZBUS_SUBSCRIBER_DEFINE(bus_logic_sub, 4);

    void business_logic_thread_entry(void *p1, void *p2, void *p3)
    {
        zbus_chan_add_obs(&sensor_data_chan, &bus_logic_sub, K_FOREVER);

        // 1. Initialize our State Machine Context
        struct app_ctx my_app;
        my_app.high_temp_detected = false;
        my_app.last_read_temp = 0.0f;

        // Set Initial State to IDLE
        smf_set_initial(SMF_CTX(&my_app), &app_states[STATE_IDLE]);

        const struct zbus_channel *chan;
        struct sensor_data_msg received_data;

        while (1) {
            // 2. Block until new sensor data arrives via Zbus
            int err = zbus_sub_wait(&bus_logic_sub, &chan, K_FOREVER);
            
            if (err == 0 && chan == &sensor_data_chan) {
                zbus_chan_read(chan, &received_data, K_MSEC(5));

                // 3. Update application context data from the message
                my_app.last_read_temp = received_data.temperature;
                my_app.high_temp_detected = (received_data.temperature > 40.0f);

                // 4. Trigger the State Machine to process the update
                // This runs the current state's '_run' function and handles transitions
                int smf_err = smf_run(SMF_CTX(&my_app));
                if (smf_err) {
                    printk("State Machine terminated or errored: %d\n", smf_err);
                    break;
                }
            }
        }
    }

    K_THREAD_DEFINE(
        biz_logic_thread_id,
        2048,
        business_logic_thread_entry,
        NULL, NULL, NULL,
        5, 0, 0);

=================================================
Why this is Highly Effective in RTOS Development?
=================================================

1. Deterministic Execution: The behavior of your device at any exact moment is bounded strictly by its active state block.
2. Readability: If you need to change how the ALARM state behaves, you only look at state_alarm_run and state_alarm_exit. You don't risk breaking the IDLE logic.
3. No Spaghetti Code: Adding a new state (like LOW_BATTERY) is as simple as defining a new enum, writing 3 static functions, and adding it to the table—completely bypassing complex, overlapping if/else statements.

######################################################
Factory Adapter Pattern for Multiple Sensors in Zephyr
######################################################

When managing multiple sensors in the Zephyr RTOS, relying solely on `DEVICE_DT_GET` (or `DEVICE_DT_GET_ANY`) across your application logic can quickly lead to tightly coupled, messy code. If you switch a sensor model, change a bus from I2C to SPI, or add a mock sensor for testing, you end up rewriting application-level code.

Combining the Factory Pattern and the Adapter Pattern creates a clean abstraction layer.

 * The Adapter: Wraps the Zephyr-specific `const struct device *` and standardizes the sensor data API.
 * The Factory: Handles the conditional device tree fetching (DEVICE_DT_GET) and instantiates the correct adapter.

----------------------------
The Architecture at a Glance
----------------------------

Instead of your application talking directly to the Zephyr device driver API, it interacts with a generic SensorAdapter interface.

+---------------------+
|  Application Logic  |
+---------------------+
           |
           v
+---------------------+
|   Sensor Factory    | <--- Uses `DEVICE_DT_GET` here *only*
+---------------------+
           |
           v
+---------------------+
|  Sensor Adapter     | (Interface: read, init, calibrate)
+---------------------+
     /             \
    v               v
+-------+       +-------+
| Temp  |       | Accel |  (Concrete Adapters wrapping Zephyr devices)
|Adaptr |       |Adaptr |
+-------+       +-------+

============================
Implementation Strategy in C
============================

Since Zephyr applications are written in C, we achieve polymorphism using Function Pointers inside a generic structure.

----------------------------
Define the Adapter Interface
----------------------------

This structure defines what every sensor must be able to do, regardless of whether it's an accelerometer, a temperature sensor, or a simulated test sensor.

`sensor_adapter.h`
.. code-block:: c
    #ifndef SENSOR_ADAPTER_H_
    #define SENSOR_ADAPTER_H_

    #include <zephyr/kernel.h>

    // Forward Declaration : Create a Sensor Adapter Structure that Defines What all Sensors are Capable of Doing
    typedef struct sensor_adapter sensor_adapter_t;

    // Interface Methods
    typedef struct {
        int (*init)(sensor_adapter_t *adapter);
        int (*read)(sensor_adapter_t *adapter, double *out_value);
    } sensor_api_t;

    // Base Adapter Structure
    struct sensor_adapter {
        const sensor_api_t *api;        // Functions that are Supported by a Sensor
        const struct device *zt_device; // Zephyr device binding
        void *private_data;             // [void *] is used to make the Element Configurable : For Sensor-Specific Configurations
    };

    #endif // SENSOR_ADAPTER_H_

---------------------------
Implement Concrete Adapters
---------------------------

Let's implement a specific adapter for a Temperature sensor (e.g., an ambient TEMP sensor defined in your devicetree).

`temp_adapter.c`
.. code-block:: c
    #include "sensor_adapter.h"
    #include <zephyr/drivers/sensor.h>

    /* Initialize a Temperature Sensor */
    static int temp_init(sensor_adapter_t *adapter) {
        if (!device_is_ready(adapter->zt_device)) {
            return -ENODEV;
        }
        return 0;
    }

    /* Function to Read from a Temperature Sensor */
    static int temp_read(sensor_adapter_t *adapter, double *out_value) {
        struct sensor_value val;
        int rc = sensor_sample_fetch(adapter->zt_device);
        if (rc == 0) {
            rc = sensor_channel_get(adapter->zt_device, SENSOR_CHAN_AMBIENT_TEMP, &val);
            if (rc == 0) {
                *out_value = sensor_value_to_double(&val);
            }
        }
        return rc;
    }

    // Map the implementation to the API
    const sensor_api_t temp_sensor_api = {
        .init = temp_init,
        .read = temp_read,
    };

------------------------------
Factory Pattern Implementation
------------------------------

The factory is the only place where Zephyr's devicetree macros (`DEVICE_DT_GET`) live. It isolates the hardware definitions from your business logic.

`sensor_factory.h`
.. code-block:: c

    #ifndef SENSOR_FACTORY_H_
    #define SENSOR_FACTORY_H_

    #include "sensor_adapter.h"

    typedef enum {
        SENSOR_TYPE_AMBIENT_TEMP,
        SENSOR_TYPE_ACCELEROMETER,
        // Add more as needed
    } sensor_type_t;

    // Factory Function to Create a Sensor of type `ensor_type_t type`
    sensor_adapter_t* sensor_factory_create(sensor_type_t type);

    #endif // SENSOR_FACTORY_H_

`sensor_factory.c`
.. code-block:: c

    #include "sensor_factory.h"
    #include <zephyr/device.h>

    // Standard Zephyr devicetree node fetching
    #define TEMP_NODE DT_ALIAS(ambient_temp)
    #define ACCEL_NODE DT_NODELABEL(my_accelerometer)

    extern const sensor_api_t temp_sensor_api;
    extern const sensor_api_t accel_sensor_api; // Assume Implemented Elsewhere

    // Statically allocate the instances to avoid dynamic memory (malloc) in embedded systems
    static sensor_adapter_t temp_adapter_instance = {
        .api = &temp_sensor_api,
        .zt_device = DEVICE_DT_GET(TEMP_NODE),
        .private_data = NULL
    };

    sensor_adapter_t* sensor_factory_create(sensor_type_t type) {
        switch (type) {
            case SENSOR_TYPE_AMBIENT_TEMP:
                return &temp_adapter_instance;
            
            case SENSOR_TYPE_ACCELEROMETER:
                return &accel_adapter_instance;

            default:
                return NULL;
        }
    }

===========================
Using it in the Application
============================

Now, your main application logic doesn't care about devicetree labels, channels, or DEVICE_DT_GET. It just uses the factory and the uniform adapter interface.

`main.c`
.. code-block:: c

    #include <zephyr/kernel.h>
    #include "sensor_factory.h"

    void main(void) {
        // 1. Ask the factory for the Sensor
        sensor_adapter_t *my_sensor = sensor_factory_create(SENSOR_TYPE_AMBIENT_TEMP);
        
        if (!my_sensor) {
            printk("Failed to create sensor adapter\n");
            return;
        }

        // 2. Initialize it using the Abstract API
        if (my_sensor->api->init(my_sensor) != 0) {
            printk("Sensor init failed\n");
            return;
        }

        // 3. Read Data Uniformly
        while (1) {
            double reading = 0.0;
            if (my_sensor->api->read(my_sensor, &reading) == 0) {
                printk("Current Sensor Reading: %.2f\n", reading);
            } else {
                printk("Error reading sensor\n");
            }
            k_sleep(K_MSEC(2000));
        }
    }

=================================
Why This Approach Wins in Zephyr?
=================================

Seamless Mocking & Testing:
 You can introduce a SENSOR_TYPE_MOCK in the factory that returns a simulated software adapter. You can run unit tests on your application logic via native_sim without needing physical target hardware or complex devicetree overlays.

Centralized Hardware Dependencies:
 If a sensor changes from I2C to an ADC-based sensor, only the concrete adapter implementation changes. The application layer remains entirely untouched.

Zero-Cost Polymorphism (Almost):
 Because we use static allocations for the adapter structures (temp_adapter_instance), we completely bypass dynamic heap allocation (malloc), making it safe and deterministic for real-time embedded environments.

##################################
Facade Pattern for System Services
##################################

In Zephyr, the Facade Pattern is highly critical for system services.

Zephyr is incredibly powerful, but its subsystems (like Non-Volatile Storage (NVS), Settings, Flash Circular Buffer (FCB), or File Systems) have complex, verbose, and highly hardware-specific APIs.

If you call Zephyr APIs directly from your business logic, you run into two major roadblocks:

Host-Side Testing (Unit Testing) becomes painful: You cannot easily compile your business logic on a host PC (using Zephyr's native_sim) because the code is tightly coupled to actual target hardware drivers.

High API Boilerplate: Your business logic gets cluttered with initialization checks, mounting sequences, flash sector offsets, and error-handling code.

The Facade Pattern solves this by wrapping complex Zephyr subsystems behind a clean, simplified, and hardware-agnostic API.

1. The Architecture
Instead of your Business Logic or State Machine interacting directly with Zephyr's storage stacks, it talks exclusively to a simplified Storage Facade interface.

       [ Business Logic / State Machine ]
                       |
                       v  (Clean, simple interface: storage_save_config)
             [ STORAGE FACADE C-API ]
                       |
     +-----------------+-----------------+
     | (Target Board Compilation)        | (Host Unit-Test Compilation)
     v                                   v
[ Zephyr NVS / Flash Subsystems ]  [ Mock/In-Memory Implementations ]
     |                                   |
[ Physical Flash / EEPROM ]         [ PC RAM Buffer ]
2. Step-by-Step Implementation: The Storage Facade
Let's build a Storage Facade that handles saving and loading your application configuration.

Step 1: Define the Facade Interface (Header)
This header is completely hardware-agnostic. It contains no Zephyr headers, making it fully mockable on a host PC.

C
// storage_facade.h
#ifndef STORAGE_FACADE_H_
#define STORAGE_FACADE_H_

#include <stdint.h>
#include <stdbool.h>

// Simple application configuration struct
struct app_config {
    float temp_threshold;
    uint16_t sample_interval_ms;
    bool alarm_enabled;
};

/**
 * @brief Initialize the storage subsystem (mount flash, setup files, etc.)
 * @return 0 on success, negative error code on failure.
 */
int storage_init(void);

/**
 * @brief Save the configuration to non-volatile memory.
 */
int storage_save_config(const struct app_config *config);

/**
 * @brief Load the configuration from non-volatile memory.
 */
int storage_load_config(struct app_config *config);

#endif /* STORAGE_FACADE_H_ */
Step 2: Implement the Facade for the Target (Zephyr NVS wrapper)
This is where you hide all of Zephyr's complex boilerplate (defining flash partitions, mounting filesystem structures, mapping IDs, and handling raw byte arrays).

C
// storage_facade_zephyr.c
#include "storage_facade.h"
#include <zephyr/kernel.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>

// 1. Zephyr-specific boilerplate hidden away from the business logic
static struct nvs_fs fs;
#define NVS_PARTITION storage_partition
#define NVS_PARTITION_ID FIXED_PARTITION_ID(NVS_PARTITION)

#define CONFIG_RECORD_ID 1  // NVS key index

static bool is_initialized = false;

int storage_init(void)
{
    if (is_initialized) {
        return 0;
    }

    int rc;
    const struct device *flash_dev = DEVICE_DT_GET(DT_MTD_FROM_FIXED_PARTITION(DT_NODE_BY_FIXED_PARTITION_LABEL(NVS_PARTITION)));

    if (!device_is_ready(flash_dev)) {
        return -ENODEV;
    }

    fs.flash_device = flash_dev;
    fs.offset = FIXED_PARTITION_OFFSET(NVS_PARTITION);
    
    struct flash_pages_info info;
    rc = flash_get_page_info_by_offs(flash_dev, fs.offset, &info);
    if (rc) {
        return rc;
    }

    fs.sector_size = info.size;
    fs.sector_count = FIXED_PARTITION_SIZE(NVS_PARTITION) / info.size;

    rc = nvs_mount(&fs);
    if (rc) {
        return rc;
    }

    is_initialized = true;
    return 0;
}

int storage_save_config(const struct app_config *config)
{
    if (!is_initialized) return -EACCES;
    
    // Hide raw byte-writing API behind our structured object
    ssize_t rc = nvs_write(&fs, CONFIG_RECORD_ID, config, sizeof(struct app_config));
    return (rc < 0) ? (int)rc : 0;
}

int storage_load_config(struct app_config *config)
{
    if (!is_initialized) return -EACCES;

    ssize_t rc = nvs_read(&fs, CONFIG_RECORD_ID, config, sizeof(struct app_config));
    if (rc == -ENOENT) {
        // Fallback to safe defaults if config doesn't exist yet
        config->temp_threshold = 40.0f;
        config->sample_interval_ms = 1000;
        config->alarm_enabled = true;
        return 0;
    }
    
    return (rc < 0) ? (int)rc : 0;
}
Step 3: Implement the Mock Facade (For Host/Unit Testing)
Because of the Facade design, you can write a separate version of the file for your test environment that doesn't rely on physical flash hardware at all.

C
// storage_facade_mock.c
#include "storage_facade.h"
#include <string.h>

// Emulate Flash storage in standard system RAM
static struct app_config ram_flash;
static bool mock_initialized = false;

int storage_init(void) 
{
    mock_initialized = true;
    // Set default test values
    ram_flash.temp_threshold = 35.0f;
    ram_flash.sample_interval_ms = 500;
    ram_flash.alarm_enabled = false;
    return 0;
}

int storage_save_config(const struct app_config *config) 
{
    if (!mock_initialized) return -1;
    memcpy(&ram_flash, config, sizeof(struct app_config));
    return 0;
}

int storage_load_config(struct app_config *config) 
{
    if (!mock_initialized) return -1;
    memcpy(config, &ram_flash, sizeof(struct app_config));
    return 0;
}
In your CMakeLists.txt, compile storage_facade_zephyr.c when building for hardware, and compile storage_facade_mock.c when building for tests.

Step 4: Using the Facade in your Business Logic
Your business logic code is now beautifully clean, highly readable, and free of low-level RTOS boilerplate.

C
#include "storage_facade.h"
#include "biz_logic_sm.h"

void initialize_system(struct app_ctx *ctx)
{
    // Initialize storage facade
    if (storage_init() == 0) {
        struct app_config cfg;
        
        // Load configurations cleanly
        if (storage_load_config(&cfg) == 0) {
            // Apply configurations to business logic context
            ctx->temp_threshold = cfg.temp_threshold;
            printk("System Config loaded successfully!\n");
        }
    }
}
Why this is a Game-Changer in Zephyr
Isolation from Zephyr Upgrades: If Zephyr changes its NVS/Flash API in a future LTS release, you only have to update storage_facade_zephyr.c. The core application business logic remains completely untouched.

Flawless Unit Testing: You can write comprehensive tests for your business logic on your host machine without needing a development board plugged in, reducing test cycles from minutes to milliseconds.

API Simplicity: Developers working strictly on features or user experience do not need to understand device trees, partition layouts, or flash sector dynamics—they just call storage_save_config().

