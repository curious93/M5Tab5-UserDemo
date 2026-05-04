#!/bin/bash
# wifi_check.sh — Schneller Network-Test für Tab5 ohne Skip-Test
#
# Flasht Tab5 und liest 60s lang Boot+WiFi+Spotify-Connect-Status.
# Liefert klare Diagnose: "WLAN OK", "Spotify nicht erreichbar" oder Crash.

set -uo pipefail

cd "$(dirname "$0")"
SESSION_DIR="sessions/$(date +%Y-%m-%d_%H%M%S)_wifi"
mkdir -p "$SESSION_DIR"
LOG="$SESSION_DIR/serial.log"

echo "=== Flash + 60s Boot-Read ==="
./build.sh --no-build --duration 60 2>&1 | tail -3

# Latest Session ist die gerade gemachte
LATEST_LOG="sessions/latest/serial.log"
[[ -f "$LATEST_LOG" ]] || { echo "❌ kein serial.log"; exit 1; }

echo ""
echo "=== Diagnose ==="

# 1. WLAN-Status
SSID=$(grep -oE "SSID=[^ ]+" "$LATEST_LOG" | head -n 1 | cut -d= -f2)
IP=$(grep -oE "sta ip: [0-9.]+" "$LATEST_LOG" | head -n 1 | awk '{print $3}')
DNS=$(grep -oE "DNS set to [0-9.]+" "$LATEST_LOG" | head -n 1 | awk '{print $4}')

echo "WLAN SSID:   ${SSID:-NOT_CONNECTED}"
echo "Tab5 IP:     ${IP:-NONE}"
echo "DNS Server:  ${DNS:-NONE}"

# 2. TLS-Connect zu Spotify
TLS_OK=$(grep -c "\[TLS_CONNECT.*OK\]" "$LATEST_LOG" 2>/dev/null | tr -d '[:space:]')
TLS_FAIL=$(grep -c "\[TLS_CONNECT.*FAILED" "$LATEST_LOG" 2>/dev/null | tr -d '[:space:]')
TLS_OK=${TLS_OK:-0}
TLS_FAIL=${TLS_FAIL:-0}

echo "TLS connects: $TLS_OK OK / $TLS_FAIL FAIL"

# 3. Verdict
echo ""
if [[ "$TLS_OK" -gt 0 ]]; then
    echo "✅ NETWORK OK — Spotify erreichbar"
    echo "   → Phase 4 starten: ./build.sh --no-build --no-flash --skip-test"
elif [[ "$TLS_FAIL" -gt 0 ]]; then
    echo "❌ NETWORK BLOCKED — Spotify nicht erreichbar"
    echo "   Mögliche Ursachen:"
    echo "   - WLAN '$SSID' hat kein Internet"
    echo "   - Captive Portal (Browser-Login nötig)"
    echo "   - Firewall blockiert Port 443"
    echo "   - Spotify-IP gebannt"
    echo ""
    echo "   Test vom selben WLAN aus (Smartphone): https://apresolve.spotify.com/"
    echo "   Wenn auch dort blockt → WLAN-Problem, anderes Netz nutzen"
    echo "   Wenn dort OK → Tab5-spezifisches Problem (TLS-Cert? Spotify-Account?)"
elif [[ -z "${IP:-}" ]]; then
    echo "⚠️  WLAN-Verbindung fehlgeschlagen — keine IP erhalten"
    echo "   WLAN '$SSID' nicht erreichbar oder falsche Credentials"
else
    echo "⚠️  Boot-Phase noch nicht abgeschlossen oder unbekannter Zustand"
    echo "   Letzte 5 Zeilen:"
    tail -n 5 "$LATEST_LOG" | sed 's/^/   /'
fi

echo ""
echo "Vollständiges Log: $LATEST_LOG"
