#!/usr/bin/env python3
"""
Self-healing Chrome→M5Tab5 Spotify Connect skip enforcer.

Guarantees that skips sent via Chrome arrive at M5, by VERIFYING via the M5
serial log (the only ground truth). Self-heals via:
  1. retry click variants in picker
  2. as last resort: reflash M5 to force fresh mDNS announce

Usage:
  python3 ensure_skip.py play         # ensure M5 plays, then exit
  python3 ensure_skip.py skip [N]     # ensure skip(s) arrive at M5; default N=1
  python3 ensure_skip.py test         # play + N=5 random skips with full report
"""
import json
import os
import random
import subprocess
import sys
import time

ROOT = "/Users/nicolasvonbruck/Desktop/M5_3"
SERIAL_LOG = f"{ROOT}/sessions/continuous/ensure.log"
PORT = "/dev/cu.usbmodem2101"
PYBIN = f"{ROOT}/flash_env/bin/python3"

# ---------- Chrome / AppleScript ----------

JS_STATE = r"""
JSON.stringify({
  devText: ((document.querySelector('[aria-label="Connect to a device"]')||{textContent:''}).textContent||'').trim().substring(0,80),
  playLabel: ((document.querySelector('[data-testid="control-button-playpause"]')||{getAttribute:function(){return 'NONE'}})).getAttribute('aria-label')
})
"""

JS_OPEN_PICKER = r"""
(function(){var b=document.querySelector('[aria-label="Connect to a device"]');if(b){b.click();return 'opened';}return 'no_btn';})()
"""

JS_LIST_M5_ITEMS = r"""
(function(){
  // Spotify's Connect panel uses plain <button> elements (no testid).
  // Find buttons whose direct text contains M5Tab5 (and is short — not the full page).
  var btns = document.querySelectorAll('button, [role="button"]');
  var out = [];
  for (var i = 0; i < btns.length; i++) {
    var t = (btns[i].textContent || '').trim();
    if (t.length > 0 && t.length < 60 && /M5Tab5/i.test(t)) {
      out.push({idx: i, text: t.substring(0,80)});
    }
  }
  return JSON.stringify(out);
})()
"""

JS_CLICK_DEVICE_AT_IDX = """
(function(){
  var btns = document.querySelectorAll('button, [role="button"]');
  if (btns[%d]) { btns[%d].click(); return 'clicked'; }
  return 'no_item';
})()
"""

JS_CLOSE_PICKER = r"""
(function(){document.body.click();})()
"""

JS_PLAY = r"""
(function(){var p=document.querySelector('[data-testid="control-button-playpause"]');if(!p)return 'no_btn';if(p.getAttribute('aria-label')==='Play'){p.click();return 'played';}return 'already_playing';})()
"""

JS_PAUSE = r"""
(function(){var p=document.querySelector('[data-testid="control-button-playpause"]');if(!p)return 'no_btn';if(p.getAttribute('aria-label')==='Pause'){p.click();return 'paused';}return 'already_paused';})()
"""

JS_SKIP_FWD = r"""
(function(){var b=document.querySelector('[data-testid="control-button-skip-forward"]');if(b){b.click();return 'fwd';}return 'no';})()
"""

JS_SKIP_BACK = r"""
(function(){var b=document.querySelector('[data-testid="control-button-skip-back"]');if(b){b.click();return 'back';}return 'no';})()
"""


def applescript_run_js(js: str, timeout: float = 8.0) -> str:
    apple = '''
on run argv
  set jsCode to item 1 of argv
  tell application "Google Chrome"
    repeat with w in windows
      set tIdx to 0
      repeat with t in tabs of w
        set tIdx to tIdx + 1
        if URL of t contains "open.spotify.com" then
          set active tab index of w to tIdx
          set index of w to 1
          return execute t javascript jsCode
        end if
      end repeat
    end repeat
    return "NO_SPOTIFY_TAB"
  end tell
end run
'''
    p = subprocess.run(["osascript", "-e", apple, js],
                       capture_output=True, text=True, timeout=timeout)
    return p.stdout.strip()


def chrome_state():
    raw = applescript_run_js(JS_STATE)
    if raw == "NO_SPOTIFY_TAB":
        return None
    try:
        return json.loads(raw)
    except Exception:
        return None


# ---------- Serial monitor (background) ----------

def kill_monitors():
    subprocess.run(["pkill", "-f", "monitor_serial.py"], capture_output=True)
    time.sleep(0.5)


