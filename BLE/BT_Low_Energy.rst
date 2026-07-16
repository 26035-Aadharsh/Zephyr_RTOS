####################################
Introduction to Bluetooth Low Energy
####################################

To construct a comprehensive mental model of BLE within the Zephyr RTOS on an Espressif ESP32 architecture, you must first decouple BLE from classic Bluetooth (Bluetooth [Basic Rate / Enhanced Data Rate], or [BR/EDR]).

---------------------------------
Differences of Classic BT and BLE
---------------------------------

Introduced in the Bluetooth 4.0 specification, BLE was architected from scratch to achieve ultra-low power consumption, enabling devices to operate for months or years on a simple coin-cell battery.

Unlike classic Bluetooth, which creates a continuous, high-throughput streaming connection optimized for audio and large file transfers, BLE operates on a paradigm of rapid, sporadic, low-duty-cycle bursts of small data packets interleaved with prolonged periods of deep sleep.

==================================================
Bluetooth Low Energy : Physical RF Characteristics
==================================================

At the physical RF layer, BLE operates in the `2.4 GHz` Industrial, Scientific, and Medical (ISM) unlicensed radio band, specifically spanning from 2402 MHz to 2480 MHz. This 78 MHz spectrum is carved into 40 distinct RF channels, each separated by a 2 MHz bandwidth.

-------------------------------------------------
Frequency Characteristics of Bluetooth Low Energy
-------------------------------------------------

To robustly combat multipath fading, electromagnetic interference, and co-existence conflicts with ubiquitous Wi-Fi networks operating in the same 2.4 GHz band, BLE utilizes a technique called `Frequency Hopping Spread Spectrum (FHSS)`.

*Note* : FHSS increases network traffic capacity.



The 40 channels are architecturally split into two functional groups:
    a. 3 Advertising Channels (Channels 37, 38, and 39)
    b. 37 Data Channels (Channels 0 through 36).

------------------
Advertising in BLE
------------------

The advertising channels are strategically positioned at the lower, middle, and upper bounds of the 2.4 GHz spectrum (specifically at 2402 MHz, 2426 MHz, and 2480 MHz) to bypass the main transmission peaks of Wi-Fi channels `1`, `6`, and `11`. When a device is broadcasting its presence (advertising), it sequentializes brief packet transmissions across these three channels.

 * Advertisements
 * Scan Request Reception
 * Scan Response Transmission

------------------------
Data Transmission in BLE
------------------------

Once an `active point-to-point connection` is successfully negotiated between two devices, the communication shifts exclusively to the 37 data channels. During a connection, the devices dynamically hop across these 37 channels following a channel selection algorithm and a mutually agreed-upon "Hop Increment," ensuring that if a single channel undergoes severe interference, subsequent packets are shifted to a clean frequency within milliseconds.

#######################
BLE Core : Stack Layers
#######################

The architectural framework of a BLE system is cleanly split into three distinct macro layers:
    1. Controller,
    2. Host, and
    3. Application.

This division isolates hardware-specific radio control from high-level data modeling and business logic, operating over a standardized interface.

+-------------------------------------------------------+
|                     APPLICATION                       |
|         (Business Logic, Profiles, User Code)         |
+-------------------------------------------------------+
|                        HOST                           |
|   +-----------------------------------------------+   |
|   |      GATT (Generic Attribute Profile)         |   |
|   +-----------------------------------------------+   |
|   |      GAP (Generic Access Profile)             |   |
|   +-----------------------------------------------+   |
|   |      SMP (Security Manager Protocol)          |   |
|   +-----------------------------------------------+   |
|   |      ATT (Attribute Protocol)                 |   |
|   +-----------------------------------------------+   |
|   |  L2CAP (Logical Link Control & Adapt. Prot.)  |   |
|   +-----------------------------------------------+   |
+-------------------------------------------------------+
|          HCI (Host Controller Interface)              |
+-------------------------------------------------------+
|                     CONTROLLER                        |
|   +-----------------------------------------------+   |
|   |               LL (Link Layer)                 |   |
|   +-----------------------------------------------+   |
|   |            PHY (Physical Layer)               |   |
|   +-----------------------------------------------+   |
+-------------------------------------------------------+

===============================================================
The Controller Layer [Hardware | Custom Controller Development]
===============================================================

The Controller represents the low-level, timing-critical hardware-software interface that directly modulates the physical silicon radio. It contains two primary components:

