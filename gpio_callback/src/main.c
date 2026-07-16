#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/**
 * @brief Commands to Build and Flash
 * 
 * Build
 * @code
 * west build -p always -b esp32_devkitc/esp32/procpu -- -DEXTRA_DTC_OVERLAYS='boards\\esp32_devkitc_procpu.overlay'
 * @endcode
 * 
 * Flash
 * @code
 * west flash
 * @endcond
 * 
 * Serial Monitor
 * @code
 * python -m serial.tools.miniterm COM7 115200
 * @endcode
 * 
 * `Limitations`
 * The interrupt controller is not turned off after a press is detected, and turned back on after the button is released. Due to bounce effects of the tactile button, multiple interrupts may be detected in a single long press.
 * Fix
 *  1. capacitor to RC filter ripples
 *  2. lock and release NVIC for button to establish safe behavior;
 * 
 * Circuit
 * 1. ESP32 DevKit C
 * 2. Extenally Pulled Up GPIO to G16
 * 3. LED + (220 Ohm Resistor in Series) to G17
 */

static struct gpio_callback button_cb_data;

/* Parse Device Tree */
static struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(external_led), gpios);
static struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(DT_ALIAS(interrupt_btn), gpios);

/* Define the Callback Handler Function : Function Signature is TypeDef */
void button_pressed_handler(
    const struct device *port,
    struct gpio_callback *cb,
    uint32_t pins)
{
    printk("Interrupt Triggered! Pin Bitmask that Fired: 0x%08X\n", pins);
    
    /* Place your LED toggle logic here */
    printk("Toggling LED...\n");
    gpio_pin_toggle_dt(&led);
}

int main()
{
    if (!gpio_is_ready_dt(&led)) {
        printk("Error: GPIO LED is not Ready\n");
        return -EIO; /* Return an Input/Output error code */
    }

    if (!gpio_is_ready_dt(&btn)) {
        printk("Error: GPIO Button is not Ready\n");
        return -EIO; /* Return an Input/Output error code */
    }

    int ret;
    ret = gpio_pin_interrupt_configure_dt(&btn, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0) {
        printk("Error: Failed to configure hardware interrupt triggers (%d)\n", ret);
        return ret;
    }

    printk("GPIO Devices are Ready for Use\n");

    gpio_init_callback(
        &button_cb_data,
        button_pressed_handler,
        BIT(btn.pin));
    
    ret = gpio_add_callback(btn.port, &button_cb_data);
    if (ret != 0) {
        printk("Error: Failed to Register Callback Structure (Error Code: %d)\n", ret);
        return ret;
    }

    while(1) {
        printk("Main Thread is, Alive!\n");
        k_msleep(1000);
    }

    return 0;
}