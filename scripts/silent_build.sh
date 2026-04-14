#!/bin/bash
exec > >(tee -a silent_build.log) 2>&1
echo "==========================================="
echo "Starting silent build and deploy for Tab5"
echo "==========================================="

export PATH="/opt/homebrew/bin:$PATH"
export DEBIAN_FRONTEND=noninteractive
ESP_DIR="$HOME/esp/esp-idf-v5.4.2-tab5"

if [ ! -d "$ESP_DIR" ]; then
    echo "[1/3] Cloning ESP-IDF 5.4.2 targeted version..."
    git clone -b v5.4.2 --depth 1 --recursive https://github.com/espressif/esp-idf.git "$ESP_DIR"
    echo "Installing ESP-IDF tools..."
    cd "$ESP_DIR"
    ./install.sh esp32p4
else
    echo "[1/3] ESP-IDF 5.4.2 already present."
fi

echo "[2/3] Activating ESP-IDF 5.4.2..."
source "$ESP_DIR/export.sh"

echo "[3/3] Building Official M5Tab5 Demo..."
cd /Users/nicolasvonbruck/.gemini/antigravity/scratch/M5_3/M5Tab5-UserDemo-Reference/platforms/tab5
idf.py fullclean || true
idf.py build

echo "[Final] Flashing Firmware..."
esptool.py --chip esp32p4 -p /dev/cu.usbmodem101 -b 460800 --before=default_reset --after=hard_reset write_flash --force --flash_mode dio --flash_freq 80m --flash_size 16MB 0x2000 build/bootloader/bootloader.bin 0x10000 build/m5stack_tab5.bin 0x8000 build/partition_table/partition-table.bin

bash /Users/nicolasvonbruck/.gemini/antigravity/scratch/M5_3/hook_webcam.sh
bash /Users/nicolasvonbruck/.gemini/antigravity/scratch/M5_3/hook_analyze_webcam.sh

echo "DONE."