--------------
Physical Layer
--------------

 1. The Physical Layer (PHY):
   The `literal radio hardware circuitry` that dictates the analog-to-digital transformations. It implements Gaussian Frequency Shift Keying (GFSK) modulation, where a logical 1 is represented by a positive frequency deviation and a logical 0 by a negative deviation.

  The PHY layer dictates whether the radio operates at : 
    a. 1 Mbps (the mandatory baseline),
    b. 2 Mbps (introduced in Bluetooth 5.0 for higher throughput), or,
    c. LE Coded (which leverages Forward Error Correction to vastly increase communication range at the expense of bit rate).

 **Register Mapping**: The Controller contains hardcoded memory-mapped I/O addresses specific to that exact chip's radio peripheral, timers, and DMA channels.
 **Modulation and Hardware Crypto**: It directly controls the silicon that handles GFSK modulation, hardware whitening, CRC generation, and AES-128 encryption engines built into the chip.
 **Hardware Timers**: BLE requires microsecond-level timing accuracy for connection events. The Controller interacts directly with the chip's specific hardware timers and real-time counters (RTC) to manage these tight windows.

----------
Link Layer
----------

 2. The Link Layer (LL):
   The hard real-time engine of the stack. The Link Layer directly manages packet construction, transmission timing, preamble generation, 24-bit Cyclic Redundancy Check (CRC) calculation, and automated hardware acknowledgments.

   It is governed by a strict state machine defining five fundamental states:
    a. Standby,
    b. Advertising,
    c. Scanning,
    d. Initiating, and,
    e. Connection.
    
    The Link Layer manages the high-precision scheduling windows required to execute connection events, ensuring the radio wakes up, switches frequencies, transmits or listens for precise microsecond durations, and drops back into low-power states seamlessly.

1. Channel Selection Register: The LL calculates which of the 40 channels to use next based on its frequency hopping algorithm. It then writes that channel number into a specific hardware register, which forces the PHY's multiplexers and synthesizers to shift frequencies.
2. Timer Registers: The LL sets up ultra-precise hardware timers (down to the `microsecond`) to trigger the radio to turn on exactly when a connection event is scheduled.
3. DMA Configuration Registers: The LL configures the Direct Memory Access registers so that when the PHY receives raw data from the air, the hardware automatically dumps it directly into a specific RAM buffer.
4. Control Registers: The LL writes to registers that tell the PHY when to switch from transmit (TX) mode to receive (RX) mode within that strict 150-microsecond window.

===============================
Host Controller Interface (HCI)
===============================

The Host Controller Interface (HCI) software in Zephyr is written entirely in C, and the protocol itself is completely hardware-agnostic. It is a standardized communication specification established by the Bluetooth Special Interest Group (SIG).

The Bluetooth Specification describes the format in which a Host must communicate with a Controller. This is called the Host Controller Interface (HCI) protocol. HCI can be implemented over a range of different physical transports like:

 a. UART,
 b. SPI, or
 c. USB.

This protocol defines the commands that a Host can send to a Controller and the events that it can expect in return, and also the format for user and protocol data that needs to go over the air. The HCI ensures that different Host and Controller implementations can communicate in a standard way making it possible to combine Hosts and Controllers from different vendors.

In dual-chip architectures (such as a separate microcontroller connected to a standalone Bluetooth chip via a UART hardware bus), physical HCI packets travel across a physical serial interface. In a monolithic system-on-chip like the ESP32, where both the Controller and the Host execute within different software tasks on the same silicon fabric, the HCI is implemented via a high-speed, `zero-copy software shared-memory virtual interface`.

-------------------------
Single-chip configuration
-------------------------

In this configuration, a single microcontroller implements all three layers and the application itself. This can also be called a system-on-chip (SoC) implementation. In this case the Bluetooth Host and the Bluetooth Controller communicate directly through function calls and queues in RAM. The Bluetooth specification does not specify how HCI is implemented in this single-chip configuration and so how HCI commands, events, and data flows between the two can be implementation-specific. This configuration is well suited for those applications and designs that require a small footprint and the lowest possible power consumption, since everything runs on a single IC.

-----------------------
Dual-chip configuration
-----------------------

This configuration uses two separate ICs, one running the Application and the Host, and a second one with the Controller and the Radio Hardware. This is sometimes also called a connectivity-chip configuration. This configuration allows for a wider variety of combinations of Hosts when using the Zephyr OS as a Controller. Since HCI ensures interoperability among Host and Controller implementations, including of course Zephyr's very own Bluetooth Host and Controller, users of the Zephyr Controller can choose to use whatever Host running on any platform they prefer.

+-----------+------------+
|    Host   | Controller |
+-----------+------------+
|   Zephyr  |   Linux    |
|   Linux   |   Zephyr   |
|   Zephyr  |   Zephyr   | [SoC | Single Chip Configuration]
+-----------+------------+

For example, the host can be the Linux Bluetooth Host stack (`BlueZ`) running on any processor capable of supporting Linux. The Host processor may of course also run Zephyr and the Zephyr OS Bluetooth Host. Conversely, combining an IC running the Zephyr Host with an external Controller that does not run Zephyr is also supported.

Because the Bluetooth SIG standardizes the Host Controller Interface (HCI) protocol, a Zephyr Host treats a pre-flashed external module exactly the same as it would treat its own controller.

.. code-block:: Kconfig
    CONFIG_BT=y
    CONFIG_BT_HOS_ONLY=y         # Tells Zephyr NOT to build its own controller
    CONFIG_BT_HCI=y
    CONFIG_BT_UART=y             # Driver layer to communicate over UART

