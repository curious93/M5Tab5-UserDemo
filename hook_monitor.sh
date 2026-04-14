#!/bin/bash
# hook_monitor.sh: Captures 10 seconds of serial output after flash (Python version)

LOG_FILE="/Users/nicolasvonbruck/Desktop/M5_3/boot_log.txt"
ESP_PYTHON="$HOME/.espressif/python_env/idf5.4_py3.14_env/bin/python"

echo "--- Post-Deploy Hook: Capturing Serial Log (10s) ---"

$ESP_PYTHON /Users/nicolasvonbruck/Desktop/M5_3/monitor_serial.py > "$LOG_FILE" 2>&1

echo "Serial log captured to $LOG_FILE"
echo "===================================================="
echo "BOOT LOG SUMMARY (Tail):"
tail -n 20 "$LOG_FILE"
echo "===================================================="
