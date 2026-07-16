============================
Bluetooth Low Energy KConfig
============================

https://developerhelp.microchip.com/xwiki/bin/view/applications/ble/introduction/bluetooth-architecture/bluetooth-controller-layer/bluetooth-link-layer/Packet-Types/

--------------------
Types of BLE Packets
--------------------

There are seven advertising channel PDU types, each having a different payload format and function:

Advertising PDUs
    1. ADV_IND,
    2. ADV_DIRECT_IND,
    3. ADV_NONCONN_IND,
    4. ADV_SCAN_IND

+---------------------------+-------------------------------------------+
|   ADV_IND	Connectable     |   Undirected Advertising                  |
|   ADV_DIRECT_IND          |   Connectable Directed Advertising        |
|   ADV_NONCONN_IND         |   Non-Connectable Undirected Advertising  |
|   ADV_SCAN_IND            |   Scannable Undirected Advertising        |
+---------------------------+-------------------------------------------+

Scanning PDUs
    5. SCAN_REQ,
    6. SCAN_RSP
Initiating PDUs
    7. CONNECT_REQ

---------------------
Advertisement Packets
---------------------

Advertising channel PDUs serve two purposes:
 * Broadcast data for applications that do not require a full connection.
 * Discover Slaves and connect to them.

BLE divides UUIDs into two types based on who created them and how much memory they take up in an advertising packet:
 - 16-bit UUIDs (Official / Standardized):
    Size: 2 bytes (e.g., 0xAAFE for Eddystone, 0x180D for a Heart Rate Service).
    Purpose: Reserved strictly by the Bluetooth SIG (Special Interest Group) for public, well-known standards.
    How it works: They are shortened versions of a master "Bluetooth Base UUID": 0000XXXX-0000-1000-8000-00805F9B34FB. The 16 bits simply replace the XXXX.
 - 128-bit UUIDs (Custom / Vendor-Specific):
    Size: 16 bytes (e.g., 123e4567-e89b-12d3-a456-426614174000).
    Purpose: Used for custom hardware or proprietary apps. Anyone can generate a random 128-bit UUID online. Because it is massive, the mathematical probability of two companies generating the same ID is virtually zero.

-------------------------------------------------
Logical Link Layer Protocol Packet Format for BLE
-------------------------------------------------

The payload consists of Several Advertisement Data Structures with the following format:
 * AD Length (1 byte)
 * AD Type (1 byte)
 * AD Data (up to 29 bytes)

============================================
Scan Response : Extended ADV for Classic BLE
============================================

A Scan Response (specifically SCAN_RSP) is a secondary, optional advertising packet provided by a Bluetooth Low Energy (BLE) peripheral in direct response to a scan request (SCAN_REQ) from a scanning device.It acts as an on-demand data extension, allowing a device to provide additional details without cluttering its primary advertisement space.

-------------------
Key Characteristics
-------------------

 * Doubles Payload Capacity: Legacy BLE advertisements are capped at 31 bytes. The scan response provides an additional 31 bytes, allowing for 62 bytes of total broadcast data.
 * On-Demand Only: The peripheral only transmits this packet when a nearby scanner explicitly asks for it. If no scanners are around, it is never sent, saving battery power.
 * Non-Connectable: A scan response packet cannot be used to initiate a connection; it strictly delivers information.

----------------------------
How It Works (The Handshake)
----------------------------

 * Advertising: Peripheral broadcasts a scannable packet (like ADV_IND or ADV_SCAN_IND).
 * Listening: A scanner hears the advertisement and wants more details.
 * Request: The scanner sends a active SCAN_REQ packet.
 * Response: The peripheral immediately replies with the SCAN_RSP packet.

-----------
Common Uses
-----------

Manufacturers typically split information between the primary advertisement and the scan response:
 1. Primary Advertisement (ADV_IND): Critical data needed instantly, such as flags, vital sensor readings, or proximity UUIDs (e.g., iBeacon payload).
 2. Scan Response (SCAN_RSP): Low-priority or static data, such as the full human-readable Local Name of the device, TX Power levels, or secondary 128-bit Service UUIDs.

================================================================
BLE Beacon : GAP Non-Connectable Advertisement `ADV_NONCONN_IND`
================================================================

An application demonstrating the GAP Broadcaster functionality by advertising
an Eddystone URL for the Zephyr Project's website.

`$(PROJ)/prj.conf`
.. code-block:: Kconfig
    CONFIG_BT=y
    CONFIG_LOG=y
    CONFIG_BT_DEVICE_NAME="Test beacon"