.. code-block:: dts
    &uart1 {
        status = "okay";
        current-speed = <115200>; /* Match pre-flashed module speed */
        hw-flow-control;

        bt_hci: bluetooth {
            compatible = "zephyr,bt-hci-uart";
        };
    };

If the module connects via SPI instead of UART, you would use `compatible = "zephyr,bt-hci-spi"`;.

When you enable Bluetooth (CONFIG_BT=y), Zephyr’s default behavior on a wireless-capable chip (like a Nordic nRF52/nRF53) is to build a Combined Topology. It compiles both the Host stack (GATT, GAP) and its own Link-Layer Controller stack into your single application binary.

A Bluetooth Controller stack requires a lot of ROM (Flash) and RAM to manage precise radio timing, packet scheduling, and connection states. Because your external BLE module is already handling those tasks, compiling a second controller onto your main MCU is completely redundant. Setting this flag strips that unneeded code out, saving tens of kilobytes of memory.

---------------------------
Standard HCI BLE Interfaces
---------------------------

* H:4 (Standard UART): The most common protocol. It assumes a highly reliable 4-wire UART connection (TX, RX, RTS, CTS).
* H:5 (3-Wire UART): Used if your hardware lacks hardware flow control pins. It adds a reliable packet layer to prevent data corruption.
* SPI: Often used by specific vendors (like STMicroelectronics' BlueNRG) for faster throughput.

-----------
Limitations
-----------

While standard operations (scanning, connecting, advertising) work right out of the box using universal HCI commands, hardware-specific tasks—like changing RF transmit power or entering deep sleep modes—require manufacturer-specific HCI commands.

===============
Host Layer (HL)
===============

The Host layer lives directly above the HCI and consists of a suite of software protocols that structure, secure, and route data streams:

 * L2CAP (Logical Link Control and Adaptation Protocol): This module acts as the fundamental data multiplexer and packet `fragmenter/reassembler` of the Host stack. It takes large data payloads from upper layers and splits them into fragments that conform to the Maximum Transmission Unit (MTU) sizes 27 to 251 Bytes allowed by the Link Layer.
 
 It maps distinct logical channels over the physical connection, routing data specifically to the Attribute Protocol or Security Manager.

       ┌───────────────────────┐       ┌───────────────────────┐
       │   ATT Layer (0x0004)  │       │   SMP Layer (0x0006)  │
       └───────────┬───────────┘       └───────────┬───────────┘
                   │                               │
  =================================┬================================
  |  [ L2CAP ]  Multiplexing & Fragmentation (Handles MTU limits)  |
  =================================┬================================
                                   │
                                   ▼
                       ┌───────────────────────┐
                       │  HCI / Virtual Layer  │
                       └───────────────────────┘

-------------------------------------------------
Inbound Direction (From Radio >> Up to SMP / ATT)
-------------------------------------------------

1. The Byte Stream Arrives: The radio antenna receives raw packets. The Virtual HCI or physical interface dumps this raw stream of bytes into the Host stack.
2. L2CAP Demultiplexes: L2CAP acts as the traffic cop. It strips off the link headers and looks inside the packet at the 2-byte Channel ID (CID).The Queue Split:
   If the CID is 0x0004, L2CAP immediately pushes that specific payload into the ATT Queue/Buffer for database processing.
   If the CID is 0x0006, L2CAP pushes the payload into the SMP Queue/Buffer for cryptographic processing.

----------------------------------------------------
Outbound Direction (From ATT / SMP >> Down to Radio)
----------------------------------------------------

1. L2CAP takes structured payloads from the ATT and SMP queues and converts them into a stream of bytes for the radio.
2. Upper Layers Queue Data: The ATT layer (e.g., sending a battery level update) or the SMP layer (e.g., sending an encryption key) drops a completed protocol data unit (PDU) into L2CAP's outbound queue.
3. L2CAP Adds the Tag: L2CAP prepends its own header, stamping it with the correct Channel ID (0x0004 or 0x0006) so the receiving device will know where it belongs.
4. Fragmentation (Chipping it down): If the ATT payload is 100 bytes long, but the physical radio can only handle 27 bytes at a time (the physical MTU limit), L2CAP chops the payload into smaller fragments.
5. The HCI Byte Stream: It streams these sequential byte fragments down across the Host Controller Interface (HCI) to the radio controller to be beamed into the air.

-----------------------------------------------------
Secure GATT Characteristics Transmission or Broadcast
-----------------------------------------------------

The SMP "Interception"There is one unique architectural nuance to the queues. While ATT and SMP have separate logical channels, SMP actually controls the security of the ATT channel.When the ATT queue tries to push an outbound data packet down to L2CAP, the Security Manager (SMP) intercepts it if that specific GATT data requires encryption. SMP encrypts the payload using the active AES-128 keys before L2CAP packages it and streams it to the controller.

 * ATT (Attribute Protocol): `The structural core of BLE data storage`. ATT defines a simple, optimized stateless database format. Every piece of information on a BLE device is stored as an "Attribute." Each attribute is assigned a unique `16-bit` or `128-bit` Universally Unique Identifier (UUID) defining its type, a 16-bit Handle representing its memory index/address in the database, a Value containing the actual raw data payload, and a specific set of Permissions (Read, Write, Authentication required, Encryption required).

 ┌───────────────────────────────────────────────────────────────────────────┐
 │ Handle (2B)  │  UUID (2B or 16B)  │ Value (Var Len) │  Permissions (1B)   │
 ├───────────────────────────────────────────────────────────────────────────┤
 │    0x0001    │       0x2800       │      0x180F     │      Read Only      │
 └───────────────────────────────────────────────────────────────────────────┘

 * SMP (Security Manager Protocol): `The cryptographic backbone of the stack`. It handles the generation, storage, and distribution of encryption keys. SMP defines the procedures for device pairing, authentication, and bonding (permanently storing keys in non-volatile flash so devices can automatically re-establish secure channels upon subsequent reconnections). It protects against eavesdropping and Man-in-the-Middle (MITM) attacks by leveraging security algorithms like Elliptic Curve Diffie-Hellman (ECDH).

 * GAP (Generic Access Profile): `The structural coordinator of the network`. It manages everything that happens before a connection is established, and handles the high-level connection management. GAP defines the operational roles of the devices and dictates how they discover, advertise, broadcast, and form physical point-to-point connections. It provides the boilerplate configurations for names, appearance characteristics, and connection parameters (such as the minimum and maximum connection intervals).

 * GATT (Generic Attribute Profile): `The hierarchical abstraction layer built directly over the flat ATT database`.
It takes the flat rows of ATT and organizes them into a neat, nested object-oriented tree structure (Services and Characteristics). GATT groups individual attributes into logical containers called Services and Characteristics. A GATT Server is the device that houses the ATT database and exposes data; a GATT Client is the remote device that connects to the server, discovers its services, and reads, writes, or subscribes to its data nodes. `What are GATT : Services and Characteristics?`

The Generic Attribute Profile (GATT) imposes a strict, Object-Oriented hierarchical structural framework over the flat linear space of the ATT database. This hierarchy is composed of Profiles, Services, Characteristics, and Descriptors.

 * **Profile**: A high-level, specification-defined conceptual umbrella. A profile is not a compiled structural entity in code; rather, it is an architectural design document that dictates which combination of services a device must implement to conform to an industry standard (such as the Heart Rate Profile or the Cycling Speed and Cadence Profile).

 * **Service**: A collection of logically related data points and behaviors. For example, the Battery Service (Standard UUID 0x180F) groups all data relevant to tracking a device's power cells. Services can be Primary (representing the fundamental utility of the device) or Secondary (providing auxiliary features).

 * **Characteristic**: The fundamental operational unit of data exposure within a service. It represents a single discrete entity — such as a specific temperature measurement, a control switch state, or a text string. A characteristic is structurally broken down into `three mandatory attributes` inside the ATT database:
   * *Declaration*: An attribute marking the boundary start of a characteristic, defining its structural parameters, property flags (Read, Write, Notify, Indicate), and the UUID of the characteristic value.
   * *Value*: The literal byte array containing the payload data.
   * *Descriptors*: Optional, auxiliary attributes placed directly underneath the Value attribute to provide metadata. For example, a Characteristic User Description Descriptor (0x2901) contains an ASCII string providing a human-readable label for the data point.

+-------------------------------------------------------------+
|                          PROFILE                            |
|  (Conceptual umbrella bundling multiple Services together)  |
+-------------------------------------------------------------+
   |
   +--> SERVICE 1 (UUID: 0x180F - Battery Service)
   |       +--> CHARACTERISTIC A (UUID: 0x2A19 - Battery Level)
   |               +--> VALUE (e.g., 98%)
   |               +--> DESCRIPTOR (UUID: 0x2902 - CCCD)
   +--> SERVICE 2 (UUID: Custom 128-bit Custom Sensor Service)
           +--> CHARACTERISTIC B (UUID: Custom 128-bit Read/Write Data)

-------------------------
Crucial Architecture Note
-------------------------

A GAP Peripheral is almost universally configured to act as a GATT Server (e.g., a smart environmental sensor hosting temperature data), while a GAP Central acts as a GATT Client (e.g., a smartphone reading that data). However, this is not a hard restriction. A GAP Peripheral can structurally act as a GATT Client, or both devices can host GATT Servers simultaneously over a single active connection.

###########################
Advertising, and Connecting
###########################

Advertising is the mechanism by which a device alerts the external world of its existence, status, or intent to connect. During an advertising state, the Link Layer wakes the radio up and transmits an identical advertising payload sequentially over Channels 37, 38, and 39.

The timing of this broadcast is governed by 2 parameters:
    1. Advertising Interval (T_advInterval)
    2. Mandatory pseudo-random Advertising Delay (T_advDelay) ranging from 0 ms to 10 ms.

The inclusion of the random delay is an intentional, low-level architectural protection against persistent packet collisions. If two distinct BLE peripherals were powered on at the exact same microsecond with identical static advertising intervals, their radio bursts would continuously step on each other, causing total mutual signal destruction. The random 0-10 ms shift injected into every single interval ensures that their transmission windows naturally drift apart, preserving network stability.

=================================
Mechanics of an Active Connection
=================================

When a GAP Central detects a connectable advertisement from a target Peripheral, it transitions its Link Layer from a Scanning state to an Initiating state. It transmits a highly specific CONNECT_IND packet precisely inside the listening window immediately following the peripheral's advertisement. This connection indication packet contains the critical synchronization parameters that both devices must adopt to maintain the connection:

 * **Connection Interval**: The precise amount of time between two consecutive communication events where both devices wake up to exchange data. This can range from `7.5 milliseconds` (high throughput, high power) up to `4.0 seconds` (ultra-low power, high latency).
 * **Peripheral Latency**: An optimization integer that defines the number of consecutive connection events that the Peripheral is legally allowed to skip if it has no data to transmit. This allows a peripheral device to remain in deep sleep even when the Central is actively waking up to ping it, dramatically extending battery life.
 * **Supervision Timeout**: The maximum duration of time that can elapse between two successfully received BLE packets before the link layer declares the connection lost or broken, initiating a teardown sequence and alerting the upper application layers.

#################################
Asynchronous Data Push Mechanisms
#################################

When a GATT Client needs data from a GATT Server, it can manually issue a Read Request packet OTA, which requires the server to fetch the attribute value and respond. However, for real-time sensor monitoring, this polling mechanism is highly inefficient, wasting bandwidth and CPU cycles. To optimize this, GATT provides two asynchronous, server-driven data push mechanisms:

---------------------------
Notifications (GATT_NOTIFY)
---------------------------

When the underlying sensor data changes on the server, the server instantly builds an unacknowledged Link Layer packet containing the updated byte payload and transmits it directly to the client. The server does not pause its execution thread to wait for a reply; it fire-and-forget transmits the packet. This offers maximum data throughput and minimal execution overhead.

---------------------------
Indications (GATT_INDICATE)
---------------------------

Indications operate on an explicit transaction acknowledgment mechanism at the application layer. When the server pushes an indication packet, it halts subsequent data transmissions for that specific attribute and blocks until it receives an explicit `GATT_CON_ACK` (Confirmation) packet back from the remote GATT Client. If the client fails to respond within a 30-second window, the local stack instantly declares a protocol timeout and tears down the physical link layer connection. Indications ensure delivery verification but limit maximum transmission speeds.

To enable either notifications or indications, a GATT Client must explicitly subscribe to them by manipulating a specialized, standardized descriptor attached to the target characteristic called the **Client Characteristic Configuration Descriptor** (CCCD), identified globally by the 16-bit UUID 0x2902.

The `CCCD` is a 2-byte bitmask attribute.

1. 0x0001 in CCCD informs the GATT Server that it is legally authorized to push asynchronous Notifications.
2. 0x0002 enables Indications,
3. 0x0000 completely silences the characteristic's server-driven channels.

========================================
ESP32 Controller and Zephyr Architecture
========================================

When executing Zephyr RTOS on an Espressif ESP32 DevKit C target, the compilation and runtime operational pipeline of the BLE stack diverges drastically from typical ARM Cortex-M microcontrollers. To master this platform, you must understand exactly how the hardware architecture maps to Zephyr's modular drivers.

-----------------------------------------------
Dual-Core Execution and the Controller Topology
-----------------------------------------------

The ESP32 features a Tensilica Xtensa Dual-Core LX6 micro-architecture consisting of two symmetric cores: `PRO_CPU` (Protocol CPU) and `APP_CPU` (Application CPU). When Zephyr boots on the ESP32, it runs as a unified operating system kernel managing threads across this hardware.

Unlike Nordic Semiconductor chips where the entire BLE Host and Controller can compile natively into a single open-source firmware binary (Zephyr's open-source Link Layer), the ESP32 contains highly proprietary, closed-source RF radio timing components. Espressif isolates these intellectual properties inside a pre-compiled digital signal processing binary module known as the `ESP-IDF Bluetooth Controller Component / Wireless HAL`.

In a standard Zephyr compilation for the ESP32, the open-source Zephyr BLE Host stack executes within standard Zephyr cooperative/preemptive threads. Below this, Zephyr hooks into the lower-level Espressif hardware abstraction layer (`modules/hal/espressif`). The proprietary, low-level Link Layer and Physical RF drivers run directly on the silicon hardware, often pinning high-priority real-time radio interrupt handlers directly to the physical PRO_CPU core to guarantee microsecond-level timing accuracy for connection windows. Communication between the open-source Zephyr Host and this binary Espressif Controller is routed over an internal virtualized software HCI driver (drivers/bluetooth/hci/hci_esp32.c).

###########################
Bluetooth Low Energy Beacon
###########################

Project Folder Path : `samples/bluetooth/beacon`
It is the absolute simplest Bluetooth sample in the codebase. It implements a non-connectable broadcaster (a BLE Beacon) that transmits minimal advertising packets. This lets you learn the core API setup without getting bogged down by complex connection tracking or GATT databases.

`samples/bluetooth/beacon/prj.conf`
.. code-block:: Kconfig
    CONFIG_BT=y
    CONFIG_LOG=y
    CONFIG_BT_DEVICE_NAME="Test beacon"

`samples/bluetooth/beacon/prj-coex.conf`
.. code-block:: Kconfig
    CONFIG_BT_LL_SW_SPLIT=y
    CONFIG_BT_CTLR_COEX_TICKER=y

*prj-coex.conf and prj.conf must be built together if Wi-Fi or other protocols that may sit on the 2.4GHz Freq. are enabled to ensure packet loss due to attempt of simultaneous antenna access.*

The Ticker Engine functions as a dynamic, event-driven scheduler that programs a hardware timer to fire precisely at the absolute timestamp of the next scheduled Bluetooth or CoEx event. When an advertising interval matures, the timer triggers an interrupt, and the Ticker immediately dispatches execution to the Lower Link Layer (LLL). Operating within a high-priority ISR context, the LLL takes immediate control of the time slot to drive the physical radio hardware.

Simultaneously, the Upper Link Layer (ULL) operates asynchronously in a background cooperative thread, completely decoupled from these real-time microsecond constraints. Its role is to perform all predictive protocol calculations ahead of time — such as defining the channel-mapping parameters, configuring peripheral settings, and staging the assembled payload PDU in memory. By keeping this state data pre-compiled and ready, the ULL ensures the LLL can instantly consume it the exact moment the dynamic Ticker alarm sounds.

TIME ───►  |  -500 us (Pre-Slot) |  0 us (Slot Starts)  |  +100 us  |  +250 us  |  +400 us (Slot Ends) | Post-Slot |
===========|=====================|======================|===========|===========|======================|===========|
TICKER     |                     | [X] Fires Interrupt  |           |           |                      |           |
-----------|---------------------|----------------------|-----------|-----------|----------------------|-----------|
ULL        | [████████████████]  |                      |           |           |                      | [███████] |
(Context)  | Prepares BLE packet |                      |           |           |                      | Evaluates |
           | & checks channel map|                      |           |           |                      | TX success|
-----------|---------------------|----------------------|-----------|-----------|----------------------|-----------|
LLL        |                     | [██████████████████] | [███████] | [███████] |                      |           |
(Highest   |                     | Radio Setup          | Radio TX  | Radio RX  |                      |           |
Priority)  |                     | (Frequencies/Power)  | (Data)    | (ACK)     |                      |           |

-------------------
Kconfig Integration
-------------------

To compile the BLE subsystem into your Zephyr application binary, you must explicitly enable a chain of cascading Kconfig options within your project's prj.conf file. Each configuration activates distinct layers of the software compilation tree:

.. code-block:: Kconfig
    # Step 1: Enable the global hardware networking and Bluetooth subsystems
    CONFIG_NET_BUF=y
    CONFIG_BLUETOOTH=y

    # Step 2: Inform Zephyr to compile the high-level Host layer code (GAP/GATT/ATT)
    CONFIG_BT_HOST=y

    # Step 3: Configure the stack to operate as a peripheral device capable of advertising
    CONFIG_BT_PERIPHERAL=y

    # Step 4: Map the underlying HCI driver to hook directly into the Espressif binary hardware controller
    CONFIG_BT_ESP32=y

    # Step 5: Define the device's human-readable advertising broadcast name
    CONFIG_BT_DEVICE_NAME="ESP32_BLE_Node"

------------------------------
Architectural C Implementation
------------------------------

The following complete compilation unit outlines the exact, low-level structural code layout required to initialize the BLE subsystem, statically instantiate a custom multi-attribute GATT database, configure the GAP advertising parameters, and activate the radio framework on an ESP32 target running Zephyr RTOS.

.. code-block:: c
    #include <zephyr/types.h>
    #include <stddef.h>
    #include <string.h>
    #include <zephyr/sys/printk.h>
    #include <zephyr/sys/util.h>
    #include <zephyr/kernel.h>

    /* Include the core Bluetooth Subsystem APIs */
    #include <zephyr/bluetooth/bluetooth.h>
    #include <zephyr/bluetooth/hci.h>
    #include <zephyr/bluetooth/conn.h>
    #include <zephyr/bluetooth/uuid.h>
    #include <zephyr/bluetooth/gatt.h>

    /**
    * ============================================================================
    * 1. CRYPTOGRAPHIC UUID DEFINITIONS (128-bit Custom Layouts)
    * ============================================================================
    * BLE requires unique identifiers. Standard 16-bit UUIDs are reserved by the
    * Bluetooth SIG. For proprietary custom hardware extensions, we define reverse
    * byte-order 128-bit arrays using the BT_UUID_128_ENCODE macro.
    */
    #define CUSTOM_SERVICE_UUID_VAL \
        BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)
    #define CUSTOM_CHAR_UUID_VAL \
        BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1)

    static struct bt_uuid_128 custom_service_uuid = BT_UUID_INIT_128(CUSTOM_SERVICE_UUID_VAL);
    static struct bt_uuid_128 custom_char_uuid = BT_UUID_INIT_128(CUSTOM_CHAR_UUID_VAL);

