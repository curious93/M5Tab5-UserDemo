#!/usr/bin/env python3
"""
Definitive M5Tab5 Spotify trigger.

Detects current state and only does what's needed:
- If M5 is already active device + paused → click Play
- If M5 is already active device + playing → no-op (success)
- If M5 not active → open picker, find M5 (skip 'Playing on...' to avoid toggle), click, verify connection, then Play
- If M5 not in picker at all → exit 2, do NOT click Play (would start in Chrome locally)

Exit codes:
  0  = M5 is now active and playing
  2  = failure (M5 not in picker, connect handshake failed, no Spotify tab)

Output: a single STATE=... line for caller parsing.
"""
import subprocess
import sys
import json

# AppleScript runs JS in Chrome's Spotify tab. We do 3 phases, each its own JS run,
# returning JSON. Python parses, decides next action, and never relies on JS state
# bleeding between calls.

JS_PHASE1_READSTATE = r"""
(function() {
  var devBtn = document.querySelector('[aria-label="Connect to a device"]') ||
               document.querySelector('[data-testid="device-picker-trigger"]');
  var devTxt = devBtn ? (devBtn.textContent || '').trim() : '';
  var play = document.querySelector('[data-testid="control-button-playpause"]');
  var playLabel = play ? play.getAttribute('aria-label') : 'none';
  var onM5 = /M5\s*Tab5/.test(devTxt);
  return JSON.stringify({onM5: onM5, devTxt: devTxt.substring(0,80), playLabel: playLabel});
})()
"""

JS_CLICK_PLAY_IF_PAUSED = r"""
(function() {
  var p = document.querySelector('[data-testid="control-button-playpause"]');
  if (!p) return JSON.stringify({clicked:false, reason:'no_play_btn'});
  if (p.getAttribute('aria-label') === 'Play') { p.click(); return JSON.stringify({clicked:true}); }
  return JSON.stringify({clicked:false, reason:'already_playing'});
})()
"""

JS_OPEN_PICKER = r"""
(function() {
  var b = document.querySelector('[aria-label="Connect to a device"]') ||
          document.querySelector('[data-testid="device-picker-trigger"]');
  if (b) { b.click(); return 'opened'; }
  return 'no_btn';
})()
"""

JS_PICK_M5 = r"""
(function() {
  var items = document.querySelectorAll('[data-testid="device-list-item"], [role="button"], button, li');
  var seen = [];
  for (var j = 0; j < items.length; j++) {
    var txt = (items[j].textContent || '').trim();
    if (txt && txt.length < 100) seen.push(txt.substring(0,50));
    // CRITICAL: skip 'Playing on M5Tab5' — clicking that would TOGGLE OFF
    if (/M5\s*Tab5/.test(txt) && !/Playing on/i.test(txt)) {
      items[j].click();
      return JSON.stringify({clicked: txt.substring(0,80)});
    }
  }
  return JSON.stringify({clicked: null, options: seen.slice(0,15)});
})()
"""

JS_VERIFY_AND_PLAY = r"""
(function() {
  var devBtn = document.querySelector('[aria-label="Connect to a device"]') ||
               document.querySelector('[data-testid="device-picker-trigger"]');
  var devTxt = devBtn ? (devBtn.textContent || '').trim() : '';
  var connected = /M5\s*Tab5/.test(devTxt);
  if (!connected) return JSON.stringify({connected:false, devTxt: devTxt.substring(0,80)});
  var p = document.querySelector('[data-testid="control-button-playpause"]');
  var label = p ? p.getAttribute('aria-label') : 'none';
  if (label === 'Play') { p.click(); return JSON.stringify({connected:true, action:'clicked_play'}); }
  return JSON.stringify({connected:true, action:'already', label:label});
})()
"""


def run_js_in_spotify_tab(js: str) -> str:
    """Run JS in the first Chrome tab whose URL contains open.spotify.com.
    Returns the JS return value (string)."""
    # Use AppleScript to find the tab and run JS, returning the JS result.
    # We use sys.argv-style escaping by writing JS to stdin via osascript -e.
    apple = f'''
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
          set theResult to execute t javascript jsCode
          return theResult
        end if
      end repeat
    end repeat
    return "NO_SPOTIFY_TAB"
  end tell
end run
'''
    p = subprocess.run(
        ["osascript", "-e", apple, js],
        capture_output=True, text=True, timeout=10,
    )
    return p.stdout.strip()


def sleep(s):
    import time; time.sleep(s)