CONFIG_BT enables the core Bluetooth subsystem: source files, compiler
definitions, and static memory pools required to compile and run BT
functionality.

CONFIG_LOG enables Zephyr's logging subsystem. Note this is independent of
printk() — the sample uses printk() directly for output, so CONFIG_LOG here
is mainly present because the BT subsystem itself emits log messages
(errors, warnings) through the logging backend, not through printk.

CONFIG_BT_DEVICE_NAME sets the string returned by bt_get_name() and used
below as DEVICE_NAME — the value placed into the scan response's
BT_DATA_NAME_COMPLETE structure.


`$(PROJ)/src/main.c`
.. code-block:: C

    #include <zephyr/types.h>
    #include <stddef.h>
    #include <zephyr/sys/printk.h>
    #include <zephyr/sys/util.h>

-------
Library
-------

zephyr/types.h    : Zephyr's fixed-width integer typedefs (legacy header;
                     modern code typically pulls these from <stdint.h>
                     directly, but kept here for backward compatibility).
stddef.h          : Standard C header, provides size_t — used below for
                     the `count` variable passed to bt_id_get().
sys/printk.h      : Zephyr's lightweight kernel-safe print function,
                     printk() — usable from ISR/early-boot context where
                     libc printf may not be, at the cost of fewer format
                     specifiers.
sys/util.h        : Provides utility macros, notably ARRAY_SIZE() used
                     twice below to compute array element counts at
                     compile time.

.. code-block:: c
    #include <zephyr/bluetooth/bluetooth.h>

Exposes high-level GAP commands (scan, adv enable/disable, bt_enable,
bt_id_get, etc.) and core BT type definitions (bt_addr_le_t, bt_data,
advertising param macros).

.. code-block:: c
    #include <zephyr/bluetooth/hci.h>

Host Controller Interface layer definitions — HCI command/event opcodes,
error codes, and low-level packet structures. Included here mainly for
completeness/error-code visibility; this sample doesn't call HCI functions
directly, as bluetooth.h's GAP layer abstracts over it.

.. code-block:: c
    /**
     * @brief CONFIG_BT_DEVICE_NAME is obtained from the prj.conf
     * Used as Scan Response
     */
    #define DEVICE_NAME CONFIG_BT_DEVICE_NAME
    #define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

DEVICE_NAME aliases the Kconfig string literal for local use.
DEVICE_NAME_LEN subtracts 1 from sizeof() to exclude the C string's
null terminator — the AD structure's data_len field must reflect only
the actual name bytes transmitted over the air, not the terminator,
which has no meaning in the advertising payload.

.. code-block:: c
    /*
     * Set Advertisement data. Based on the Eddystone specification:
     * https://github.com/google/eddystone/blob/master/protocol-specification.md
     * https://github.com/google/eddystone/tree/master/eddystone-url
     */
    static const struct bt_data ad[] = {
        BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
        BT_DATA_BYTES(BT_DATA_UUID16_ALL, 0xaa, 0xfe),
        BT_DATA_BYTES(BT_DATA_SVC_DATA16,
                      0xaa, 0xfe, /* Eddystone UUID */
                      0x10, /* Eddystone-URL frame type */
                      0x00, /* Calibrated Tx power at 0m */
                      0x00, /* URL Scheme Prefix http://www. */
                      'z', 'e', 'p', 'h', 'y', 'r',
                      'p', 'r', 'o', 'j', 'e', 'c', 't',
                      0x08) /* .org */
    };

The primary advertising payload (max 31 bytes, legacy advertising), sent
in the ADV_IND/ADV_NONCONN_IND PDU. Three AD structures, evaluated by a
scanner in increasing order of parse cost:

  ad[0] — Flags (type 0x01): declares BLE-only, no BR/EDR support.
  ad[1] — UUID16 List (type 0x03): advertises service UUID 0xFEAA
          (Eddystone), little-endian byte order (0xaa, 0xfe). Lets an
          Eddystone-aware scanner cheaply pre-filter before decoding
          Service Data.
  ad[2] — Service Data 16-bit (type 0x16): the actual Eddystone-URL
          frame — repeats the 0xFEAA UUID, then frame type 0x10
          (Eddystone-URL, vs. 0x00 for UID or 0x20 for TLM), calibrated
          Tx power, URL scheme prefix byte (0x00 = "http://www."),
          the URL body as raw ASCII, and a trailing expansion byte
          (0x08 = ".org") per the Eddystone-URL encoding table.