/**
 * ============================================================================
 * 2. ATTRIBUTE DATA STORAGE AND CCCD STATE TRACKING
 * ============================================================================
 */
static uint8_t telemetry_payload_buffer[4] = {0x00, 0x00, 0x00, 0x00};
static uint8_t cccd_configuration_state = 0;

/**
 * @brief GATT Callback triggered whenever a remote Client issues an over-the-air Read Request.
 * * @param conn   Opaque pointer to the active connection structure instance.
 * @param attr   Pointer to the specific structural attribute element being read from flash/RAM.
 * @param buf    Pointer to the destination buffer where extracted bytes must be copied.
 * @param len    The maximum number of bytes requested by the client's packet constraint.
 * @param offset The byte index offset requested if reading a fragmented long attribute.
 * * @return int   Returns the total number of bytes successfully read, or a negative error code.
 */
static ssize_t read_telemetry_characteristic(struct bt_conn *conn,
                                            const struct bt_gatt_attr *attr,
                                            void *buf, uint16_t len, uint16_t offset)
{
    printk("[GATT] Read request received targeting handle index: %u\n", attr->handle);
    
    /* Abstractly reference the raw memory pointer assigned to the attribute configuration struct */
    return bt_gatt_attr_read(conn, attr, buf, len, offset, 
                             telemetry_payload_buffer, sizeof(telemetry_payload_buffer));
}

