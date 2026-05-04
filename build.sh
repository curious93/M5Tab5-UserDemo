#!/bin/bash
# M5_3 Spotify Streamer — Konsolidierter Build/Flash/Monitor Wrapper
#
# Ersetzt: fast_build.sh, silent_build.sh, hook_post_deploy.sh
#
# Usage:
#   ./build.sh              # build + flash + monitor (default)
#   ./build.sh --no-build   # nur flash + monitor
#   ./build.sh --no-flash   # nur build
#   ./build.sh --no-monitor # build + flash, kein serial read
#   ./build.sh --silent     # alles in commands.log statt stdout
#   ./build.sh --webcam     # zusätzlich Webcam-Snapshot als Diagnose
#   ./build.sh --skip-test  # Phase 4: 10× Skip-Performance-Test
#   ./build.sh --duration N # Monitor-Dauer in Sekunden (default: 15)
#   ./build.sh --until "[CSPOT_READY]" # Stoppen bei Sentinel

set -uo pipefail

# ===== Defaults =====
DO_BUILD=1
DO_FLASH=1
DO_MONITOR=1
SILENT=0
WEBCAM=0
SKIP_TEST=0
WATCH=0
DURATION=15
SENTINEL=""

# ===== Parse Args =====
while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-build)    DO_BUILD=0; shift ;;
        --no-flash)    DO_FLASH=0; shift ;;
        --no-monitor)  DO_MONITOR=0; shift ;;
        --silent)      SILENT=1; shift ;;
        --webcam)      WEBCAM=1; shift ;;
        --skip-test)   SKIP_TEST=1; [[ "$DURATION" -lt 60 ]] && DURATION=60; shift ;;
        --watch)       WATCH=1; DO_BUILD=0; DO_FLASH=0; DURATION=300; shift ;;
        --duration)    DURATION="$2"; shift 2 ;;
        --until)       SENTINEL="$2"; shift 2 ;;
        -h|--help)
            sed -n '3,16p' "$0" | sed 's/^# \?//'
            exit 0 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

# ===== Paths =====
M5_3_ROOT="/Users/nicolasvonbruck/Desktop/M5_3"
ESP_DIR="$HOME/esp/esp-idf-v5.4.2-tab5"
BUILD_DIR="$M5_3_ROOT/M5Tab5-UserDemo-Reference/platforms/tab5"
SESSIONS_DIR="$M5_3_ROOT/sessions"
MONITOR="$M5_3_ROOT/monitor_serial.py"

# ===== Session anlegen =====
SESSION_ID="$(date +%Y-%m-%d_%H%M%S)_$(openssl rand -hex 2)"
SESSION_DIR="$SESSIONS_DIR/$SESSION_ID"
mkdir -p "$SESSION_DIR"
COMMANDS_LOG="$SESSION_DIR/commands.log"
SERIAL_LOG="$SESSION_DIR/serial.log"
VERDICT_JSON="$SESSION_DIR/verdict.json"

# ===== Cleanup-Trap (Serial-Port freigeben bei Abbruch / Fehler) =====
cleanup() {
    local rc=$?
    # Hinterlegt in serial.log dass abgebrochen wurde
    if [[ -n "${MONITOR_PID:-}" ]] && kill -0 "$MONITOR_PID" 2>/dev/null; then
        kill "$MONITOR_PID" 2>/dev/null || true
    fi
    # Falls noch ein monitor_serial.py läuft (Race), aufräumen
    pkill -f "$M5_3_ROOT/monitor_serial.py" 2>/dev/null || true
    # Bei abnormalem Exit: verdict.json mit ABORT überschreiben (falls noch PENDING)
    if [[ $rc -ne 0 && -f "${VERDICT_JSON:-/dev/null}" ]] \
       && grep -q '"PENDING"' "$VERDICT_JSON" 2>/dev/null; then
        write_verdict "FAIL" "aborted_exit_$rc"
    fi
    exit $rc
}
trap cleanup EXIT INT TERM

# ===== Logging =====
ts() { date +"%Y-%m-%d %H:%M:%S"; }
log() {
    local msg="[$(ts)] $*"
    echo "$msg" >> "$COMMANDS_LOG"
    if [[ $SILENT -eq 0 ]]; then echo "$msg"; fi
}

if [[ $SILENT -eq 1 ]]; then
    exec > >(tee -a "$COMMANDS_LOG") 2>&1
fi

