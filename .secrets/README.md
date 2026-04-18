# .secrets/

Gitignored project-local secret storage. **Never committed.**

## Files
- `cspot_blob.json` — Spotify reusable-auth-credentials captured from Tab5 after a successful ZeroConf claim. Used to auto-login without iPhone tap after factory reset.
  - Written by `~/.claude/hooks/cspot_blob_scraper.py` (fires on PostToolUse/Bash)
  - Read by `~/.claude/hooks/cspot_blob_gen_header.py` during `idf.py build` to embed as `CSPOT_EMBEDDED_BLOB_JSON` in firmware
  - Mirror: also kept at `~/.claude/tab5/cspot_blob.json` (host-level backup)
  - Format: `{"username": "...", "authType": 1, "authData": "<base64>"}`
  - Rotate: delete this file and the Tab5 NVS + reflash → one iPhone tap → new blob captured automatically

## Recovery
If both `.secrets/cspot_blob.json` and `~/.claude/tab5/cspot_blob.json` are lost but Tab5 NVS still has the blob:
1. Boot Tab5 with current firmware
2. Check `~/.claude/tab5/serial_logs/live.log` for the last `CSPOT_BLOB_BEGIN ... CSPOT_BLOB_END` line
3. Extract the JSON between those markers and paste into `.secrets/cspot_blob.json`

If nothing is salvageable: one iPhone tap re-claims and repopulates both locations automatically.