/**
 * @brief GATT Callback triggered whenever a remote Client issues an over-the-air Write Request.
 */
static ssize_t write_control_characteristic(struct bt_conn *conn,
                                           const struct bt_gatt_attr *attr,
                                           const void *buf, uint16_t len, uint16_t offset,
                                           uint8_t flags)
{
    printk("[GATT] Write request received containing raw byte payload length: %u\n", len);

    if (offset + len > sizeof(telemetry_payload_buffer)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    /* Copy incoming byte arrays directly into the application space data container */
    memcpy(telemetry_payload_buffer + offset, buf, len);
    
    printk("[GATT] State updated internally to: 0x%02X\n", telemetry_payload_buffer[0]);
    return len;
}

/**
 * @brief GATT Callback triggered whenever a Client alters the CCCD state configuration bitmask.
 */
static void cccd_configuration_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    cccd_configuration_state = (value == BT_GATT_CCC_NOTIFY);
    printk("[GATT] CCCD State Modification. Asynchronous Notifications Enabled: %s\n",
           cccd_configuration_state ? "TRUE" : "FALSE");
}

/**
 * ============================================================================
 * 3. STATIC GATT LINKER EXPANSION DATABASE (The Macro Tree)
 * ============================================================================
 * The BT_GATT_SERVICE_DEFINE macro performs static compilation allocation.
 * It passes parameters straight into a dedicated linker section area, instructing
 * the build engine to auto-generate the ATT database entries cleanly without
 * dynamic runtime runtime memory allocations.
 */