log "=== M5_3 build.sh start (session: $SESSION_ID) ==="
log "flags: build=$DO_BUILD flash=$DO_FLASH monitor=$DO_MONITOR webcam=$WEBCAM skip_test=$SKIP_TEST"

# ===== Port discovery =====
PORT_LIST=$(ls /dev/cu.usbmodem* 2>/dev/null | head -n 1)
PORT="${PORT_LIST:-/dev/cu.usbmodem*}"
log "port detected: $PORT"

# ===== meta.json =====
COMMIT_HASH=$(cd "$M5_3_ROOT" && git rev-parse --short HEAD 2>/dev/null || echo "unknown")
SDKCONFIG_HASH=$(shasum "$BUILD_DIR/sdkconfig" 2>/dev/null | awk '{print substr($1,1,12)}' || echo "missing")
cat > "$SESSION_DIR/meta.json" <<EOF
{
  "session_id": "$SESSION_ID",
  "started_at": "$(ts)",
  "commit": "$COMMIT_HASH",
  "sdkconfig_sha": "$SDKCONFIG_HASH",
  "port": "$PORT",
  "flags": {
    "build": $DO_BUILD,
    "flash": $DO_FLASH,
    "monitor": $DO_MONITOR,
    "webcam": $WEBCAM,
    "skip_test": $SKIP_TEST
  },
  "duration": $DURATION,
  "sentinel": $(if [[ -n "$SENTINEL" ]]; then printf '"%s"' "$SENTINEL"; else echo "null"; fi)
}
EOF

# ===== verdict.json (initial: PENDING) =====
write_verdict() {
    local result="$1"; local reason="$2"; local extra="${3:-}"
    cat > "$VERDICT_JSON" <<EOF
{
  "result": "$result",
  "reason": "$reason",
  "session_id": "$SESSION_ID",
  "ended_at": "$(ts)"$extra
}
EOF
}
write_verdict "PENDING" "started"

# ===== ESP-IDF =====
if [[ $DO_BUILD -eq 1 || $DO_FLASH -eq 1 ]]; then
    if [[ ! -f "$ESP_DIR/export.sh" ]]; then
        log "ERROR: ESP-IDF v5.4.2 nicht gefunden unter $ESP_DIR"
        write_verdict "FAIL" "esp-idf missing"
        exit 1
    fi
    log "sourcing ESP-IDF v5.4.2"
    # shellcheck disable=SC1091
    source "$ESP_DIR/export.sh" >> "$COMMANDS_LOG" 2>&1
fi

# ===== Build =====
if [[ $DO_BUILD -eq 1 ]]; then
    log "build: idf.py build (in $BUILD_DIR)"
    cd "$BUILD_DIR"
    if ! idf.py build >> "$COMMANDS_LOG" 2>&1; then
        log "BUILD FAILED — siehe commands.log"
        write_verdict "FAIL" "build_failed"
        exit 2
    fi
    log "build OK"
fi

# ===== Flash =====
if [[ $DO_FLASH -eq 1 ]]; then
    cd "$BUILD_DIR"
    log "flash: esptool to $PORT"
    if ! esptool.py --chip esp32p4 -p "$PORT" -b 460800 \
        --before=default_reset --after=hard_reset \
        write_flash --force --flash_mode dio --flash_freq 80m --flash_size 16MB \
        0x2000  build/bootloader/bootloader.bin \
        0x8000  build/partition_table/partition-table.bin \
        0x10000 build/m5stack_tab5.bin >> "$COMMANDS_LOG" 2>&1; then
        log "FLASH FAILED"
        write_verdict "FAIL" "flash_failed"
        exit 3
    fi
    log "flash OK"
fi

# ===== Monitor (serial read into session log) =====
if [[ $DO_MONITOR -eq 1 ]]; then
    log "monitor: $DURATION s, sentinel=${SENTINEL:-(none)}"
    MON_ARGS=(--duration "$DURATION" --output "$SERIAL_LOG" --quiet)
    [[ -n "$SENTINEL" ]] && MON_ARGS+=(--until "$SENTINEL")
    # Pass silence-crash threshold (15s) so watchdog fires if device resets mid-session
    MON_ARGS+=(--silence-crash 15)
    set +e
    "$MONITOR" "${MON_ARGS[@]}" >> "$COMMANDS_LOG" 2>&1
    MONITOR_RC=$?
    set -e
    log "monitor exit code: $MONITOR_RC"
    # Exit code 4 = crash/silence detected by watchdog
    if [[ ${MONITOR_RC:-0} -eq 4 ]]; then
        log "CRASH/SILENCE detected by monitor watchdog — marking FAIL"
        RESULT="FAIL"
        REASON="monitor_crash_watchdog"
    fi
