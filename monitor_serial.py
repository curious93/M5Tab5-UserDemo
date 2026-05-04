#!/Users/nicolasvonbruck/Desktop/M5_3/flash_env/bin/python3
"""
Primary serial reader for M5_3 Spotify Streamer.

Reads from /dev/cu.usbmodem* (auto-detected via glob) at 2 Mbaud,
prepends millisecond timestamps, strips ANSI escapes, and optionally
writes to a session log file.

Crash detection:
  - Silence watchdog: if no serial output for SILENCE_CRASH_S seconds,
    emits [WATCHDOG: silent Xs — crash suspected] to log and stderr.
  - Pattern detector: lines matching known crash patterns (Guru Meditation,
    stack overflow, abort, Task watchdog) emit [CRASH_DETECTED: ...].
  - Exit code 4 when crash/silence detected.

Usage:
    ./monitor_serial.py                     # 10s capture, stdout
    ./monitor_serial.py --duration 30
    ./monitor_serial.py --until "[CSPOT_READY]"
    ./monitor_serial.py --output sessions/foo/serial.log
    ./monitor_serial.py --baud 921600       # override baud
    ./monitor_serial.py --silence-crash 15  # crash if silent >15s (default)
"""

import argparse
import glob
import re
import sys
import time
from pathlib import Path

import serial


ANSI_RE = re.compile(rb"\x1b\[[0-?]*[ -/]*[@-~]")

# Patterns that indicate a firmware crash/panic.
CRASH_PATTERNS = [
    re.compile(rb"Guru Meditation"),
    re.compile(rb"stack overflow"),
    re.compile(rb"\*\*\*ERROR\*\*\*"),
    re.compile(rb"abort\(\)"),
    re.compile(rb"Task watchdog"),
    re.compile(rb"LoadProhibited"),
    re.compile(rb"IllegalInstruction"),
    re.compile(rb"InstrFetchProhibited"),
]

# Default silence threshold: if no output for this long, assume crash/reset.
SILENCE_CRASH_S = 15


def find_port() -> str:
    """Return first matching /dev/cu.usbmodem* or raise."""
    candidates = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not candidates:
        raise RuntimeError("Tab5 nicht angeschlossen — kein /dev/cu.usbmodem* gefunden")
    return candidates[0]


def _emit(line: bytes, out_fp, quiet: bool) -> None:
    if not quiet:
        sys.stdout.buffer.write(line)
        sys.stdout.buffer.flush()
    if out_fp:
        out_fp.write(line)
        out_fp.flush()


def main() -> int:
    parser = argparse.ArgumentParser(description="M5_3 serial reader")
    parser.add_argument("--port", default=None)
    parser.add_argument("--baud", type=int, default=2_000_000)
    parser.add_argument("--duration", type=float, default=10.0,
                        help="Max capture seconds (default: 10)")
    parser.add_argument("--until", default=None,
                        help="Stop when this sentinel appears in output")
    parser.add_argument("--output", default=None)
    parser.add_argument("--quiet", action="store_true",
                        help="Suppress stdout, only write to file")
    parser.add_argument("--silence-crash", type=float, default=SILENCE_CRASH_S,
                        help=f"Emit crash warning after this many seconds of silence (default: {SILENCE_CRASH_S})")
    args = parser.parse_args()

    port = args.port or find_port()
    out_path = Path(args.output) if args.output else None
    out_fp = out_path.open("wb") if out_path else None

    if not args.quiet:
        sys.stderr.write(f"[monitor] port={port} baud={args.baud} "
                         f"duration={args.duration}s silence_crash={args.silence_crash}s\n")
        if args.until:
            sys.stderr.write(f"[monitor] until={args.until!r}\n")
        sys.stderr.flush()

    sentinel_bytes = args.until.encode() if args.until else None
    start = time.time()
    sentinel_hit = False
    crash_detected = False
    exit_code = 0

    last_output_t = time.time()
    silence_warned = False  # only warn once per silence episode

    try:
        with serial.Serial(port, args.baud, timeout=0.5) as ser:
            while (time.time() - start) < args.duration:
                line = ser.readline()
                now = time.time()
                ts_ms = int((now - start) * 1000)

                if not line:
                    # Check silence watchdog
                    silent_s = now - last_output_t
                    if not silence_warned and silent_s >= args.silence_crash:
                        msg = (f"[{ts_ms:6d}ms] [WATCHDOG: no serial output for "
                               f"{silent_s:.0f}s — device crash suspected]\n").encode()
                        _emit(msg, out_fp, args.quiet)
                        sys.stderr.write(f"[monitor] CRASH SUSPECTED: silent {silent_s:.0f}s\n")
                        sys.stderr.flush()
                        silence_warned = True
                        crash_detected = True
                    continue

                # Got output — reset silence tracking
                last_output_t = now
                silence_warned = False

                line = ANSI_RE.sub(b"", line)
                stamped = f"[{ts_ms:6d}ms] ".encode() + line
                if not stamped.endswith(b"\n"):
                    stamped += b"\n"
                _emit(stamped, out_fp, args.quiet)

                # Crash pattern detection
                for pat in CRASH_PATTERNS:
                    if pat.search(line):
                        alert = (f"[{ts_ms:6d}ms] [CRASH_DETECTED: {line.decode(errors='replace').strip()}]\n").encode()
                        _emit(alert, out_fp, args.quiet)
                        sys.stderr.write(f"[monitor] CRASH DETECTED at {ts_ms}ms: "
                                         f"{line.decode(errors='replace').strip()[:80]}\n")
                        sys.stderr.flush()
                        crash_detected = True
                        break

                if sentinel_bytes and sentinel_bytes in line:
                    sentinel_hit = True
                    if not args.quiet:
                        sys.stderr.write(f"[monitor] sentinel hit at {ts_ms}ms\n")
                    break

    except serial.SerialException as e:
        sys.stderr.write(f"[monitor] error: {e}\n")
        exit_code = 2
    except KeyboardInterrupt:
        sys.stderr.write("[monitor] interrupted\n")
        exit_code = 130
    finally:
        if out_fp:
            out_fp.close()

    if sentinel_bytes and not sentinel_hit and exit_code == 0:
        sys.stderr.write(f"[monitor] sentinel {args.until!r} not seen "
                         f"within {args.duration}s\n")
        exit_code = 3

    if crash_detected and exit_code == 0:
        exit_code = 4

    return exit_code


if __name__ == "__main__":
    sys.exit(main())