BT_GATT_SERVICE_DEFINE(custom_telemetry_service,
    /* 1. Root Level Service Attribute Declaration */
    BT_GATT_PRIMARY_SERVICE(&custom_service_uuid),

    /* 2. Characteristic Wrapper Definition pairing properties, read/write hooks, and UUID descriptors */
    BT_GATT_CHARACTERISTIC(&custom_char_uuid.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           read_telemetry_characteristic, 
                           write_control_characteristic, 
                           telemetry_payload_buffer),

    /* 3. Client Characteristic Configuration Descriptor (CCCD) providing notification control tracking */
    BT_GATT_CCC(cccd_configuration_changed, 
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
);

/**
 * ============================================================================
 * 4. GAP NETWORKING STATE PARAMENTERS AND CALL INTERFACES
 * ============================================================================
 */
static const struct bt_data advertising_packets[] = {
    /* Flag parameter defining the radio configuration discovery modes */
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    /* Broadcast complete local machine string name mapped into the primary payload */
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, (sizeof(CONFIG_BT_DEVICE_NAME) - 1)),
};

static const struct bt_data scan_response_packets[] = {
    /* Expose the custom 128-bit Service UUID to scanning machines in the neighborhood */
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, CUSTOM_SERVICE_UUID_VAL),
};

