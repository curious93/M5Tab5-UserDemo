#!/bin/bash
# hook_post_deploy.sh: Synchronisierte Überwachung (Log + Bild)

LOG_FILE="/Users/nicolasvonbruck/.gemini/antigravity/scratch/M5_3/boot_log.txt"
SNAP_FILE="/Users/nicolasvonbruck/.gemini/antigravity/scratch/M5_3/snap_anker_latest.jpg"
ESP_PYTHON="$HOME/.espressif/python_env/idf5.4_py3.14_env/bin/python"
MONITOR_SCRIPT="/Users/nicolasvonbruck/.gemini/antigravity/scratch/M5_3/monitor_serial.py"

echo "--- Post-Deploy Hook: Synchronisierter Check (12s) ---"

# 1. Starte Log-Erfassung im Hintergrund
$ESP_PYTHON "$MONITOR_SCRIPT" > "$LOG_FILE" 2>&1 &
MONITOR_PID=$!

# 2. Synchroner Countdown (Display braucht Zeit zum Stabilisieren)
echo "Warte auf Boot und Display-Init..."
for i in {12..1}; do
    echo -n "$i "
    sleep 1
done
echo "!"

# 3. Snapshot machen (exakt nach dem Countdown)
echo "Mache Snapshot mit imagesnap..."
imagesnap -d "Anker PowerConf C200" -w 1.0 "$SNAP_FILE" > /dev/null 2>&1

# 4. Sicherstellen, dass der Monitor fertig ist
wait $MONITOR_PID 2>/dev/null || true

echo ""
echo "===================================================="
echo "DIAGNOSE-ABSCHLUSSBERICHT"
echo "===================================================="
# Prüfe Log auf Fehler
if grep -qiE "panic|abort|Error|Instruction fault|ESP_ERROR_CHECK" "$LOG_FILE"; then
    echo "!!! KRITISCHER FEHLER IM LOG GEFUNDEN !!!"
    grep -iE "panic|abort|Error|Instruction fault|ESP_ERROR_CHECK" "$LOG_FILE" | head -n 5
else
    echo "Log-Check: Keine offensichtlichen Abstürze gefunden."
fi
echo "Visual: Bild unter $SNAP_FILE gespeichert."
echo "===================================================="

# Trigger AI Analysis (alte Hook aufrufen oder direkt integrieren)
bash /Users/nicolasvonbruck/.gemini/antigravity/scratch/M5_3/hook_analyze_webcam.sh
