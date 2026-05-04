#!/bin/bash
export PATH="/opt/homebrew/bin:$PATH"
export DEBIAN_FRONTEND=noninteractive
ESP_DIR="$HOME/esp/esp-idf-v5.4.2-tab5"
source "$ESP_DIR/export.sh"
cd /Users/nicolasvonbruck/Desktop/M5_3/M5Tab5-UserDemo-Reference/platforms/tab5
idf.py build
esptool.py --chip esp32p4 -p /dev/cu.usbmodem101 -b 460800 --before=default_reset --after=hard_reset write_flash --force --flash_mode dio --flash_freq 80m --flash_size 16MB 0x2000 build/bootloader/bootloader.bin 0x10000 build/m5stack_tab5.bin 0x8000 build/partition_table/partition-table.bin

bash /Users/nicolasvonbruck/Desktop/M5_3/hook_post_deploy.sh
