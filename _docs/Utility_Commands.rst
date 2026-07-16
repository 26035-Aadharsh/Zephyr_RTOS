=========================================
Zephyr CLI Commands and Utility Functions
=========================================



---------------------
Zephyr Build Commands
---------------------
 * Build Commands :  west build -p always -b esp32_devkitc/esp32/procpu
 * Flash :  west flash

Zephyr automatically looks inside your application's boards/ folder for matching .overlay files. It evaluates the filename using a specific order of precedence, replacing slashes / with underscores _.

It checks for files in the following exact format:
 1. `<board>_<soc>_<cpu>.overlay` (Most specific)
 2. `<board>_<soc>`.overlay
 3. `<board>`.overlay (Least specific)

--------------------
Python Serial Viewer
--------------------
 * Serial Viewer : python -m serial.tools.miniterm COM7 115200