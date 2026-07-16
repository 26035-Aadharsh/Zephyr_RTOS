#include <zephyr/kernel.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#include <zephyr/drivers/sensor.h>

#define SENSOR_CHANNEL 0

const struct device *accelerometer = DEVICE_DT_GET(DT_ALIAS(accelerometer));

int main()
{
    struct sensor_value accel_data[3];

    if (!device_is_ready(accelerometer)) {
        printk("Accelerometer is Not Ready!\n");
        return -1;
    }

    int res;
    while(1) {
        res = sensor_sample_fetch(accelerometer);
        if (res == 0) {
            printk("Fetched Sensor Result!\n");
        } else {
            printk("Failure to Fetch from Accelerometer!\n");
            return res;
        }

        sensor_channel_get(accelerometer,
			SENSOR_CHAN_ACCEL_XYZ,
			accel_data);
            
            printk("\n----------------SENSOR DATA----------------\n");
            printk("Acceleration X: %d.%06d m/s^2\n", accel_data->val1, -(accel_data->val2));
            printk("Acceleration Y: %d.%06d m/s^2\n",
                +(accel_data + 1)->val1,
                -(accel_data + 1)->val2);
            printk("Acceleration Z: %d.%06d m/s^2\n",
                +(accel_data + 2)->val1,
                -(accel_data + 2)->val2);
            k_msleep(1000); /* yield for low priorty tasks */
    }

    return 0;
}
