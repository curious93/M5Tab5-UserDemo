#!/usr/bin/env python3
"""
Structured 10-minute Chrome→M5 test loop.

Plan (repeats N times in 10 min):
  - 4s audio    → skip_fwd  → 3s verify
  - 4s audio    → skip_fwd  → 3s verify
  - 4s audio    → skip_back → 3s verify
  - 4s audio    → pause     → 4s silence verify (PCM delta < 50 KB)
  - resume play → next iteration

Final: pause + reflash for absolute silence.
"""
import importlib.util
import sys
import time

spec = importlib.util.spec_from_file_location(
    "es", "/Users/nicolasvonbruck/Desktop/M5_3/ensure_skip.py")
es = importlib.util.module_from_spec(spec)
spec.loader.exec_module(es)


def verify_skip(direction):
    """Send skip, verify SKIP_REQ on serial within 3s."""
    pre = es.serial_count("[SKIP_REQ")
    js = es.JS_SKIP_BACK if direction == "back" else es.JS_SKIP_FWD
    es.applescript_run_js(js)
    time.sleep(2.5)
    post = es.serial_count("[SKIP_REQ")
    return post > pre, post - pre


def verify_pause():
    """Pause, verify PCM total stops growing within 4s."""
    pre = es.serial_pcm_total()
    es.applescript_run_js(es.JS_PAUSE)
    time.sleep(4)
    post = es.serial_pcm_total()
    delta_kb = (post - pre) // 1024
    # Some tail from buffer drains (1MB max ~ 1024 KB), so threshold 200 KB
    return delta_kb < 200, delta_kb


def verify_play():
    """Click play, verify PCM grows within 4s."""
    pre = es.serial_pcm_total()
    es.applescript_run_js(es.JS_PLAY)
    time.sleep(4)
    post = es.serial_pcm_total()
    delta_kb = (post - pre) // 1024
    return delta_kb > 100, delta_kb


def heal():
    """Reconnect Chrome→M5 binding via picker."""
    print("    [heal] reconnect…")
    if es.reconnect_chrome_to_m5():
        return True
    print("    [heal] reflash…")
    es.reflash()
    return es.reconnect_chrome_to_m5()


def main():
    duration_s = 10 * 60
    start = time.time()

    # Initial: ensure playback established + verified
    print("[init] ensure_play")
    rc = es.ensure_play()
    if rc != 0:
        print("ABORT: cannot start playback")
        return 2

    # Op plan per iteration: ~22s
    iter_plan = [
        ("audio", 4),
        ("skip_fwd", 0),
        ("audio", 4),
        ("skip_fwd", 0),
        ("audio", 4),
        ("skip_back", 0),
        ("audio", 4),
        ("pause", 0),
        ("play", 0),
    ]
    op_log = []
    iteration = 0

    while time.time() - start < duration_s:
        iteration += 1
        iter_t0 = time.time()
        elapsed = int(time.time() - start)
        print(f"\n[{elapsed:4d}s][iter {iteration}]")

        for op, sleep_s in iter_plan:
            t = int(time.time() - start)
            if op == "audio":
                time.sleep(sleep_s)
            elif op in ("skip_fwd", "skip_back"):
                ok, n = verify_skip("fwd" if op == "skip_fwd" else "back")
                flag = "✓" if ok else "✗"
                print(f"  [{t:4d}s] {flag} {op} (SKIP_REQ delta={n})")
                op_log.append((t, op, ok))
                if not ok:
                    healed = heal()
                    if healed:
                        es.applescript_run_js(es.JS_PLAY); time.sleep(2)
            elif op == "pause":
                ok, kb = verify_pause()
                flag = "✓" if ok else "✗"
                print(f"  [{t:4d}s] {flag} pause (post-pause delta={kb}KB)")
                op_log.append((t, op, ok))
            elif op == "play":
                ok, kb = verify_play()
                flag = "✓" if ok else "✗"
                print(f"  [{t:4d}s] {flag} play (post-play delta={kb}KB)")
                op_log.append((t, op, ok))
                if not ok:
                    healed = heal()
                    if healed:
                        es.applescript_run_js(es.JS_PLAY); time.sleep(2)

        # ~22s per iteration, allow up to remaining time
        if time.time() - start >= duration_s - 5:
            break

    # End cleanly: pause + reflash for absolute silence
    print("\n[end] pause Chrome + reflash M5 for silence")
    es.applescript_run_js(es.JS_PAUSE)
    time.sleep(1)
    es.reflash()  # also stops monitor

    # Summary
    total_elapsed = int(time.time() - start)
    glitch = es.serial_count("[AUDIO_GLITCH")
    slow = es.serial_count("[I2S_SLOW")
    crash = es.serial_count("Guru") + es.serial_count("CRASH_DETECTED")
    skip_req = es.serial_count("[SKIP_REQ")
    track_loads = es.serial_count("TRACK_LOAD ready")
    n_ok = sum(1 for _, _, ok in op_log if ok)
    n_total = len(op_log)
    n_skip_ops = sum(1 for _, op, _ in op_log if "skip" in op)

    print("\n" + "=" * 60)
    print(f"FINAL — {total_elapsed}s, {iteration} iterations")
    print(f"Operations OK: {n_ok}/{n_total}  ({100*n_ok//max(1,n_total)}%)")
    print(f"Of which skip ops: {n_skip_ops}, SKIP_REQ on M5: {skip_req}")
    print(f"TRACK_LOAD ready on M5: {track_loads}")
    print(f"AUDIO_GLITCH: {glitch}")
    print(f"I2S_SLOW: {slow}")
    print(f"Crashes: {crash}")
    print("=" * 60)

    if op_log:
        print("\nFailing ops:")
        for ts, op, ok in op_log:
            if not ok:
                print(f"  [{ts:4d}s] {op}")

    return 0 if (n_ok == n_total and crash == 0) else 2


if __name__ == "__main__":
    sys.exit(main())
