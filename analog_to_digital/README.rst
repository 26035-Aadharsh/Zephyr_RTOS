##############################################################
Architectural and Physical Foundations of ADC on the **ESP32**
##############################################################

To construct an accurate mental model of Analog-to-Digital Conversion (ADC) within the Espressif ESP32 system-on-chip (SoC) hosted on an ESP32 DevKit C, you must first trace the hardware execution pipeline at the silicon layer. The ESP32 integrates two independent Successive Approximation Register (SAR) ADC modules, designated as **ADC1** and **ADC2**.

The SAR architecture operates on a binary-search principle: for every analog sample, an internal sample-and-hold circuit captures the incoming voltage trace, and a comparator evaluates this voltage against an internal Digital-to-Analog Converter (DAC) feedback ladder. Bit by bit, from the Most Significant Bit (MSB) down to the Least Significant Bit (LSB), the SAR logic refines its digital approximation until the internal voltage matches the sampled analog input.

============================================
Silicon Modules and Pin Mapping Restrictions
============================================

 * **ADC1:** Manages 8 physical channels mapped sequentially across Channels 0 through 7.
 * **ADC2:** Manages 10 physical channels dispersed across Channels 0 through 9.

When engineering applications with an RTOS, a severe hardware resource conflict arises concerning **ADC2** its internal silicon blocks share digital routing infrastructure with the ESP32's integrated Wi-Fi and Bluetooth baseband hardware subsystems. Consequently, if your Zephyr application starts the wireless network stack such as initializing a Wi-Fi connection or executing a Bluetooth Low Energy scan, the hardware arbiters lock out software access to ADC2. Any attempt to sample from an ADC2 channel while the wireless module is active will return a driver execution error or corrupt values.

Therefore, physical hardware designs requiring continuous analog sampling alongside wireless data transmission must restrict their analog targets strictly to **ADC1** (GPIO32–GPIO39).

-----------------------------------------------------
Attenuation, Reference Voltage, and Full-Scale Ranges
-----------------------------------------------------

A naked, un-attenuated SAR ADC cell inside the ESP32 silicon has a baseline structural input voltage range capped between 0V and an internal reference voltage V Ref, which sits at an unbuffered nominal target of approximately 1.1V. To read voltages exceeding this 1.1V barrier without destroying the internal gates, the ESP32 integrates an analog routing multiplexer preceding each SAR block. This multiplexer passes the incoming signal through a programmable resistor-ladder voltage divider, a feature designated as **Hardware Attenuation**.

Dis-Advantages of Oversampling
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

`Verdict : Increasing the FSR - Full Scale Range decreases the Sensitivity and Decreasing the FSR increases the Sensitivity of the ADC. An ADC is the most sensitive in its 0dB Attenuation.`

The configuration of this attenuation directly determines your full-scale input measurement threshold and shapes the input impedance profile of the pin. The hardware supports four distinct attenuation steps:
 1. **0dB Attenuation:** The resistor network is bypassed. The maximum full-scale input range is 0 to 1.1V. Input signals must never exceed this range, making it ideal for low-voltage sensor interfaces.
 2. **2.5dB Attenuation:** The input signal is attenuated by a factor of approximately 1.33. This expands the readable full-scale input voltage window from 0 up to roughly 1.5V.
 3. **6dB Attenuation:** The input signal is divided by a factor of 2. This scales the structural full-scale measurement window from 0 to 2.2V.
 4. **11dB Attenuation:** The input signal is divided by a factor of approximately 3.54. This expands the analog reading window from 0 up to a maximum ceiling of 3.9V. However, since the ESP32 DevKit C operates on a principal digital supply rail V_DD of 3.3V, the absolute physical voltage reading is capped at 3.3V. Voltages higher than 3.3V on a pin will saturate the reading or damage the pad.

----------------------------------------
Non-Linearity and Silicon Non-Idealities
----------------------------------------

