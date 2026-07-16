#include <zephyr/devicetree.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include <zephyr/drivers/sensor.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include <zephyr/bluetooth/services/bas.h>

/**
 * @brief Code to Read from an ADXL345 Accelerometer using a ESP32 in Triggered Mode.
 * TRIGGER occurs after each Data Ready Event.
 * 
 */

uint8_t battery_level = 100U;
/* 
SECTION 01 : Initialization of Variables
    1. Get Sensor from DT and Create Space for Sensor Readings
    2. Initialize BLE ad[] Advertisment Data
*/
static const struct device *const accelerometer = DEVICE_DT_GET(DT_ALIAS(accelerometer));
struct sensor_value sensor_reading[3];

/* Create a Sensor Trigger */
struct sensor_trigger trig = {
    .type = SENSOR_TRIG_DATA_READY,
    .chan = SENSOR_CHAN_ACCEL_XYZ,
};

/* LL Packet Data Data for BLE Advertisment and Scan Response */
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
		        BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
/* Accelerometer is Published as Environment Sensing ESS*/
#if defined(CONFIG_BT_EXT_ADV)
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
#endif /* CONFIG_BT_EXT_ADV */
};

#if !defined(CONFIG_BT_EXT_ADV)
static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};
#endif /* !CONFIG_BT_EXT_ADV */

/*
sensor accelerometer_notify function's signature is strictly defined by : 
`zephyr/drivers/sensor.h`
*/
static void accelerometer_notify(
    const struct device *dev,
    const struct sensor_trigger *trigger)
{
    /* Read from an Accelerometer and Publish its Readings */
    if (sensor_sample_fetch(dev) < 0) {
        printk("Sensor sample fetch failed\n");
        return;
    }

    sensor_channel_get(dev, SENSOR_CHAN_ACCEL_XYZ, sensor_reading);

    /*
    Set a Battery Level :
        Accelerometer is not a Standard GATT Service and Must be Cutomized
        (bt_conn*) is set as NULL to advertise to all Connections
        */
    bt_bas_set_battery_level(battery_level--);

    if (battery_level < 0) {
        battery_level = 100U;
    }

    /* Display Acceleration */
    printk("Acceleration [\n\tx: %d.%06d,\ty: %d.%06d,\tz: %d.%06d]\n",
        sensor_reading[0].val1, sensor_reading[0].val2,
        sensor_reading[1].val1, sensor_reading[1].val2,
        sensor_reading[2].val1, sensor_reading[2].val2);
}

/* Establish and Terminate a BT LE Connection */
void connected(struct bt_conn *conn, uint8_t err)
{
    /* Establish a BT LE Connection */
    if (err) {
        printk("Connection Couldn't be Established\n");
    } else {
        printk("Connection Established Successfully ...\n");
    }
}

void disconnected(struct bt_conn *conn, uint8_t err)
{
    /* Terminate a BT LE Connection */
    printk("Disconnected BT LE Device\n");
}

/* Register a Connection and Disconnection CB to the Call Back List */
BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static int bt_ready()
{
    /* Begin Advertisment of BLE */
	int err = bt_le_adv_start(
                        BT_LE_ADV_CONN_FAST_1,
                        ad, ARRAY_SIZE(ad),
                        sd, ARRAY_SIZE(sd));
	if (err) {
		printk("Advertising Failed to Start (ERROR : %d)\n", err);
		return -EIO;
	}
	printk("Advertising Successfully Started\n");
    return 0;
}


int main()
{
    int err;

    /* Check if Device is Ready */
    if (!device_is_ready(accelerometer)) {
        printk("Accelerometer Not Initialized.\n");
        return -ENODEV;
    }
    printk("Accelerometer Initialization Status : SUCCESS\n");

    /* register or set trigger using Zephyr's sensor API */
    int ret = sensor_trigger_set(accelerometer, &trig, accelerometer_notify);
    if (ret < 0) {
        printk("Failed to set trigger: %d\n", ret);
        return ret;
    }

    /* Advertise BT Data */
    printk ("BLE Sensor Data Advertisment ...\n");

    /* Enable BLE Synchronously */
    err = bt_enable(NULL);
    if (err) {
        printk ("Bluetooth Initialization Failed (ERR %d)\n", err);
        return -EIO;
    }
    printk ("Bluetooth Initialization Success\n");

    /* Start BLE Advertisment */
    bt_ready();

    /* Loop Forever : Keep Main Thread Alive*/
    while(1) {
        printk("Main Thread is Alive!\n");
        k_msleep(1000);     /* Yield : Allow Trigger Function to RUN */
    }

    return 0;
}
