# M5_3 Autonomer Workflow — Cheat Sheet

Schritt-für-Schritt für jeden Test-Zyklus, damit künftige Sessions sofort reinkommen.

---

## Standard-Iteration (alles in einem Befehl)

```bash
cd /Users/nicolasvonbruck/Desktop/M5_3
./build.sh                    # Build + Flash + Monitor + Verdict
```

Verdict-Pfad: `sessions/latest/verdict.json`

---

## Skip-Performance-Test (Phase 4 + 5)

```bash
# 1. Tab5 anschließen, Spotify-App auf Tab5 spielt einen Track
# 2. Test starten
./build.sh --no-build --skip-test
# 3. Während die 60s laufen: 10× Skip-Forward via Touch antippen
# 4. Verdict ansehen
cat sessions/latest/verdict.json
# 5. Mit vorheriger Baseline vergleichen
./diff_sessions.py
# Oder Tabelle der letzten 5
./diff_sessions.py --last 5
```

`verdict.json` enthält dann:
```json
{
  "result": "PASS",
  "skip_stats": {"n": 10, "median_ms": 1840, "p95_ms": 2400, "max_ms": 2900}
}
```

---

## Live-Watch (sporadische Bugs aufspüren)

```bash
./build.sh --watch                    # 5 Min Beobachtung, kein Build/Flash
./build.sh --watch --duration 600     # 10 Min
./build.sh --watch --until "[CSPOT_READY]"  # bis Sentinel
```

---

## Bei FAIL: Diagnose-Reihenfolge (autonom)

1. `cat sessions/latest/verdict.json` — was war der Grund?
2. `tail -50 sessions/latest/serial.log` — was sagte die Hardware?
3. `cat sessions/latest/serial_decoded.log` — Backtrace via addr2line aufgelöst
4. `cat sessions/latest/commands.log` — was hat build.sh getan?
5. `ls sessions/latest/webcam.jpg` — Display-Snapshot (auto bei FAIL)

---

## Hypothesen-Loop (für Phase 5, autonom)

Voraussetzung: Baseline aus Phase 4 in `sessions/<baseline-id>/verdict.json`.

**Pro Hypothese:**

```bash
# 1. Hypothese aus HYPOTHESES.md wählen (H1 zuerst)
# 2. Referenz-Code aus HYPOTHESES.md lesen
# 3. Patch schreiben (1:1 mit minimaler Adaption)
# 4. Build + Skip-Test
./build.sh --skip-test
# 5. Diff zur Baseline
./diff_sessions.py <baseline-id>
# 6. Entscheiden:
#    - Median -10% oder besser  → git commit, weiter mit H2
#    - Keine signifikante Änderung → git checkout -- <files>, nächste H
#    - Verschlechterung           → git checkout -- <files>, in POSTMORTEMS.md eintragen
#    - Crash                       → git checkout -- <files>, Backtrace + POSTMORTEM
```

**Stop-Bedingungen:**
- 2 erfolglose Hypothesen in Folge → User eskalieren
- Klasse-B-Anti-Pattern berührt → User eskalieren (Compile-Assert wird sowieso feuern)
- Submodule-Bump nötig → User eskalieren

---

## Anti-Patterns (build feuert sofort)

- `MAX_TRACKS_PRELOAD>1` in `cspot/include/TrackQueue.h` → static_assert
- ESP-IDF-Version != v5.4.2 → Boot-Crash mit "Illegal Instruction"
- esptool < v4.11 → "Invalid head of packet"

Siehe vollständige Liste in `Claude.md §6` und `POSTMORTEMS.md`.

---

## Sentinels die im serial.log erscheinen

| Sentinel | Bedeutung | Quelle |
|---|---|---|
| `[SKIP_REQ direction=fwd\|back]` | Skip wurde ausgelöst | SpircHandler.cpp |
| `[SKIP_DONE direction=X skipped=N in Yms]` | Skip abgeschlossen | SpircHandler.cpp |
| `[TRACK_LOAD start url=...]` | CDN-Stream wird geöffnet | CDNAudioFile.cpp |
| `[TRACK_LOAD ready in Xms totalSize=Y]` | Track-Buffer initialisiert | CDNAudioFile.cpp |
| `[DMA_FREE N largest=M]` | DMA-Heap-Status (für H3) | CDNAudioFile.cpp |
| `[DNS_LOOKUP host=X took=Yms]` | DNS-Auflösung (H1) | PlainConnection.cpp |
| `[TCP_CONNECT host=X took=Yms]` | TCP-Connect (H2) | PlainConnection.cpp |
| `prefetch:` | Lade-Fortschritt | CDNAudioFile.cpp |
| `Backtrace:` | Crash-Adresses | ESP-IDF-Panic-Handler |

---

## Files-Übersicht

```
Claude.md          — Hauptregelwerk
POSTMORTEMS.md     — Gescheiterte Versuche dokumentiert
HYPOTHESES.md      — Skip-Fix-Ansätze mit Referenzen
WORKFLOW.md        — Diese Datei
build.sh           — Single-Source Build/Flash/Monitor
monitor_serial.py  — Serial-Reader
diff_sessions.py   — Cross-Session-Vergleich
sessions/          — Test-Verlauf, latest/ ist Symlink auf neueste
archive/           — alte Tools (fast_build.sh, hooks etc.)
```

---

## Phase-Übersicht

| Phase | Was | Status |
|---|---|---|
| 1 | Claude.md neu | ✅ |
| 2 | Tooling konsolidieren | ✅ |
| 3 | Session-Logging + CSpot-Sentinels | ✅ |
| 3.5 | DMA-Free Sentinel + Compile-Assert | ✅ |
| 4 | Skip-Baseline messen | 🟡 User an Hardware |
| 5 | Skip-Fix mit Hypothese (autonomer Loop) | ⏸ wartet auf Phase 4 |
| 6 | Interne Kamera für UI-Tests | später |
| 7 | USB-Audio + DAC | später |