The ESP32's SAR controllers are notorious for distinct physical non-idealities, primarily **Integral Non-Linearity (INL)** and **Differential Non-Linearity (DNL)**. The transfer curve mapping input voltage to digital code ticks is not perfectly uniform.

 * Near the bottom of the scale (close to 0V), the input transistors exhibit a dead-zone, failing to register minute voltage shifts until the threshold clears approximately 0.1V.
 * Conversely, as the input voltage approaches the upper limit of the configured attenuation ceiling (e.g., nearing 3.3V at 11dB attenuation), the curve exhibits severe compression and non-linear saturation.

SAR ADC Non-Linearity Compensation
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

To counteract this distortion, manufacturers burn chip-specific calibration parameters into the ESP32’s **eFuse (electrically programmable fuse)** memory banks during factory testing. These fuses store individual offset variables and specific reference voltage deltas (V_ref variations caused by manufacturing imperfections). Zephyr’s lower-level driver reads these eFuse values during early system boot sequences to run mathematical polynomial corrections on raw digital outputs.

============================================================
Oversampling and Decimation Mechanics: Hardware vs. Software
============================================================

Oversampling is a signal processing technique where a system samples an analog signal at a frequency significantly higher than the Nyquist rate (twice the highest frequency component present in the signal). In basic firmware applications, oversampling is used to accomplish two distinct goals:

1. filtering out high-frequency stochastic white noise, and
2. artificially extending the **Effective Number of Bits (ENOB)** or digital resolution of the converter beyond its native physical constraint.

The ESP32 hardware does not integrate a dedicated independent synchronous hardware oversampling block capable of autonomous multi-sample averaging inside the SAR register itself. In the Zephyr driver model for the ESP32, oversampling is orchestrated through sequential sampling routines. When you configure an oversampling factor of 2^n (e.g., an oversampling setting of 4, 16, or 64), the underlying runtime layer orchestrates a multi-sample execution loop. It triggers successive conversions in rapid succession, aggregates the accumulated digital tick outputs into an accumulation register, and processes the arithmetic average.

.. code-block:: markdown
    Sampling Rate must be increased 2^(2n) times for every 1-bit gain in resolution of ADC, i.e. for each additional bit of resolution, the ADC must be sampled by a factor of 4

    Ex:
        1. For a 4-bit increase in resolution, sample the signal 4^(n) = 4 ^ (4) = 256 times instead of once for A2D conversion
        2. actual sampled signal increased 256 times, so the number of increase in bits in log2(256) = 8
        3. 4 of these bits are noisy and are discarded, therefore, << 4 and eliminate 4 noisy bits and obtain a 4-bit increase in the ADC resolution.

    By accumulating 4^n samples and right-shifting the accumulated sum by n, you lower the noise floor via statistical averaging. This adds n bits of resolution, provided the input signal contains a minimum amount of white noise to dither the LSB step transitions.

======================================
Zephyr RTOS Hardware Abstraction Layer
======================================

Zephyr enforces a complete separation of hardware topology from application source code using a build-time configuration blueprint called the **Devicetree**. Instead of hardcoding physical registers, base memory addresses, or GPIO pin offsets directly into your C application, the physical topology of the ESP32's ADC is declared inside a hierarchical network of hardware files (`.dts` and `.overlay`).

----------------------------------------
Devicetree Node Construction for the ADC
----------------------------------------

In the default Zephyr SoC definition files for the ESP32 family, the primary silicon ADC engines are declared as system nodes labeled adc1 and adc2. Due to zero-based indexing conventions inside Zephyr's DTS compiler framework, physical ADC1 maps to the device node handle &adc1. To configure individual channels on this controller without modifying base system files, you build an application-specific overlay file named esp32_devkitc_procpu.overlay.
Inside this overlay file, you reference the target ADC node by its phandle (&adc1), activate its status flag, define the configuration cells, and create child nodes that explicitly map out each targeted analog channel. The following structural block illustrates this layout:

