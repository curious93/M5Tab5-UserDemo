#!/bin/bash
echo "--- Post-Deploy Hook: Checking Webcam ---"

# Check if Anker PowerConf C200 is connected
if system_profiler SPUSBDataType | grep -q "Anker PowerConf C200"; then
    echo "Anker Webcam detected. Wait 12 seconds for device boot and LVGL render..."
    sleep 12
    
    SNAPSHOT_FILE="/Users/nicolasvonbruck/.gemini/antigravity/scratch/M5_3/snap_anker_latest.jpg"
    
    if command -v imagesnap &> /dev/null; then
        echo "Taking snapshot with imagesnap..."
        imagesnap -w 2 -d "Anker PowerConf C200" "$SNAPSHOT_FILE" > /dev/null 2>&1
        echo "Snapshot saved to $SNAPSHOT_FILE"
    elif command -v ffmpeg &> /dev/null; then
        echo "Taking snapshot with ffmpeg..."
        ffmpeg -y -f avfoundation -video_size 1280x720 -framerate 30 -i "0" -vframes 1 "$SNAPSHOT_FILE" > /dev/null 2>&1
        echo "Snapshot saved to $SNAPSHOT_FILE"
    else
        echo "WARNING: Neither 'ffmpeg' nor 'imagesnap' is installed."
        echo "Please install one of them (e.g. 'brew install ffmpeg') to enable the webcam hook."
    fi
    
    # NEU: Kausale Kette - Starte sofort den Analyse-Check
    bash /Users/nicolasvonbruck/.gemini/antigravity/scratch/M5_3/hook_analyze_webcam.sh
else
    echo "Anker Webcam not connected. Skipping snapshot hook."
fi
