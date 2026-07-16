#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>

const struct device *uart_device = DEVICE_DT_GET(DT_ALIAS(ctrl_uart));

void uart_cb(const struct device *dev, void *user_data)
{
    /**
     * @brief There are 3 Types of UART Interrupt Polling Registers.
     * Different architectures handle the UART ISR request differently.
     * 
     * Automatic : If the ACK is set to 1, MCU automatically clears the flag, therefore, the `uart_irq_update` copies the ACK flag's status to the RAM and caches it. A copy of the ACKs status is preserved in cache before it is cleared by the susequent UART FIFO call.
     * Explicit : If the ACK is set to 1, MCU does not automatically clears the flag, therefore, the `uart_irq_update` copies the ACK flag's status to the RAM and caches it. A copy of the ACKs status is preserved in cache before it is cleared by the `uart_irq_update` function to prevent infinite ISR.
     * Implicit : ACK is computed by circuitry in real-time and is accessed through the ACK register. `uart_irq_update` does nothing!
     */
    uart_irq_update(dev);

    if (uart_irq_rx_ready(dev)) {
        uint8_t buffer[64];
        int recv_len = uart_fifo_read(dev, buffer, sizeof(buffer));

        if (recv_len > 0) {
            for (int i = 0; i < recv_len; i++) {
                printk("%c\t", buffer[i]);
            }
            printk("\n");
        } else {
            printk("No data Rx...\n");
        }
    }

    uint8_t user_in[64] = "Hello UART\n";
    if (uart_irq_tx_ready(dev)) {
        int tx_len = uart_fifo_fill(dev, user_in, sizeof(user_in));

        if (tx_len > 0) {
            for (int i = 0; i < tx_len; i++) {
                printk("%c\t", user_in[i]);
            }
        }
        uart_irq_tx_disable(dev);
    }
}

int main()
{
    if (!device_is_ready(uart_device)){
        printk("UART Device **&uart0** NOT Ready\n");
        return -ENXIO;
    }
    /**
     * @brief Interrupt Mode : ESP32 DevKit C does not have a DMA and can not perform True Async UART.
     * Interrupts can be used instead!
     * 
     * Current CB function has no User Data.
     */
    uart_irq_callback_user_data_set(uart_device, uart_cb, NULL);
    uart_irq_tx_enable(uart_device);
    uart_irq_rx_enable(uart_device);
    while(1) {
        k_sleep(K_FOREVER);
    }
    return 0;
}