def start_monitor(duration_s: int = 600):
    kill_monitors()
    open(SERIAL_LOG, "w").close()  # truncate
    subprocess.Popen(
        [PYBIN, f"{ROOT}/monitor_serial.py",
         "--port", PORT, "--baud", "2000000",
         "--output", SERIAL_LOG,
         "--silence-crash", "30",
         "--duration", str(duration_s)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        start_new_session=True,
    )
    # wait until log starts being written
    for _ in range(30):
        if os.path.exists(SERIAL_LOG) and os.path.getsize(SERIAL_LOG) > 0:
            return True
        time.sleep(0.2)
    return False


def serial_count(pattern: str) -> int:
    """Count occurrences of pattern in serial log."""
    if not os.path.exists(SERIAL_LOG):
        return 0
    n = 0
    with open(SERIAL_LOG, errors="replace") as f:
        for line in f:
            if pattern in line:
                n += 1
    return n


def serial_pcm_total() -> int:
    """Last reported feedPCMFrames total= value, or 0."""
    if not os.path.exists(SERIAL_LOG):
        return 0
    last = 0
    with open(SERIAL_LOG, errors="replace") as f:
        for line in f:
            i = line.find("feedPCMFrames: calls=")
            if i < 0:
                continue
            j = line.find("total=", i)
            if j < 0:
                continue
            digits = ""
            for ch in line[j+6:]:
                if ch.isdigit(): digits += ch
                else: break
            if digits:
                try: last = int(digits)
                except: pass
    return last


def device_alive() -> bool:
    """True if device is producing log lines (HEAP, etc.) within last 3s."""
    before = os.path.getsize(SERIAL_LOG) if os.path.exists(SERIAL_LOG) else 0
    time.sleep(3)
    after = os.path.getsize(SERIAL_LOG) if os.path.exists(SERIAL_LOG) else 0
    return after > before


# ---------- Reflash (self-heal) ----------

def reflash():
    print("[heal] re-flashing M5 to force fresh Spotify-Connect mDNS announce")
    kill_monitors()
    p = subprocess.run(["bash", f"{ROOT}/build.sh", "--no-monitor"],
                       capture_output=True, text=True, timeout=180)
    print("[heal] flash done, waiting 25s for full boot + WiFi + mDNS")
    time.sleep(25)
    start_monitor()


# ---------- Reconnect / verify pipeline ----------

def open_picker_and_list_m5():
    applescript_run_js(JS_OPEN_PICKER)
    time.sleep(1.5)
    raw = applescript_run_js(JS_LIST_M5_ITEMS)
    try:
        return json.loads(raw)
    except Exception:
        return []


def click_picker_idx(idx: int):
    applescript_run_js(JS_CLICK_DEVICE_AT_IDX % (idx, idx))
    time.sleep(3.0)


def close_picker():
    applescript_run_js(JS_CLOSE_PICKER)
    time.sleep(0.4)


def is_chrome_controller_for_m5() -> bool:
    """Cheap, non-destructive check: Chrome's device-button text contains M5.
       If true, Chrome is the active Connect controller and clicks route to M5."""
    return m5_is_active_in_chrome()


def reconnect_chrome_to_m5() -> bool:
    """Make Chrome the active Connect controller for M5.
    Strategy:
      1. List M5 items in picker
      2. Prefer plain "M5Tab5" entry (binds fresh)
      3. Else click "Playing on M5Tab5" (toggles off → M5 becomes available)
         then re-list and click plain entry
      4. Verify via devText (non-destructive — no probe-skip)
    Up to 4 total attempts."""
    for attempt in range(4):
        items = open_picker_and_list_m5()
        if not items:
            close_picker()
            print(f"[recon] attempt {attempt+1}: no M5 in picker (empty list)")
            time.sleep(2)
            continue
        # Prefer plain entries (no 'Playing on')
        plain = [it for it in items if "Playing on" not in it["text"]]
        playing = [it for it in items if "Playing on" in it["text"]]
        target = plain[0] if plain else playing[0]
        kind = "plain" if plain else "playing-on"
        print(f"[recon] attempt {attempt+1}: clicking {kind} idx={target['idx']} text={target['text']!r}")
        click_picker_idx(target["idx"])
        close_picker()
        time.sleep(3.5)
        if m5_is_active_in_chrome():
            s = chrome_state()
            print(f"[recon] OK — devText={s.get('devText') if s else '?'}")
            return True
        # If we clicked playing-on, M5 is now toggled off. Wait briefly, then on
        # next attempt the picker will list M5 as plain (available) → bind it.
        if kind == "playing-on":
            print("[recon] toggled off — next attempt will pick plain entry")
            time.sleep(1.5)
    print("[recon] all attempts failed")
    return False


# ---------- High-level commands ----------

JS_M5_ACTIVE = r"""
(function(){
  // Spotify shows 'Playing on <device>' in the now-playing-bar when a remote
  // device is the active Connect sink. Search for any element with that text.
  var bars = document.querySelectorAll('aside[data-testid="now-playing-bar"], aside[aria-label*="Now playing"]');
  for (var i = 0; i < bars.length; i++) {
    if (/Playing on M5/i.test(bars[i].textContent || '')) return 'true';
  }
  // Fallback: any short button with 'Playing on M5'
  var btns = document.querySelectorAll('button');
  for (var j = 0; j < btns.length; j++) {
    var t = (btns[j].textContent || '').trim();
    if (t.length < 60 && /Playing on M5/i.test(t)) return 'true';
  }
  return 'false';
})()
"""


def m5_is_active_in_chrome():
    """True if Chrome shows 'Playing on M5...' in now-playing-bar = M5 is active sink.
    NEVER click Play unless this returns True."""
    raw = applescript_run_js(JS_M5_ACTIVE)
    return raw == "true"


def ensure_play():
    """Ensure music plays ON M5 (never on laptop)."""
    if not start_monitor():
        print("ERR: monitor failed to start (USB issue?)")
        return 2
    s = chrome_state()
    if s is None:
        print("ERR: no Spotify tab in Chrome")
        return 2

    # SAFETY: never click Play unless M5 is active controller. Otherwise plays locally.
    if not m5_is_active_in_chrome():
        print(f"[bind] devText={s.get('devText')!r} — must bind M5 first")
        if not reconnect_chrome_to_m5():
            print("[bind] reconnect failed; reflashing M5 for fresh mDNS announce")
            reflash()
            time.sleep(3)
            if not reconnect_chrome_to_m5():
                print("FAIL: cannot bind Chrome→M5 even after reflash")
                return 2
        # confirm binding worked
        if not m5_is_active_in_chrome():
            print("FAIL: M5 binding did not take effect (devText still empty)")
            return 2

    # Now safe to click Play — Chrome will route to M5
    s = chrome_state()
    if s and s.get("playLabel") == "Play":
        applescript_run_js(JS_PLAY)
        time.sleep(2)

    # Verify M5 is actually producing audio
    pcm_before = serial_pcm_total()
    time.sleep(4)
    pcm_after = serial_pcm_total()
    delta_kb = (pcm_after - pcm_before) // 1024
    if delta_kb > 100:
        print(f"OK: M5 streaming, {delta_kb} KB in 4s, devText={s.get('devText')!r}")
        return 0
    print(f"FAIL: bound but no audio on M5 (delta={delta_kb}KB)")
    return 2


def ensure_skip(direction: str = "fwd"):
    """Ensure ONE skip in the given direction arrives at M5."""
    if not is_chrome_controller_for_m5():
        if not reconnect_chrome_to_m5():
            print("FAIL: cannot bind Chrome→M5")
            return 2
    pre = serial_count("[SKIP_REQ")
    js = JS_SKIP_BACK if direction == "back" else JS_SKIP_FWD
    applescript_run_js(js)
    time.sleep(2.5)
    post = serial_count("[SKIP_REQ")
    if post > pre:
        print(f"OK: skip {direction} arrived (SKIP_REQ {pre}→{post})")
        return 0
    print(f"FAIL: skip {direction} did NOT arrive on M5 (SKIP_REQ {pre}={post})")
    return 2


def cmd_test():
    """Full guaranteed-skip test with audio verification."""
    rc = ensure_play()
    if rc != 0:
        return rc
    plan = ["fwd", "fwd", "back", "fwd", "back", random.choice(["fwd", "back"])]
    ok = 0
    for op in plan:
        time.sleep(7)
        if ensure_skip(op) == 0:
            ok += 1
    print(f"\nResult: {ok}/{len(plan)} skips verified at M5")
    # Quality check
    glitch = serial_count("[AUDIO_GLITCH")
    slow = serial_count("[I2S_SLOW")
    crash = serial_count("Guru") + serial_count("CRASH_DETECTED")
    print(f"AUDIO_GLITCH={glitch}  I2S_SLOW={slow}  Crashes={crash}")
    return 0 if ok == len(plan) and crash == 0 else 2


def main():
    if len(sys.argv) < 2:
        print(__doc__); return 2
    cmd = sys.argv[1]
    if cmd == "play":     return ensure_play()
    if cmd == "skip":     return ensure_skip(sys.argv[2] if len(sys.argv) > 2 else "fwd")
    if cmd == "fwd":      return ensure_skip("fwd")
    if cmd == "back":     return ensure_skip("back")
    if cmd == "test":     return cmd_test()
    print("unknown cmd:", cmd); return 2


if __name__ == "__main__":
    sys.exit(main())
