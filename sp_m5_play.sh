#!/bin/bash
# Definitive Spotify M5Tab5 trigger
# - Detects current connection state, never toggles off
# - Verifies M5 is connected BEFORE clicking Play
# - Returns non-zero exit code on failure (so caller can fail-fast)
#
# Output (single line, parseable):
#   STATE=already|connected|failed REASON=<text>

set -uo pipefail

RESULT=$(osascript <<'EOF' 2>/dev/null
tell application "Google Chrome"
  repeat with w in windows
    set tIdx to 0
    repeat with t in tabs of w
      set tIdx to tIdx + 1
      if URL of t contains "open.spotify.com" then
        set active tab index of w to tIdx
        set index of w to 1
        delay 0.4
        -- Phase 1: read state. Is M5 already active?
        set s1 to execute t javascript "
          (function() {
            var devBtn = document.querySelector('[aria-label=\"Connect to a device\"]') ||
                         document.querySelector('[data-testid=\"device-picker-trigger\"]');
            var devTxt = devBtn ? (devBtn.textContent||'').trim() : '';
            var play = document.querySelector('[data-testid=\"control-button-playpause\"]');
            var playLabel = play ? play.getAttribute('aria-label') : 'none';
            // 'Playing on M5Tab5' OR similar = already on M5
            var onM5 = /M5\\s*Tab5/.test(devTxt);
            return JSON.stringify({onM5: onM5, devTxt: devTxt.substring(0,80), playLabel: playLabel});
          })()
        "
        -- AppleScript can't parse JSON; use string match
        if s1 contains "\"onM5\":true" then
          -- Already connected. Only click play if paused.
          if s1 contains "\"playLabel\":\"Play\"" then
            execute t javascript "(function(){var p=document.querySelector('[data-testid=\"control-button-playpause\"]');if(p)p.click();})()"
            delay 0.5
            return "STATE=already_connected ACTION=clicked_play S1=" & s1
          else
            return "STATE=already_connected ACTION=already_playing S1=" & s1
          end if
        end if
        -- Not on M5 → must connect. Open picker.
        execute t javascript "
          (function() {
            var b = document.querySelector('[aria-label=\"Connect to a device\"]') ||
                    document.querySelector('[data-testid=\"device-picker-trigger\"]');
            if (b) b.click();
          })()
        "
        delay 1.8
        -- Find M5 list item. CRITICAL: skip items containing 'Playing on' (those toggle off)
        set s2 to execute t javascript "
          (function() {
            var items = document.querySelectorAll('[data-testid=\"device-list-item\"], [role=\"button\"], button, li');
            var seen = [];
            for (var j = 0; j < items.length; j++) {
              var txt = (items[j].textContent || '').trim();
              if (txt.length < 100) seen.push(txt.substring(0,40));
              // Match standalone M5Tab5 / 'Connect to this deviceM5Tab5' but NOT 'Playing on...'
              if ((/M5\\s*Tab5/.test(txt)) && !/Playing on/i.test(txt)) {
                items[j].click();
                return JSON.stringify({clicked: txt.substring(0,80)});
              }
            }
            return JSON.stringify({clicked: null, options: seen.filter(function(x){return x}).slice(0,15)});
          })()
        "
        if s2 does not contain "\"clicked\":\"" then
          -- M5 not in list — FAIL, do NOT click Play (would play locally in Chrome)
          execute t javascript "document.body.click()"
          return "STATE=failed REASON=m5_not_in_picker S2=" & s2
        end if
        delay 3.5  -- wait for Spotify Connect handshake
        -- Phase 3: verify connection THEN click play
        set s3 to execute t javascript "
          (function() {
            var devBtn = document.querySelector('[aria-label=\"Connect to a device\"]') ||
                         document.querySelector('[data-testid=\"device-picker-trigger\"]');
            var devTxt = devBtn ? (devBtn.textContent||'').trim() : '';
            var connected = /M5\\s*Tab5/.test(devTxt);
            if (!connected) return JSON.stringify({connected: false, devTxt: devTxt.substring(0,80)});
            var play = document.querySelector('[data-testid=\"control-button-playpause\"]');
            var label = play ? play.getAttribute('aria-label') : 'none';
            if (label === 'Play') {
              play.click();
              return JSON.stringify({connected: true, action: 'clicked_play'});
            }
            return JSON.stringify({connected: true, action: 'already', label: label});
          })()
        "
        if s3 contains "\"connected\":true" then
          return "STATE=connected S3=" & s3
        else
          return "STATE=failed REASON=connect_handshake_failed S3=" & s3
        end if
      end if
    end repeat
  end repeat
  return "STATE=failed REASON=no_spotify_tab"
end tell
EOF
)

echo "$RESULT"
case "$RESULT" in
  *"STATE=already_connected"*|*"STATE=connected"*) exit 0 ;;
  *) exit 2 ;;
esac