JS_NEXT = r"""
(function() {
  var btn = document.querySelector('[data-testid="control-button-skip-forward"]') ||
            document.querySelector('[aria-label="Next"]');
  if (!btn) return JSON.stringify({skipped:false, reason:'no_next_btn'});
  btn.click();
  return JSON.stringify({skipped:true, dir:'fwd'});
})()
"""

JS_PREV = r"""
(function() {
  var btn = document.querySelector('[data-testid="control-button-skip-back"]') ||
            document.querySelector('[aria-label="Previous"]');
  if (!btn) return JSON.stringify({skipped:false, reason:'no_prev_btn'});
  btn.click();
  return JSON.stringify({skipped:true, dir:'back'});
})()
"""


def cmd_skip(direction="fwd"):
    """Trigger one Spotify skip via Chrome. direction = 'fwd' | 'back'."""
    js = JS_PREV if direction == "back" else JS_NEXT
    raw = run_js_in_spotify_tab(js)
    if raw == "NO_SPOTIFY_TAB":
        print("STATE=failed REASON=no_spotify_tab")
        return 2
    print(f"STATE=skip_sent DIR={direction} RAW={raw}")
    return 0


def main():
    import time
    # Sub-command support
    if len(sys.argv) > 1:
        cmd = sys.argv[1]
        if cmd == "skip" or cmd == "next":
            return cmd_skip("fwd")
        if cmd == "prev" or cmd == "back":
            return cmd_skip("back")
        if cmd == "random":
            import random
            d = random.choice(["fwd", "back"])
            return cmd_skip(d)
    # Phase 1: read current state
    s1_raw = run_js_in_spotify_tab(JS_PHASE1_READSTATE)
    if s1_raw == "NO_SPOTIFY_TAB":
        print("STATE=failed REASON=no_spotify_tab")
        return 2
    try:
        s1 = json.loads(s1_raw)
    except Exception as e:
        print(f"STATE=failed REASON=phase1_parse_error raw={s1_raw!r}")
        return 2

    if s1.get("onM5"):
        # Already on M5. Only click play if paused.
        if s1.get("playLabel") == "Play":
            run_js_in_spotify_tab(JS_CLICK_PLAY_IF_PAUSED)
            print(f"STATE=already_connected ACTION=clicked_play DEV={s1.get('devTxt')!r}")
        else:
            print(f"STATE=already_connected ACTION=already_playing DEV={s1.get('devTxt')!r} LABEL={s1.get('playLabel')}")
        return 0

    # Not on M5 → open picker
    open_result = run_js_in_spotify_tab(JS_OPEN_PICKER)
    if open_result == "no_btn":
        print("STATE=failed REASON=no_device_picker_btn")
        return 2
    sleep(1.5)

    # Pick M5 (skipping 'Playing on...' entries to avoid toggle-off)
    s2_raw = run_js_in_spotify_tab(JS_PICK_M5)
    try:
        s2 = json.loads(s2_raw)
    except Exception:
        print(f"STATE=failed REASON=phase2_parse_error raw={s2_raw!r}")
        return 2

    if not s2.get("clicked"):
        opts = s2.get("options", [])
        # Check if M5 is in picker AS "Playing on M5Tab5" — that means it IS the
        # active device, just paused. Don't click it (toggles off). Close picker
        # and click Play instead.
        already_active = any("Playing on" in o and "M5" in o for o in opts)
        # Close picker (Escape key or click outside)
        run_js_in_spotify_tab("document.body.click()")
        sleep(0.5)
        if already_active:
            play_raw = run_js_in_spotify_tab(JS_CLICK_PLAY_IF_PAUSED)
            print(f"STATE=already_connected ACTION=closed_picker_clicked_play RAW={play_raw}")
            return 0
        # Truly not in picker
        print(f"STATE=failed REASON=m5_not_in_picker OPTIONS={opts}")
        return 2

    sleep(3.5)  # Spotify Connect handshake

    # Verify connection took effect, then play
    s3_raw = run_js_in_spotify_tab(JS_VERIFY_AND_PLAY)
    try:
        s3 = json.loads(s3_raw)
    except Exception:
        print(f"STATE=failed REASON=phase3_parse_error raw={s3_raw!r}")
        return 2

    if not s3.get("connected"):
        print(f"STATE=failed REASON=connect_handshake_failed DEV={s3.get('devTxt')!r}")
        return 2

    print(f"STATE=connected ACTION={s3.get('action')} S2_clicked={s2.get('clicked')!r}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
