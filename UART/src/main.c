#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/uart.h>

const struct device *const uart_device = DEVICE_DT_GET(DT_ALIAS(ctrl_uart));

#define RX_BUF_SIZE 64
static uint8_t rx_buf_a[RX_BUF_SIZE];
static uint8_t rx_buf_b[RX_BUF_SIZE];

#define TX_BUF_SIZE 64
static uint8_t tx_buf[TX_BUF_SIZE];

/**
 * @brief Register a CB Function for a UART Interrupt.
 * 
 * Callback function signature is derived from
 *      `typedef void (*uart_callback_t)(const struct device *dev,
                            struct uart_event *evt, void *user_data);`
 * in `zephyr/drivers/uart.h`
 * 
 * @param dev           UART device to be registered in a Callback function
 * @param evt           
 * @param user_data     
 */
static void uart_cb(const struct device *dev, struct uart_event *evt, void *user_data)
{
    /**
     * @brief uart_event is the event that caused a UART interrupt. It could be of various types, namely :
     * 	struct uart_event_tx;
		struct uart_event_rx;
		struct uart_event_rx_buf;
		struct uart_event_rx_stop;
     * refer, `zephyr/drivers/uart.h` and grep "union" for more information...
     */
    switch (evt->type) {
        case UART_RX_RDY:
            /* #UART_RX_RDY Event Data. Enclose in {} to Prevent Compiler Errors */
            {
                struct uart_event_rx rx = evt->data.rx;
                printk(
                    "SUCCESS: Async UART Signal Rx.\n Received [%d Bytes]\n.",
                    rx.len);
                
                printk("Async Data : \n");
                for (int i = rx.offset; i < rx.offset + rx.len; i++) {
                    printk("\t%c", rx.buf[i]);
                }
                printk("\n");
            }
            break;
        
        case UART_TX_DONE:
            printk("SUCCESS: Tx!\n");
            break;

        case UART_RX_BUF_REQUEST:
            /**
             * @brief 
             * The Buffer Chain FlowThe Request:
             *  1. When the hardware starts filling the current buffer, Zephyr triggers UART_RX_BUF_REQUEST ahead of time to ask you for the next one (i.e. a alternate buffer).
             * The Happy Path: You call `uart_rx_buf_rsp()` inside that event. The driver links the new buffer. When the first buffer is completely full (`UART_RX_RDY`), the hardware immediately switches to your next buffer without losing a single byte.
             * The Failure Path: If you ignore the request or fail to provide a buffer in time, the hardware runs out of space. Since it has nowhere left to write incoming data, it immediately stops the receiver and triggers `UART_RX_DISABLED`.
             */
            {
                struct uart_event_rx_buf rx = evt->data.rx_buf;

                uint8_t *next_buf = rx_buf_a;
                if (rx.buf == rx_buf_a) {
                    next_buf = rx_buf_b;
                } 

                int err = uart_rx_buf_rsp(dev, next_buf, RX_BUF_SIZE);
                if (err) {
                    printk("failed to chain next ping-pong buffer: %d\n", err);
                } else {
                    printk("next buffer chained successfully.\n");
                }
            }
            break;

        case UART_RX_DISABLED:
            /**
             * @brief Enable UART if disabled for continual data byte reception.
             * UART_RX_DISABLED occurs if user buffer is filled.
             * @code
             * {
                    int err = uart_rx_enable(uart_device, rx_buf, sizeof(rx_buf), 50000);
                    if (err) {
                        printk("Failed to Enable Rx: %d\n", err);
                    } else {
                        printk("UART Rx Enabled.\n");
                    }
                }
             * @endcode
             * Disadvantages : Using a single buffer for UART reception in Zephyr introduces a high risk of data loss and corruption due to a fundamental race condition during buffer processing. When the single buffer fills up or hits a timeout, the Direct Memory Access (DMA) hardware triggers an interrupt and pauses data reception to hand the buffer over to the CPU. Because there is no secondary, back-up memory space immediately available for the hardware to swap into, any new bytes arriving over the serial line during this precise handoff window will overwrite existing unread data or be dropped entirely by the hardware. This architecture forces a strict, unrealistic dependency where the CPU must instantly process or copy the received data before the next hardware byte arrives, making single-buffered systems highly unreliable for continuous, high-speed, or unpredictable data streams.
             * Altenaives : `Dual-Buffer Architecture` or `Ping-Pong Buffer`
             */
            printk("Disabled UART Rx..");
            {
                /* Enable 1st Buffer for UART Tx */
                int err = uart_rx_enable(uart_device, rx_buf_a, sizeof(rx_buf_a), 50000);
                if (err) {
                    printk("Failed to Enable Rx: %d\n", err);
                } else {
                    printk("UART Rx Enabled.\n");
                }
            }
            break;

        default:
            printk("Unhandled UART ISR Case ...\n");
            break;
    }
}

int main()
{
    /**
     * @brief Evaluate if the UART device is ready; If not ready, return ERROR
     */
    if (!device_is_ready(uart_device)) {
        printk("UART device not ready\n");
        return -ENXIO;
    }

    /* Event Hook-Up : Register a callback function for UART device */
    int ret = uart_callback_set(uart_device, uart_cb, NULL);
    if (ret) {
        printk("Failed to set UART callback: %d\n", ret);
        return 0;
    }

    int err = uart_rx_enable(uart_device, rx_buf_a, sizeof(rx_buf_a), 50000);
    if (err) {
        printk("Failed to Enable Rx: %d\n", err);
        return err;
    }

    /* sleep forever; do absolutely nothing */
    while(1) {
        err = uart_tx(uart_device, tx_buf, sizeof(tx_buf), SYS_FOREVER_US);
        if (err) {
            return err;
        }
        k_msleep(100);
    }

    return 0;
}