.. code-block:: dts
    / {
        aliases {
            potentiometer = &adc_channel_0;
        };

        zephyr,user {
            io-channels = <&adc1 0>;
        };
    };

    &adc1 {
        status = "okay";
        #address-cells = <1>;
        #size-cells = <0>;

        adc_channel_0: channel@0 {
            reg = <0>;
            zephyr,gain = "ADC_GAIN_1_4";
            zephyr,reference = "ADC_REF_INTERNAL";
            zephyr,acquisition-time = <ADC_ACQ_TIME_DEFAULT>;
        };
    };

===========================
Detailed Property Breakdown
===========================

 * #address-cells = <1>; and #size-cells = <0>;These properties inform the Devicetree compiler that any sub-nodes nestled within this block represent indexed sub-components rather than memory-mapped regions. Every child node requires a single-integer identifier (reg) and does not possess a memory size footprint.

 * reg = <0>; This parameter dictates the physical hardware channel index of the underlying ADC controller. On &adc1, a reg value of 0 binds this node explicitly to Channel 0, which corresponds to physical pin **GPIO36** (SENSOR_VP) on the ESP32 DevKit C board.

 * zephyr,gain = "ADC_GAIN_1_4"; This abstract token characterizes the input scaling factor. Because Zephyr is a cross-platform operating system, it maps target-specific characteristics to generic parameters. On Espressif hardware, this property sets the analog hardware attenuation cells. The mapping translates as follows:

   * "ADC_GAIN_1" maps directly to **0dB Attenuation** (Full Scale: 1.1V).
   * "ADC_GAIN_5_6" maps directly to **2.5dB Attenuation** (Full Scale: 1.5V).
   * "ADC_GAIN_1_2" maps directly to **6dB Attenuation** (Full Scale: 2.2V).
   * "ADC_GAIN_1_4" maps directly to **11dB Attenuation** (Full Scale: 3.3V).

 * `zephyr,reference = "ADC_REF_INTERNAL";` Directs the driver to utilize the internal reference generation infrastructure of the SoC (V_ref approx 1.1V) rather than routing an external reference voltage vector through a dedicated reference pin.

###########################################################
Device Tree Syntax : phandle-arrays and Binding Definitions
###########################################################

General DTS Binding and DTS for phandle arrays

.. code-block:: yml
    (Provider Binding)
    properties:
        compatible = "vndr,dev"
        "#foo-cells":
            type: int
            required: true
        foo-cells:
        - a
        - b
        - c
    

.. code-block:: dts
    // provider node
        "#foo-cells" = <N>
    // consumer node : this is obtained by replacing '#*-cells' using '*s'
        foos = <&provider value_a value_b value_c>

-----------
zephyr,user
-----------