static void on_device_connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        printk("[GAP] Connection establishment sequence failure abort. Code: %u\n", err);
        return;
    }
    printk("[GAP] High-Speed Connection link established successfully.\n");
}

static void on_device_disconnected(struct bt_conn *conn, uint8_t reason)
{
    printk("[GAP] Physical link dropped or lost. Tearing down structures. Reason Code: 0x%02X\n", reason);
}

/* Statically bind the core link lifecycle structures directly to the host supervisor array */
BT_CONN_CB_DEFINE(gap_connection_callbacks) = {
    .connected = on_device_connected,
    .disconnected = on_device_disconnected,
};

/**
 * ============================================================================
 * 5. MAIN SUPERVISORY APPLICATION ENTRY THREAD
 * ============================================================================
 */
int main(void)
{
    int error_tracking_variable;

    printk("[SYSTEM] Initiating ESP32 DevKit C BLE Stack Architecture...\n");

    /* Step 1: Initialize the internal physical and logical layers of the stack */
    error_tracking_variable = bt_enable(NULL);
    if (error_tracking_variable) {
        printk("[FATAL] Subsystem boot failure. Code execution halted: %d\n", error_tracking_variable);
        return error_tracking_variable;
    }

    printk("[SYSTEM] Host Controller Interface linked successfully. Booting GAP Advertisements...\n");

    /* Step 2: Push Advertising configurations straight down to the Link Layer */
    error_tracking_variable = bt_le_adv_start(BT_LE_ADV_CONN_NAME, 
                                             advertising_packets, ARRAY_SIZE(advertising_packets),
                                             scan_response_packets, ARRAY_SIZE(scan_response_packets));
    if (error_tracking_variable) {
        printk("[FATAL] Radio broadcast injection failed. Code: %d\n", error_tracking_variable);
        return error_tracking_variable;
    }

    printk("[GAP] Advertising channels 37, 38, and 39 are active.\n");

    /* Step 3: Enter the primary asynchronous application processing loop */
    uint32_t rolling_counter = 0;
    while (1) {
        k_sleep(K_MSEC(1000));
        rolling_counter++;

        /* Pack the incrementing state variables directly into the mapped data region */
        sys_put_le32(rolling_counter, telemetry_payload_buffer);

        /* If a remote machine has set the CCCD bits to high, fire notifications asynchronously */
        if (cccd_configuration_state) {
            /* * Passing a NULL connection pointer instructs the Zephyr Host stack
             * to distribute this notification to all connected clients globally.
             */
            bt_gatt_notify(NULL, &custom_telemetry_service.attrs[1], 
                           telemetry_payload_buffer, sizeof(telemetry_payload_buffer));
        }
    }
    return 0;
}