fi

# ===== Backtrace decoding =====
DECODED_LOG="$SESSION_DIR/serial_decoded.log"
decode_backtrace() {
    local elf="$BUILD_DIR/build/m5stack_tab5.elf"
    if [[ ! -f "$elf" ]]; then
        return
    fi
    # addr2line via PATH ODER fallback auf ESP-IDF Tools-Verzeichnis
    local addr2line=""
    if command -v riscv32-esp-elf-addr2line >/dev/null 2>&1; then
        addr2line="riscv32-esp-elf-addr2line"
    else
        # ESP-IDF v5.4.2 Tool-Pfad (RISC-V für ESP32-P4)
        local tool_glob=$(ls -d "$HOME"/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin/riscv32-esp-elf-addr2line 2>/dev/null | head -n 1)
        [[ -n "$tool_glob" && -x "$tool_glob" ]] && addr2line="$tool_glob"
    fi
    if [[ -z "$addr2line" ]]; then
        log "addr2line not found — backtrace decode skipped"
        return
    fi
    # Backtrace-Zeilen finden, Adressen extrahieren, durch addr2line jagen
    # || true: grep no-match returned 1, ist mit pipefail sonst fatal
    local addrs
    addrs=$(grep -oE "0x4[0-9a-fA-F]{7}" "$SERIAL_LOG" 2>/dev/null | sort -u || true)
    if [[ -z "$addrs" ]]; then
        return 0
    fi
    {
        echo "=== Backtrace decoded via $addr2line ==="
        echo "ELF: $elf"
        echo ""
        for addr in $addrs; do
            local resolved
            resolved=$($addr2line -pfiaC -e "$elf" "$addr" 2>/dev/null)
            echo "$resolved"
        done
    } > "$DECODED_LOG"
    log "backtrace decoded → $DECODED_LOG ($(wc -l < "$DECODED_LOG") lines)"
}
if [[ $DO_MONITOR -eq 1 ]]; then
    decode_backtrace
fi

# ===== Webcam-Snapshot =====
SNAP_DST="$SESSION_DIR/webcam.jpg"
take_snapshot() {
    if command -v imagesnap >/dev/null 2>&1; then
        imagesnap -d "Anker PowerConf C200" -w 1.0 "$SNAP_DST" >/dev/null 2>&1 \
            && log "webcam snapshot: $SNAP_DST" \
            || log "webcam snapshot FAILED"
    else
        log "imagesnap not installed — skipping webcam"
    fi
}

# Webcam: bei --webcam IMMER, sonst nur bei FAIL (am Ende geprüft)
if [[ $WEBCAM -eq 1 ]]; then
    take_snapshot
fi

# ===== Verdict bestimmen =====
RESULT="PASS"
REASON="ok"

if [[ $DO_MONITOR -eq 1 && -f "$SERIAL_LOG" ]]; then
    if grep -qiE "panic|abort|Guru Meditation|Instruction fault|ESP_ERROR_CHECK failed" "$SERIAL_LOG"; then
        RESULT="FAIL"
        REASON="crash_in_serial"
    elif [[ -n "$SENTINEL" && ${MONITOR_RC:-0} -eq 3 ]]; then
        RESULT="FAIL"
        REASON="sentinel_not_seen"
    fi
fi

# Bei FAIL: Webcam-Snapshot automatisch (falls noch nicht gemacht)
if [[ "$RESULT" == "FAIL" && $WEBCAM -eq 0 && ! -f "$SNAP_DST" ]]; then
    log "FAIL detected — taking diagnostic webcam snapshot"
    take_snapshot
fi