.. code-block:: c
    /* Set Scan Response Data : Name of the Device */
    static const struct bt_data sd[] = {
        BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
    };

A second, independent 31-byte payload, sent only in reply to an active
SCAN_REQ from a scanner — not broadcast passively like ad[]. Carries the
human-readable device name, kept separate from ad[] so it doesn't compete
with the Eddystone URL for the primary advertisement's limited byte budget.

.. code-block:: c
    /**
     * @brief callback function for BLE enable
     * ...
     */
    static void bt_ready(int err)
    {

Registered via bt_enable(bt_ready) in main(). Not called directly by
application code — invoked asynchronously by the Bluetooth host once
HCI/controller initialization (HCI reset, feature exchange, identity
address setup) completes. `err` is supplied by the stack, reporting the
outcome of that initialization: 0 on success, a negative errno-style
value on failure.

Correction to your inline doc comment: "after the MCU initializes the BT
Stack using internal hardware-specific driver code" overstates what's
guaranteed — bt_ready fires after Zephyr's HOST-side init sequence
completes, which includes an HCI handshake with the controller, but the
controller itself may be a separate chip over UART/SPI, Zephyr's own
software link layer, or a vendor SoftDevice; "hardware-specific driver
code" isn't uniformly what's being awaited across all these cases.

.. code-block:: c
        bt_addr_le_t addr = {0};
        size_t count = 1;

        if (err) {
            printk("Bluetooth init failed (err %d)\n", err);
            return;
        }

        printk("Bluetooth initialized\n");

        /* Start advertising */
        err = bt_le_adv_start(BT_LE_ADV_NCONN_IDENTITY, ad, ARRAY_SIZE(ad),
                              sd, ARRAY_SIZE(sd));

bt_le_adv_start() begins advertising using the given parameters and both
payloads. BT_LE_ADV_NCONN_IDENTITY: non-connectable, and explicitly uses
the device's real identity address rather than a rotating/private one
(see below). ARRAY_SIZE(ad)/ARRAY_SIZE(sd) computed at compile time via
sys/util.h, avoiding a hand-counted, error-prone element count.

Correction to your inline doc comment: the claim that "a BLE device
exposes its real hardware identity directly via the Link Layer Packet
Header, specifically in the TxAdd bit" is imprecise. The TxAdd bit in the
advertising PDU header only indicates the *type* of address present
(public vs. random) — it is a 1-bit flag, not a carrier of the address
itself. The actual address bytes are carried separately in the PDU's
AdvA field. TxAdd tells a receiver how to interpret AdvA, not what
identity is being exposed.

.. code-block:: c
        if (err) {
            printk("Advertising failed to start (err %d)\n", err);
            return;
        }

        /* For connectable advertising you would use
         * bt_le_oob_get_local(). ...
         */
        bt_id_get(&addr, &count);

Retrieves the local identity address for logging purposes only — valid
here specifically because _IDENTITY advertising was used, so the address
returned matches what's actually on-air. count=1 in/count=N out: caps
how many entries bt_id_get() may write into addr, and is overwritten
with the number of identities actually configured on the device (which
may be more than 1 if multiple BT identities are provisioned, though
this sample only reads the first).

.. code-block:: c
        printk("Beacon started, advertising as %s\n", bt_addr_le_str(&addr));
    }

bt_addr_le_str() formats the bt_addr_le_t struct into a human-readable
string (e.g. "XX:XX:XX:XX:XX:XX (public)") for the printk above.

.. code-block:: c
    int main(void)
    {
        int err;

        printk("Starting Beacon Demo\n");

        /*
         * Initialize the Bluetooth Subsystem (async).
         * bt_ready() is invoked by the stack once HCI/controller
         * init completes, not immediately — err reflects init result.
         */
        err = bt_enable(bt_ready);

Kicks off async BT host/controller initialization and registers bt_ready
as the completion callback. The err returned here reports only whether
the *request* to begin initialization was accepted (e.g. rejects with
-EALREADY if BT is already enabled) — not whether initialization itself
ultimately succeeds; that outcome arrives later, via bt_ready's own err
parameter.

.. code-block:: c
        if (err) {
            printk("Bluetooth init failed (err %d)\n", err);
        }
        return 0;
    }

main() returns immediately after issuing bt_enable() — all further
beacon setup (starting advertising, printing the address) happens
asynchronously inside `bt_ready()` once the stack calls back.