1. Compilation and Linker Mechanics: Memory Layout Breakdown
To complete your low-level technical understanding, we must trace exactly what happens inside the toolchain when compiling the code block above using Zephyr's macro frameworks.
When you declare BT_GATT_SERVICE_DEFINE(custom_telemetry_service, ...), the build system entirely avoids runtime heap fragmentation. Under the hood, this macro leverages GCC compiler attributes (__attribute__((__section__("." STRINGIFY(name))))) to inject the generated database structures into dedicated, contiguous memory arrays within the final executable ELF binary file.

+-----------------------------------------------------------------------+
|                       ESP32 MEMORY BLOCK MAP                          |
+-----------------------------------------------------------------------+
|  FLASH / ENCRYPTED SPI DATA ROM (.rodata / .text)                     |
|  - Raw Attribute Struct Templates                                     |
|  - Static Callback Tables                                             |
+-----------------------------------------------------------------------+
|  INTERNAL SRAM (.data / .bss)                                        |
|  - Real-time Mutable Data Buffers (telemetry_payload_buffer)           |
|  - State Control Variables (cccd_configuration_state)                 |
+-----------------------------------------------------------------------+

1. Read-Only Flash Flash / ROM Blocks: The metadata structures describing each attribute—including the pointers to your custom read_telemetry_characteristic and write_control_characteristic callback functions, the literal 128-bit array values of the custom UUID constants, and the baseline structural permissions—are cleanly packed into read-only linker segments. On the ESP32, these regions live inside external flash and are fetched on demand through the SoC's flash cache hardware MMU map.
2. Mutable Internal RAM Blocks: The actual real-time variable structures—specifically your 4-byte telemetry_payload_buffer and the cccd_configuration_state variable—are placed inside the standard, high-speed internal SRAM data segments (.data or .bss).

When bt_enable() is invoked at boot, the Host stack processes these static linker sections to build a lightweight, logical runtime database matrix. When an over-the-air packet reaches the ESP32 radio antenna, the underlying Espressif Link Layer processes the packet, passes it up via the virtual HCI software queue to the Zephyr Host, and the GATT engine walks this linker-mapped structure. If the transaction passes validation, the engine securely routes control directly to your application callbacks, completing the path from the radio silicon to your C code.