#!/bin/bash
# auto_git_sync.sh
# Dieses Skript wird nach jedem Deployment aufgerufen.

REPO_DIR="/Users/nicolasvonbruck/.gemini/antigravity/scratch/M5_3/M5Tab5-UserDemo-Reference"
MSG="Auto-sync after deploy: $(date)"

cd "$REPO_DIR" || exit
if [ -d ".git" ]; then
    git add .
    git commit -m "$MSG"
    git push origin main # Oder den aktuellen Branch
    echo "GitHub-Sync abgeschlossen."
else
    echo "KEIN GIT-REPO GEFUNDEN. Synchronisation fehlgeschlagen."
fi
