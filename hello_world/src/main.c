#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

int main()
{
    while(1) {
        printk("Hello, World!\n");
        k_msleep(1000);
    }
    return 0;
}
