#!/usr/bin/env python3
"""
10-minute autonomous Chrome→M5 control loop.

Random ops every 5-15s: skip fwd, skip back, pause, play.
Each op is verified via Serial log. On failure: auto-heal (reconnect or reflash).
Runs ensure_skip.py functions internally.
"""
import importlib.util
import random
import sys
import time

spec = importlib.util.spec_from_file_location(
    "es", "/Users/nicolasvonbruck/Desktop/M5_3/ensure_skip.py")
es = importlib.util.module_from_spec(spec)
spec.loader.exec_module(es)


def main():
    duration_s = 10 * 60
    start = time.time()
    op_log = []
    print("[loop] starting 10-min Chrome control loop")
    rc = es.ensure_play()
    if rc != 0:
        print("[loop] FATAL: cannot start playback, abort")
        return 2

    iteration = 0
    while time.time() - start < duration_s:
        iteration += 1
        elapsed = int(time.time() - start)
        op = random.choice(["fwd", "fwd", "back", "fwd", "fwd", "pause_play"])
        print(f"\n[{elapsed}s][it={iteration}] op={op}")

        if op == "pause_play":
            # pause briefly then resume — exercises play/pause via Connect
            pre_pcm = es.serial_pcm_total()
            es.applescript_run_js(es.JS_PAUSE)
            time.sleep(2.5)
            mid_pcm = es.serial_pcm_total()
            es.applescript_run_js(es.JS_PLAY)
            time.sleep(3)
            end_pcm = es.serial_pcm_total()
            paused_correctly = (mid_pcm - pre_pcm) < 200_000  # <200 KB during pause = effectively stopped
            resumed = (end_pcm - mid_pcm) > 100_000
            ok = paused_correctly and resumed
            op_log.append((elapsed, "pause_play", ok))
            print(f"  paused_delta={(mid_pcm-pre_pcm)//1024}KB resume_delta={(end_pcm-mid_pcm)//1024}KB ok={ok}")
            if not ok:
                # heal
                if es.reconnect_chrome_to_m5():
                    es.applescript_run_js(es.JS_PLAY); time.sleep(2)
                else:
                    print("  [heal] reconnect failed; reflashing")
                    es.reflash()
                    es.reconnect_chrome_to_m5()
                    es.applescript_run_js(es.JS_PLAY); time.sleep(2)
        else:
            rc = es.ensure_skip(op)
            ok = (rc == 0)
            op_log.append((elapsed, op, ok))

        # Random idle between ops 5-15s
        idle = random.uniform(5, 15)
        time.sleep(idle)

    # Summary
    elapsed = int(time.time() - start)
    glitch = es.serial_count("[AUDIO_GLITCH")
    slow = es.serial_count("[I2S_SLOW")
    crash = es.serial_count("Guru") + es.serial_count("CRASH_DETECTED")
    skip_req = es.serial_count("[SKIP_REQ")
    track_loads = es.serial_count("TRACK_LOAD ready")
    pcm_total = es.serial_pcm_total()
    n_ok = sum(1 for _, _, ok in op_log if ok)
    n_total = len(op_log)
    print("\n" + "=" * 50)
    print(f"LOOP RESULT — {elapsed}s")
    print(f"Operations: {n_ok}/{n_total} verified at M5")
    print(f"SKIP_REQ at M5: {skip_req}")
    print(f"TRACK_LOAD ready: {track_loads}")
    print(f"PCM total streamed: {pcm_total/1024/1024:.1f} MB")
    print(f"AUDIO_GLITCH: {glitch}")
    print(f"I2S_SLOW: {slow}")
    print(f"Crashes: {crash}")
    print("\nPer-op breakdown:")
    for ts, op, ok in op_log:
        flag = "✓" if ok else "✗"
        print(f"  [{ts:4d}s] {flag} {op}")
    return 0 if n_ok == n_total and crash == 0 else 2


if __name__ == "__main__":
    sys.exit(main())
