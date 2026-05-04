#!/usr/bin/env python3
"""
5-second-rule audio test.

Hard rules:
  - PRECONDITION: PCM must reach M5 (verify within 5s after play)
  - Each skip must produce SKIP_REQ on M5 within 5s
  - On any 5s timeout: IMMEDIATELY pause Chrome, stop, diagnose
"""
import importlib.util
import subprocess
import sys
import time

spec = importlib.util.spec_from_file_location(
    "es", "/Users/nicolasvonbruck/Desktop/M5_3/ensure_skip.py")
es = importlib.util.module_from_spec(spec)
spec.loader.exec_module(es)

DEADLINE = 5.0  # seconds, hard cap per op


def stop_audio():
    """Pause Chrome immediately. Used when test fails."""
    es.applescript_run_js(es.JS_PAUSE)


def fail(msg):
    print(f"✗ FAIL: {msg}")
    print("  → Stopping audio (pause Chrome)")
    stop_audio()
    print("  → Diagnose:")
    s = es.chrome_state()
    print(f"    chrome_state: {s}")
    audible_raw = subprocess.run(
        ["osascript", "-e",
         'tell application "Google Chrome"\nset n to 0\nrepeat with w in windows\nrepeat with t in tabs of w\ntry\nif audible of t then set n to n+1\nend try\nend repeat\nend repeat\nreturn n\nend tell'],
        capture_output=True, text=True).stdout.strip()
    print(f"    audible tabs: {audible_raw}")
    print(f"    serial last 3 lines:")
    log = es.SERIAL_LOG
    try:
        with open(log) as f:
            tail = f.readlines()[-3:]
            for ln in tail: print(f"      {ln.rstrip()}")
    except Exception:
        pass
    sys.exit(2)


def wait_for(check, label, deadline=DEADLINE, poll=0.2):
    """Returns True within deadline, else False. Uses fast polling."""
    t0 = time.time()
    while time.time() - t0 < deadline:
        if check():
            return time.time() - t0
        time.sleep(poll)
    return None


def main():
    es.kill_monitors()
    es.start_monitor(120)
    time.sleep(1.5)

    # ===== PRECONDITION =====
    print("[pre] Click Play in Chrome")
    es.applescript_run_js(es.JS_PLAY)

    print("[pre] Verify PCM arrives at M5 within 5s")
    pre_total = es.serial_pcm_total()
    found = wait_for(lambda: es.serial_pcm_total() > pre_total + 50_000,
                     "PCM growth")
    if not found:
        fail("PCM did not reach M5 within 5s — playback not actually on M5")
    print(f"  ✓ PCM arrived (delta {(es.serial_pcm_total()-pre_total)//1024}KB) in {found:.1f}s")

    # ===== TEST: 5 fwd skips, each with 5s window =====
    plan = ["fwd", "fwd", "back", "fwd", "back"]
    results = []
    for i, op in enumerate(plan):
        pre_skip = es.serial_count("[SKIP_REQ")
        js = es.JS_SKIP_BACK if op == "back" else es.JS_SKIP_FWD
        es.applescript_run_js(js)
        t = wait_for(lambda: es.serial_count("[SKIP_REQ") > pre_skip,
                     f"SKIP_REQ {op}")
        if t is None:
            fail(f"skip #{i+1} {op} did not produce SKIP_REQ within 5s")
        print(f"  ✓ skip #{i+1} {op} → SKIP_REQ in {t:.1f}s")
        results.append((op, t))

    # ===== POSTCONDITION: pause cleanly =====
    print("[end] Pause Chrome")
    es.applescript_run_js(es.JS_PAUSE)
    time.sleep(0.5)

    # ===== Quality summary =====
    log = es.SERIAL_LOG
    glitch = es.serial_count("[AUDIO_GLITCH")
    slow = es.serial_count("[I2S_SLOW")
    crash = es.serial_count("Guru") + es.serial_count("CRASH_DETECTED")
    pcm = es.serial_pcm_total() - pre_total

    print()
    print("=" * 50)
    print(f"PASS: {len(results)}/{len(plan)} skips verified within 5s")
    print(f"PCM streamed during test: {pcm//1024} KB")
    print(f"AUDIO_GLITCH: {glitch}")
    print(f"I2S_SLOW: {slow}")
    print(f"Crashes: {crash}")
    print("=" * 50)

    es.kill_monitors()
    return 0 if crash == 0 else 2


if __name__ == "__main__":
    sys.exit(main())