The `/zephyr,user`` node in Zephyr is a special case in the devicetree that allows users to store arbitrary properties and retrieve their values without writing bindings. It is designed for sample code and user applications, making it a convenient container for simple properties. The macro ZEPHYR_USER_NODE expands refers to the zephyr,user node.

.. code-block:: dts
    #include <zephyr/dt-bindings/gpio/gpio.h>

    / {
        zephyr,user {
                signal-gpios = <&gpio0 1 GPIO_ACTIVE_HIGH>;
        };
    };

You can convert the pin defined in signal-gpios to a struct gpio_dt_spec in your source code, then use it like this:

.. code-block:: C
    #include <zephyr/drivers/gpio.h>

    #define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

    const struct gpio_dt_spec signal =
            GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, signal_gpios);

-----------------------------------------
Structure of Bindings and pHandles in DTS
-----------------------------------------

ADC expose a phande as defined by the bindings file, for instance, a ESP ADC exposes a io-channels phandle, that can be used to reference the ADC.

.. code-block:: yaml
    compatible: "espressif,esp32-adc"
    properties:
        "#io-channel-cells":
            const: 1

    io-channel-cells:
    - input

Mapping Container **zephyr,user**. The top-level block creates a bridge for application-space convenience.
`Why?`

.. code-block:: dts
    /* adc consumer node */
    / {
        zephyr,user {
            io-channels = <&adc1 0>;
            };
        };
    };


By defining the io-channels phandle-array property inside this generic node, the build system converts this hardware topology into static macros. This allows your source code to fetch a clean, hardware-agnostic specification package (`struct adc_dt_spec`) without hardcoding node paths.

#########################################
The Zephyr Application API Code Mechanics
#########################################

Once the Devicetree has been processed by pre-build Python scripts, it generates compile-time macros inside the header layer (devicetree_generated.h). Your application software consumes these definitions by interfacing with the generic driver framework declared in `<zephyr/drivers/adc.h>`.

-------------------------------------------
Structural Typology: Data Tracking Elements
-------------------------------------------

To manage an ADC transaction, your application interacts with two operational layers:
    * the **Channel Specification Layout** (defining *how* the channel behaves) and
    * the **Sequence Sampling Descriptor** (defining *where* and *how much* data is read).

+---------------------------------------------------------+
|                  struct adc_sequence                    |
|  +---------------------------------------------------+  |
|  | .channels     = BIT(0)                            |  |
|  | .buffer       = (void *)raw_buffer_array          |  |
|  | .buffer_size  = sizeof(raw_buffer_array)          |  |
|  | .resolution   = 12                                |  |
|  | .oversampling = 4                                 |  |
|  +---------------------------------------------------+  |
+---------------------------------------------------------+
                            |
           Dispatches to Low-Level Controller
                            v
+---------------------------------------------------------+
|                struct adc_channel_cfg                   |
|  +---------------------------------------------------+  |
|  | .channel_id       = 0                             |  |
|  | .gain             = ADC_GAIN_1_4 (11 dB Atten)    |  |
|  | .reference        = ADC_REF_INTERNAL              |  |
|  | .acquisition_time = ADC_ACQ_TIME_DEFAULT          |  |
|  +---------------------------------------------------+  |
+---------------------------------------------------------+

`zephyr,acquisition-time = <ADC_ACQ_TIME_DEFAULT>;` Dictates the sampling window duration—the exact timeframe that the input sample-and-hold capacitor network remains physically connected to the external input pin. Setting this to ADC_ACQ_TIME_DEFAULT allows the Espressif driver to utilize the factory-default charge integration tracking windows optimized for standard low-impedance signals.

The primary tracking structures include:
 1. **struct adc_dt_spec** This structure bundles compile-time hardware configuration data into a single application handle. It contains:
   1. const struct device *dev: A pointer targeting the compiled runtime device structure of the root controller instance (&adc1).
   2. uint8_t channel_id: The hardware index tracking number extracted from the reg property (e.g., 0).
   3. `struct adc_channel_cfg channel_cfg`: A sub-structure predefined with the structural attributes mapped out in the channel sub-node (gain, reference, acquisition_time).

 2. **struct adc_channel_cfg** If you bypass the automatic Devicetree spec retrieval macro to configure channels dynamically, you must manually populate this structure. It explicitly tracks channel_id, gain, reference, and acquisition_time using the standard macro definitions.

 3. **struct adc_sequence** This core structure defines the runtime constraints for a specific sampling operation. The driver engine parses this structure every time a conversion is requested:

   * uint32_t channels: A precise bitmask tracking which channels must be sampled during this specific request sequence. To sample channel 0, this must be assigned to BIT(0). If you are executing a multi-channel scan across channels 0 and 3, this bitmask would be configured as BIT(0) | BIT(3). [Sequence Sampling | Scan Sampling, Parallel Sampling]
   * void *buffer: A pointer targeting a raw memory allocation array (typically an array of int16_t or uint16_t) where the raw conversion ticks will be sequentially deposited by the driver. Results of the ADC are deposited as a queue as defined in the sampling queue as an array.
  
  .. code-block:: c
        /*
         * Multi-Channel Sampling using a Sampling Queue.
         */
        void sample_multiple_channels(void) {
            // Array size must match the number of bits set in the mask
            uint16_t result_array[2] = {0}; 
            adc_request_t config;

            // Configure for both Channel 0 and Channel 3
            config.channels = BIT(0) | BIT(3); 
            config.buffer = result_array;

            // After execution:
            // result_array[0] holds the raw data from Channel 0
            // result_array[1] holds the raw data from Channel 3
            // adc_execute_transfer(&config);
        }

   * size_t buffer_size: The physical allocation footprint of your destination buffer array, specified strictly in bytes. If your array contains 4 elements of type int16_t, this must be set to `4 * sizeof(int16_t)`.
   * uint8_t resolution: The target bit-depth resolution requested for the final output word. For the ESP32 chip, this field can be assigned to 9, 10, 11, or 12 bits. Specifying a resolution of 12 returns a range spanning from 0 to 4095 digital ticks.
   * uint8_t oversampling: Configures the averaging multiplier layer. In the Zephyr environment, you pass the raw multiplier parameter here. Valid values include 0 (disabling the filter), or any power-of-two value spanning up to 8 (representing an 2^8 = 256 times oversampling ratio).

##########################################
Step-by-Step Low-Level Execution Lifecycle
##########################################

To successfully coordinate an analog capture sequence, your code must execute a structured lifecycle loop. This process spans compile-time verification, hardware initialization, transaction processing, and mathematical conversion.

==================================================
Step 1: Static Instantiation and Handle Generation
==================================================

First, your application uses the standard macro wrappers to statically extract the metadata structured within the Devicetree rules.

.. code-block:: c
    #include <zephyr/kernel.h>
    #include <zephyr/drivers/adc.h>

    /* Extract the structural specification using the node label macro matching the alias */
    static const struct adc_dt_spec adc_chan0 = ADC_DT_SPEC_GET(DT_ALIAS(potentiometer));

The macro `ADC_DT_SPEC_GET` automatically navigates the compile-time metadata structures, instantiates a persistent struct adc_dt_spec, extracts the device object pointer tracking &adc1, copies out the channel tracking ID 0, and extracts the pre-configured attenuation structures.

=============================================
Step 2: Runtime Initialization and Validation
=============================================

Before querying physical register lines, your application must confirm that the underlying software driver has initialized successfully.

.. code-block:: c
    int init_adc_subsystem(void)
    {
        int ret;

        /* Verify that the base device controller is compiled and ready for operation */
        if (!device_is_ready(adc_chan0.dev)) {
            printk("Error: Underlying ADC hardware device node is not ready.\n");
            return -ENODEV;
        }

        /* Initialize the internal registers for this channel using the macro-unpacked specification */
        ret = adc_channel_setup_dt(&adc_chan0);
        if (ret < 0) {
            printk("Error: Failed to configure ADC channel registers (Error: %d)\n", ret);
            return ret;
        }

        return 0;
    }

When `adc_channel_setup_dt` executes, it drops into the internal implementation file (`drivers/adc/adc_esp32.c`). The driver extracts the physical channel number from the parameter block, calls low-level Espressif HAL primitives to link that physical pin to the SAR peripheral block, clears any prior pin multiplexer definitions, and writes the attenuation configuration into the hardware registers.

=============================================================
Step 3: Structuring the Sample Sequence and Buffer Allocation
=============================================================
To execute a physical measurement sequence, you must allocate a storage buffer and prepare the runtime control parameters tracking the transaction.

.. code-block:: c
    #define BUFFER_SIZE 1
    static int16_t sample_buffer[BUFFER_SIZE];

    int read_analog_sample(int32_t *out_mv)
    {
        int ret;

        /* Define the specific sequence execution block */
        struct adc_sequence sequence = {
            .channels    = BIT(adc_chan0.channel_id), /* Select target bit mask via ID */
            .buffer      = sample_buffer,             /* Point to our destination array */
            .buffer_size = sizeof(sample_buffer),     /* Inform driver of byte boundary */
            .resolution  = 12,                        /* Configure 12-bit native resolution */
            .oversampling = 4,                        /* Request a 16x oversampling factor */
        };

        ... (rest of the logic)
    }

Here, setting .oversampling = 4 tells the underlying driver to sample the analog pin multiple times. The driver averages these readings before storing the final result in sample_buffer[0].

====================================
Step 4: Dispatching the Read Routine
====================================

With the sequence parameter block fully populated, you dispatch the transaction to the synchronous execution engine.

.. code-block:: c
    /* Dispatch the sequence configuration block to the generic driver core */
    ret = adc_read(adc_chan0.dev, &sequence);
    if (ret < 0) {
        printk("Error: ADC read sequence transaction failed (Error: %d)\n", ret);
        return ret;
    }

    /* Extract the raw digitizing ticks generated by the hardware transaction */
    int16_t raw_ticks = sample_buffer[0];

The execution flow of adc_read() follows a strict pipeline:
 1. The function takes control of the thread and locks an internal device mutex to prevent other execution threads from intercepting the hardware registers.
 2. It programs the target resolution and triggers the sampling sequence.
 3. Because this is a synchronous blocking read operation, the calling thread is temporarily descheduled or spins in a low-overhead wait loop while the internal SAR logic finishes its successive-approximation comparisons.
 4. Once the hardware updates its data registers, an interrupt or status flag wakes the driver, which unloads the digital values, processes any requested oversampling corrections, writes the results into your local sample_buffer, and releases the device mutex.

========================================================================
Step 5: Applying Calibration Coefficients to Convert Ticks to Millivolts
========================================================================
 
A raw tick integer value like 2748 does not tell your high-level application code the actual physical voltage present on the pin. To transform this raw digital representation into a precise physical voltage value (expressed in millivolts), you must pass the raw value through Zephyr's internal calibration and scaling math engine.
 
.. code-block:: c
    int32_t mv_value = (int32_t)raw_ticks;
    
    /* Process raw digital ticks into millivolts, factoring in attenuation and eFuse offsets */
    ret = adc_raw_to_millivolts_dt(&adc_chan0, &mv_value);
    
    if (ret < 0) {
        printk("Error: Voltage calibration conversion failed (Error: %d)\n", ret);
        return ret;
    }
    
    *out_mv = mv_value;
    return 0;
    }
 
 
The API helper adc_raw_to_millivolts_dt reads the hardware attributes embedded inside your adc_dt_spec handle. It looks up the active gain attenuation (e.g., ADC_GAIN_1_4), retrieves the structural reference voltage attributes, checks the device's internal calibration tables, and computes the slope and offset equations:
 
This math is refined using the unique calibration constants read from the chip's internal eFuses, correcting for any inherent manufacturing variances. The resulting value is written back to the memory address pointed to by mv_value. This completes the transition from raw physical analog voltage to verified, calibrated digital information inside your real-time application thread.
 
##########
References
##########

To trace the structural definitions of these data structures in your local Zephyr workspace, examine the following source file locations:
 
 1. **zephyr/include/zephyr/drivers/adc.h** This core header file contains the public, application-facing APIs, structural definitions for struct adc_sequence, struct adc_channel_cfg, and struct adc_dt_spec, as well as macro expansions like ADC_DT_SPEC_GET.
 
 2. **zephyr/drivers/adc/adc_esp32.c** The underlying low-level driver implementation code managed by Espressif for Zephyr. Review this file to observe how adc_context structures are handled, how the adc_read sequence unpacks the channel bitmasks, and how the internal attenuation configurations map directly to the ESP-IDF base HAL registers.
 
 3. **zephyr/dts/bindings/adc/espressif,esp32-adc.yaml** The formal Devicetree binding template file that outlines the validation properties for the ESP32 ADC nodes, explicitly documenting the permitted strings for zephyr,gain and tracking reference cells.
 
