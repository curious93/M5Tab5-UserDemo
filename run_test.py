#!/usr/bin/env python3
"""
The actual test. Single shot, no iteration:
  - Reflash M5
  - Play (verify PCM in 8s — Spotify Connect handshake is 5-7s legit)
  - 8 mixed skips: fwd, fwd, back, fwd, back, random, fwd, random
  - Each skip verified via SKIP_REQ within 5s
  - On any 5s skip-fail: pause Chrome immediately
  - Final: pause + reflash for silence
  - Report glitches/crashes during the test
"""
import importlib.util
import random
import subprocess
import sys
import time

spec = importlib.util.spec_from_file_location(
    "es", "/Users/nicolasvonbruck/Desktop/M5_3/ensure_skip.py")
es = importlib.util.module_from_spec(spec)
spec.loader.exec_module(es)


def wait_until(cond, deadline_s, poll=0.2):
    t0 = time.time()
    while time.time() - t0 < deadline_s:
        if cond():
            return time.time() - t0
        time.sleep(poll)
    return None


def main():
    # 1. Reflash for clean state
    print("[1] reflash...")
    es.kill_monitors()
    subprocess.run(["bash", "/Users/nicolasvonbruck/Desktop/M5_3/build.sh", "--no-monitor"],
                   capture_output=True, timeout=180)
    print("[1] boot 22s...")
    time.sleep(22)

    # 2. Start monitor + click Play
    print("[2] start monitor + click Play")
    es.start_monitor(180)
    time.sleep(1.5)

    # Click Play (we don't pre-check binding; rely on PCM arrival as verification)
    es.applescript_run_js(es.JS_PLAY)

    # PCM arrival can take up to 8s (Spotify backend handshake + track fetch)
    pre_pcm = es.serial_pcm_total()
    t = wait_until(
        lambda: es.serial_pcm_total() > pre_pcm + 50_000,
        deadline_s=10, poll=0.3,
    )
    if t is None:
        # PCM did not arrive — try clicking Play once more in case it was paused
        print("  PCM not arriving — retrying Play click")
        es.applescript_run_js(es.JS_PLAY)
        t = wait_until(
            lambda: es.serial_pcm_total() > pre_pcm + 50_000,
            deadline_s=8, poll=0.3,
        )
    if t is None:
        print("✗ FAIL: Spotify→M5 playback did not start. Pausing + reflashing.")
        es.applescript_run_js(es.JS_PAUSE)
        subprocess.run(["bash", "/Users/nicolasvonbruck/Desktop/M5_3/build.sh", "--no-monitor"],
                       capture_output=True, timeout=180)
        return 2
    delta_kb = (es.serial_pcm_total() - pre_pcm) // 1024
    print(f"  ✓ PCM arrived in {t:.1f}s ({delta_kb} KB streamed)")

    # 3. Mixed skips
    plan = ["fwd", "fwd", "back", "fwd", "back", "random", "fwd", "random"]
    results = []
    for i, op in enumerate(plan, 1):
        actual = op
        if op == "random":
            actual = random.choice(["fwd", "back"])
        pre_skip = es.serial_count("[SKIP_REQ")
        js = es.JS_SKIP_BACK if actual == "back" else es.JS_SKIP_FWD
        es.applescript_run_js(js)
        t = wait_until(
            lambda: es.serial_count("[SKIP_REQ") > pre_skip,
            deadline_s=5, poll=0.2,
        )
        if t is None:
            print(f"  ✗ skip #{i} {actual} (random→{actual} if rand) NO SKIP_REQ in 5s — PAUSING")
            es.applescript_run_js(es.JS_PAUSE)
            results.append((op, actual, False, None))
            break
        else:
            print(f"  ✓ skip #{i} {actual:4s} → SKIP_REQ in {t:.1f}s")
            results.append((op, actual, True, t))
        # short gap between skips so audio plays a bit
        time.sleep(1.5)

    # 4. End: pause + reflash
    print("[4] pause + reflash for silence")
    es.applescript_run_js(es.JS_PAUSE)
    time.sleep(0.5)
    # capture metrics before kill
    log = es.SERIAL_LOG
    glitch = es.serial_count("[AUDIO_GLITCH")
    slow = es.serial_count("[I2S_SLOW")
    crash = es.serial_count("Guru") + es.serial_count("CRASH_DETECTED")
    skip_done = es.serial_count("[SKIP_DONE")
    track_loads = es.serial_count("TRACK_LOAD ready")
    pcm_total = (es.serial_pcm_total() - pre_pcm) // 1024 // 1024
    es.kill_monitors()

    n_ok = sum(1 for *_, ok, _ in [(*r,) for r in results] for ok in [r[2]] if ok)
    n_total = len(plan)

    print()
    print("=" * 60)
    print(f"RESULT: {n_ok}/{n_total} skips verified at M5")
    print(f"PCM streamed during test: {pcm_total} MB")
    print(f"SKIP_DONE on M5:    {skip_done}")
    print(f"TRACK_LOAD ready:   {track_loads}")
    print(f"AUDIO_GLITCH:       {glitch}")
    print(f"I2S_SLOW:           {slow}")
    print(f"Crashes:            {crash}")
    if glitch > 0:
        gaps = subprocess.run(
            ["bash", "-c", f"grep -oE 'gap=[0-9]+' '{log}' | sort -t= -k2 -n | tail -5"],
            capture_output=True, text=True).stdout.strip()
        print(f"Worst gaps:\n{gaps}")
    print("=" * 60)

    # final reflash for absolute silence
    subprocess.run(["bash", "/Users/nicolasvonbruck/Desktop/M5_3/build.sh", "--no-monitor"],
                   capture_output=True, timeout=180)
    print("[5] M5 silenced.")
    return 0 if n_ok == n_total and crash == 0 else 2


if __name__ == "__main__":
    sys.exit(main())
