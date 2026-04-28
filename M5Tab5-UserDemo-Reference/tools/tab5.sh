#!/usr/bin/env bash
#
# tab5.sh — Single-source-of-truth Build/Flash/Log helper for the M5Tab5
#           project. Hides path traps that have caused repeated session
#           failures (flash_env location, port renumbering, IDF version env).
#
# Usage:
#   tools/tab5.sh build
#   tools/tab5.sh flash
#   tools/tab5.sh log [seconds]                       # default 600
#   tools/tab5.sh logpath                              # print where the next log will land
#   tools/tab5.sh trigger                              # /tmp/sp_m5_osa.sh
#   tools/tab5.sh full [seconds]                      # build + flash + log + trigger
#   tools/tab5.sh grep <pattern> [logfile]            # filter latest log (or named one)
#
set -eu

# ---- Hardcoded constants ---------------------------------------------------
IDF_PATH="${HOME}/esp/esp-idf-v5.4.2-tab5"
FLASH_PY="/Users/nicolasvonbruck/Desktop/M5_3/flash_env/bin/python3"
LOG_DIR="${HOME}/.claude/tab5/serial_logs"
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/platforms/tab5"
TRIGGER="/tmp/sp_m5_osa.sh"

mkdir -p "${LOG_DIR}"

# ---- Helpers ---------------------------------------------------------------
detect_port() {
  local p
  p="$(ls /dev/cu.usbmodem* 2>/dev/null | head -1 || true)"
  if [ -z "${p}" ]; then
    echo "tab5.sh: no /dev/cu.usbmodem* port found — is the device connected?" >&2
    return 1
  fi
  echo "${p}"
}

source_idf() {
  # shellcheck disable=SC1091
  source "${IDF_PATH}/export.sh" >/dev/null 2>&1
  export ESP_IDF_VERSION=5.4
}

ts_filename() {
  echo "${LOG_DIR}/$(date +%Y%m%d_%H%M%S).log"
}

latest_log() {
  ls -t "${LOG_DIR}"/*.log 2>/dev/null | head -1 || true
}

# ---- Commands --------------------------------------------------------------
cmd_build() {
  source_idf
  cd "${BUILD_DIR}"
  idf.py build
}

cmd_flash() {
  source_idf
  cd "${BUILD_DIR}"
  local port
  port="$(detect_port)"
  echo "tab5.sh: flashing on ${port}"
  idf.py -p "${port}" flash
}

cmd_log() {
  local secs="${1:-600}"
  local out
  out="$(ts_filename)"
  local port
  port="$(detect_port)"
  # Wait for port to settle if it is still re-enumerating from a recent reset
  local tries=0
  while [ ! -e "${port}" ] && [ ${tries} -lt 30 ]; do
    sleep 0.5
    tries=$((tries+1))
    port="$(detect_port 2>/dev/null || echo "${port}")"
  done
  stty -f "${port}" 115200 raw 2>/dev/null || true
  echo "tab5.sh: logging ${port} -> ${out} for ${secs}s"
  cat "${port}" > "${out}" &
  local pid=$!
  echo "${pid}" > "${LOG_DIR}/.last_pid"
  echo "${out}"  > "${LOG_DIR}/.last_log"
  ( sleep "${secs}" ; kill "${pid}" 2>/dev/null || true ) &
}

cmd_logpath() {
  cat "${LOG_DIR}/.last_log" 2>/dev/null || echo "(no active log)"
}

cmd_trigger() {
  if [ ! -x "${TRIGGER}" ]; then
    echo "tab5.sh: ${TRIGGER} missing or not executable" >&2
    return 1
  fi
  bash "${TRIGGER}"
}

cmd_full() {
  local secs="${1:-600}"
  cmd_flash
  # idf.py flash hard-resets via RTS; the USB port re-enumerates briefly.
  sleep 2
  cmd_log "${secs}"
  # Give the device ~12s to bring up WiFi + cspot before we hit the trigger.
  sleep 14
  cmd_trigger || true
  echo "tab5.sh: log running, latest = $(cmd_logpath)"
}

cmd_grep() {
  local pattern="${1:?usage: tab5.sh grep <pattern> [logfile]}"
  local file="${2:-$(latest_log)}"
  if [ -z "${file}" ] || [ ! -f "${file}" ]; then
    echo "tab5.sh: no log file to grep" >&2
    return 1
  fi
  echo "tab5.sh: grepping ${file} for /${pattern}/"
  grep -aE "${pattern}" "${file}" || true
}

# ---- Dispatch --------------------------------------------------------------
sub="${1:-help}"
shift || true
case "${sub}" in
  build)   cmd_build "$@" ;;
  flash)   cmd_flash "$@" ;;
  log)     cmd_log "$@" ;;
  logpath) cmd_logpath "$@" ;;
  trigger) cmd_trigger "$@" ;;
  full)    cmd_full "$@" ;;
  grep)    cmd_grep "$@" ;;
  help|--help|-h)
    sed -n '3,18p' "$0" ;;
  *)
    echo "tab5.sh: unknown command '${sub}' (try: tools/tab5.sh help)" >&2
    exit 2 ;;
esac