# ===== Stats-Aggregation (Skip + DNS + TCP + DMA) =====
SKIP_EXTRA=""
agg_stat() {
    # $1=marker pattern, $2=label — OUTPUT geht nur ins JSON-Fragment, KEIN log()-Aufruf
    # Stats werden AUSSERHALB der command-substitution geloggt
    # Wichtig: nur "took=N" extrahieren, NICHT alle Zahlen (sonst Port etc. drin)
    local pattern="$1"; local label="$2"
    local times
    times=$(grep -oE "$pattern took=[0-9]+ms" "$SERIAL_LOG" 2>/dev/null \
            | grep -oE 'took=[0-9]+' | grep -oE '[0-9]+' | sort -n || true)
    [[ -z "$times" ]] && return
    local n med p95 mx
    n=$(echo "$times" | wc -l | tr -d ' ')
    med=$(echo "$times" | awk -v n="$n" 'NR==int((n+1)/2)')
    p95=$(echo "$times" | awk -v n="$n" 'NR==int(n*0.95)+1 || (NR==n && n<2)')
    mx=$(echo "$times" | tail -n 1)
    echo "${label}|n=$n|med=$med|p95=${p95:-$med}|max=$mx"
}
emit_stat_json() {
    # $1=label, $2=n, $3=med, $4=p95, $5=max
    echo ", \"${1}_stats\": {\"n\": $2, \"median_ms\": $3, \"p95_ms\": $4, \"max_ms\": $5}"
}
if [[ -f "$SERIAL_LOG" ]]; then
    # Skip-Stats werden IMMER aggregiert wenn Marker da sind
    SKIP_TIMES=$(grep -oE '\[SKIP_DONE [^]]*in [0-9]+ms\]' "$SERIAL_LOG" 2>/dev/null \
                 | grep -oE 'in [0-9]+ms' | grep -oE '[0-9]+' | sort -n || true)
    if [[ -n "$SKIP_TIMES" ]]; then
        N=$(echo "$SKIP_TIMES" | wc -l | tr -d ' ')
        MEDIAN=$(echo "$SKIP_TIMES" | awk -v n="$N" 'NR==int((n+1)/2)')
        P95=$(echo "$SKIP_TIMES" | awk -v n="$N" 'NR==int(n*0.95)+1 || (NR==n && n<2)')
        MAX=$(echo "$SKIP_TIMES" | tail -n 1)
        SKIP_EXTRA="${SKIP_EXTRA}, \"skip_stats\": {\"n\": $N, \"median_ms\": $MEDIAN, \"p95_ms\": ${P95:-$MEDIAN}, \"max_ms\": $MAX}"
        log "skip-stats: n=$N median=${MEDIAN}ms p95=${P95:-$MEDIAN}ms max=${MAX}ms"
    elif [[ $SKIP_TEST -eq 1 ]]; then
        log "skip-test: no [SKIP_DONE] markers found"
    fi
    DNS_RAW=$(agg_stat '\[DNS_LOOKUP[^]]*' dns)
    if [[ -n "$DNS_RAW" ]]; then
        IFS='|' read -r _label _n _med _p95 _max <<< "$DNS_RAW"
        SKIP_EXTRA="${SKIP_EXTRA}$(emit_stat_json dns "${_n#n=}" "${_med#med=}" "${_p95#p95=}" "${_max#max=}")"
        log "dns-stats: $DNS_RAW"
    fi
    TCP_RAW=$(agg_stat '\[TCP_CONNECT[^]]*' tcp)
    if [[ -n "$TCP_RAW" ]]; then
        IFS='|' read -r _label _n _med _p95 _max <<< "$TCP_RAW"
        SKIP_EXTRA="${SKIP_EXTRA}$(emit_stat_json tcp "${_n#n=}" "${_med#med=}" "${_p95#p95=}" "${_max#max=}")"
        log "tcp-stats: $TCP_RAW"
    fi
    TLS_RAW=$(agg_stat '\[TLS_CONNECT[^]]*' tls)
    if [[ -n "$TLS_RAW" ]]; then
        IFS='|' read -r _label _n _med _p95 _max <<< "$TLS_RAW"
        SKIP_EXTRA="${SKIP_EXTRA}$(emit_stat_json tls "${_n#n=}" "${_med#med=}" "${_p95#p95=}" "${_max#max=}")"
        log "tls-stats: $TLS_RAW"
    fi
    DMA_MIN=$(grep -oE '\[DMA_FREE [0-9]+' "$SERIAL_LOG" 2>/dev/null \
              | grep -oE '[0-9]+' | sort -n | head -n 1 || true)
    [[ -n "$DMA_MIN" ]] && {
        SKIP_EXTRA="${SKIP_EXTRA}, \"dma_min_bytes\": $DMA_MIN"
        log "dma-min: $DMA_MIN bytes"
    }
    # TRACK_LOAD ready — echte CDN-Ladezeit pro Track (der wichtigste Wert)
    TRACK_LOAD_TIMES=$(grep -oE '\[TRACK_LOAD ready in [0-9]+ms' "$SERIAL_LOG" 2>/dev/null \
                       | grep -oE '[0-9]+' | sort -n || true)
    if [[ -n "$TRACK_LOAD_TIMES" ]]; then
        TL_N=$(echo "$TRACK_LOAD_TIMES" | wc -l | tr -d ' ')
        TL_MED=$(echo "$TRACK_LOAD_TIMES" | awk -v n="$TL_N" 'NR==int((n+1)/2)')
        TL_P95=$(echo "$TRACK_LOAD_TIMES" | awk -v n="$TL_N" 'NR==int(n*0.95)+1 || (NR==n && n<2)')
        TL_MAX=$(echo "$TRACK_LOAD_TIMES" | tail -n 1)
        SKIP_EXTRA="${SKIP_EXTRA}, \"track_load_stats\": {\"n\": $TL_N, \"median_ms\": $TL_MED, \"p95_ms\": ${TL_P95:-$TL_MED}, \"max_ms\": $TL_MAX}"
        log "track-load-stats: n=$TL_N median=${TL_MED}ms p95=${TL_P95:-$TL_MED}ms max=${TL_MAX}ms"
    fi
    # H2 reuse count — zeigt ob TLS-Connection-Wiederverwendung aktiv ist
    H2_REUSE_N=$(grep -c '\[H2_REUSE ' "$SERIAL_LOG" 2>/dev/null || true)
    [[ -n "$H2_REUSE_N" && "$H2_REUSE_N" -gt 0 ]] && {
        SKIP_EXTRA="${SKIP_EXTRA}, \"h2_reuse_count\": $H2_REUSE_N"
        log "h2-reuse: $H2_REUSE_N connections reused"
    }

    # Audio glitch count + CDN stall count
    # Note: grep -c exits 1 when no matches but still prints "0"; use || true to avoid
    # the || echo 0 pattern which would produce "0\n0" (two lines) on zero-match files.
    GLITCH_N=$(grep -c '\[AUDIO_GLITCH ' "$SERIAL_LOG" 2>/dev/null || true)
    CDN_STALL_N=$(grep -c '\[CDN_STALL ' "$SERIAL_LOG" 2>/dev/null || true)
    GLITCH_MAX_MS=$(grep -oE '\[AUDIO_GLITCH gap=[0-9]+' "$SERIAL_LOG" 2>/dev/null \
                    | grep -oE '[0-9]+' | sort -n | tail -1 || echo 0)
    STALL_MAX_MS=$(grep -oE '\[CDN_STALL_END waited=[0-9]+' "$SERIAL_LOG" 2>/dev/null \
                   | grep -oE '[0-9]+' | sort -n | tail -1 || echo 0)
    SKIP_EXTRA="${SKIP_EXTRA}, \"audio_glitches\": {\"count\": ${GLITCH_N:-0}, \"max_gap_ms\": ${GLITCH_MAX_MS:-0}}"
    SKIP_EXTRA="${SKIP_EXTRA}, \"cdn_stalls\": {\"count\": ${CDN_STALL_N:-0}, \"max_wait_ms\": ${STALL_MAX_MS:-0}}"
    log "audio-glitches: ${GLITCH_N:-0} (max gap ${GLITCH_MAX_MS:-0}ms)"
    log "cdn-stalls: ${CDN_STALL_N:-0} (max wait ${STALL_MAX_MS:-0}ms)"

    # Crash/watchdog markers from monitor
    CRASH_N=$(grep -c '\[CRASH_DETECTED\|\[WATCHDOG:' "$SERIAL_LOG" 2>/dev/null || true)
    [[ "${CRASH_N:-0}" -gt 0 ]] && {
        SKIP_EXTRA="${SKIP_EXTRA}, \"crash_events\": $CRASH_N"
        log "CRASH EVENTS DETECTED: $CRASH_N"
        RESULT="FAIL"
        REASON="crash_detected_in_log"
    }
fi
# Verdict-extra muss mit Komma anfangen wenn nicht leer (write_verdict erwartet das)
[[ -n "$SKIP_EXTRA" && "${SKIP_EXTRA:0:2}" != ", " ]] && SKIP_EXTRA=", $SKIP_EXTRA"

write_verdict "$RESULT" "$REASON" "$SKIP_EXTRA"

# ===== Latest Symlink =====
ln -sfn "$SESSION_ID" "$SESSIONS_DIR/latest"

# ===== Retention (last 30) =====
cd "$SESSIONS_DIR"
ls -1dt 2*/ 2>/dev/null | tail -n +31 | xargs -I {} rm -rf "{}"

log "=== verdict: $RESULT ($REASON) ==="
log "session: $SESSION_DIR"

[[ "$RESULT" == "PASS" ]] && exit 0 || exit 4
