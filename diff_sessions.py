#!/Users/nicolasvonbruck/Desktop/M5_3/flash_env/bin/python3
"""
Cross-Session-Diff-Tool für M5_3.

Vergleicht zwei oder mehr Test-Sessions miteinander:
- Skip-Latenz (Median, P95, Max)
- Resultat (PASS/FAIL)
- Welcher Commit
- DMA-Free Verlauf aus serial.log

Usage:
    ./diff_sessions.py                      # latest vs vorletzte
    ./diff_sessions.py SESSION_A SESSION_B  # zwei explizite Sessions
    ./diff_sessions.py --baseline           # alle vs latest baseline (n=10)
    ./diff_sessions.py --last 5             # letzte 5 Sessions tabellarisch
"""

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).parent
SESSIONS = ROOT / "sessions"


def load_session(name):
    sd = SESSIONS / name
    if not sd.is_dir():
        return None
    out = {"name": name, "verdict": None, "meta": None, "dma_min": None,
           "skip_n": 0}
    vfile = sd / "verdict.json"
    if vfile.exists():
        try:
            out["verdict"] = json.loads(vfile.read_text())
        except json.JSONDecodeError:
            out["verdict"] = None
    mfile = sd / "meta.json"
    if mfile.exists():
        out["meta"] = json.loads(mfile.read_text())
    sfile = sd / "serial.log"
    if sfile.exists():
        text = sfile.read_text(errors="replace")
        # DMA_FREE Werte extrahieren (legacy + neu)
        dmas = [int(m.group(1)) for m in re.finditer(r"\[DMA_FREE (\d+)", text)]
        # Neue: HEAP_DMA total_free + largest
        for m in re.finditer(r"\[HEAP_DMA total_free=(\d+) largest=(\d+)", text):
            dmas.append(int(m.group(2)))  # nimm largest als kritisches Mass
        out["dma_min"] = min(dmas) if dmas else None
        out["dma_max"] = max(dmas) if dmas else None
        # Skip-Anzahl
        out["skip_n"] = len(re.findall(r"\[SKIP_DONE", text))
        # Audio quality metrics from serial.log
        glitch_gaps = [int(m.group(1)) for m in
                       re.finditer(r"\[AUDIO_GLITCH gap=(\d+)", text)]
        out["glitch_n"] = len(glitch_gaps)
        out["glitch_max_ms"] = max(glitch_gaps) if glitch_gaps else 0
        stall_waits = [int(m.group(1)) for m in
                       re.finditer(r"\[CDN_STALL_END waited=(\d+)", text)]
        out["stall_n"] = len(stall_waits)
        out["stall_max_ms"] = max(stall_waits) if stall_waits else 0
        # Empirical threshold calibration: max byte offset per track (TRACK_DTOR)
        max_needs = [int(m.group(1)) for m in
                     re.finditer(r"\[TRACK_DTOR [^]]*maxNeed=(\d+)KB", text)]
        out["max_need_kb"] = max(max_needs) if max_needs else None
        # SKIP_REQ → AUDIO_START latency (ESP timer in parentheses: "(XXXXX)")
        # Correlate by finding consecutive SKIP_REQ + AUDIO_START pairs.
        skip_req_times = [int(m.group(1)) for m in
                          re.finditer(r"\((\d+)\) [^\n]*\[SKIP_REQ", text)]
        audio_start_times = [int(m.group(1)) for m in
                             re.finditer(r"\((\d+)\) [^\n]*\[AUDIO_START\]", text)]
        if skip_req_times and audio_start_times:
            latencies = []
            ai = 0
            for st in skip_req_times:
                while ai < len(audio_start_times) and audio_start_times[ai] <= st:
                    ai += 1
                if ai < len(audio_start_times):
                    latencies.append(audio_start_times[ai] - st)
                    ai += 1
            if latencies:
                latencies.sort()
                out["audio_latency_ms"] = {
                    "n": len(latencies),
                    "median": latencies[len(latencies) // 2],
                    "max": latencies[-1]
                }
    return out


def list_sessions():
    if not SESSIONS.is_dir():
        return []
    return sorted([p.name for p in SESSIONS.iterdir()
                   if p.is_dir() and re.match(r"\d{4}-\d{2}-\d{2}_", p.name)],
                  reverse=True)


def fmt_skip(s):
    if not s.get("verdict"): return "(no verdict)"
    v = s["verdict"]
    tl = v.get("track_load_stats")
    h2 = v.get("h2_reuse_count", 0)
    h2_s = f" h2={h2}" if h2 else ""
    if tl:
        return (f"{v.get('result','?'):4s} CDN n={tl['n']:2d} "
                f"med={tl['median_ms']:5d}ms p95={tl['p95_ms']:5d}ms "
                f"max={tl['max_ms']:5d}ms{h2_s}")
    stats = v.get("skip_stats")
    if not stats:
        return f"{v.get('result','?'):4s} ({v.get('reason','?')}){h2_s}"
    return (f"{v.get('result','?'):4s} SPIRC n={stats['n']:2d} "
            f"med={stats['median_ms']:5d}ms p95={stats['p95_ms']:5d}ms "
            f"max={stats['max_ms']:5d}ms{h2_s}")


def show_diff(a, b):
    print(f"\n=== Diff: {a['name']}  →  {b['name']} ===")
    print(f"Commit: {a['meta']['commit'] if a['meta'] else '?'}  →  "
          f"{b['meta']['commit'] if b['meta'] else '?'}")
    print(f"Result: {fmt_skip(a)}")
    print(f"     →  {fmt_skip(b)}")
    if (a.get("verdict") and a["verdict"].get("skip_stats")
            and b.get("verdict") and b["verdict"].get("skip_stats")):
        sa, sb = a["verdict"]["skip_stats"], b["verdict"]["skip_stats"]
        for k in ("median_ms", "p95_ms", "max_ms"):
            d = sb[k] - sa[k]
            sign = "+" if d > 0 else ""
            pct = (100 * d / sa[k]) if sa[k] else 0
            arrow = "↑ schlechter" if d > 0 else ("↓ besser" if d < 0 else "= gleich")
            print(f"  {k:10s}: {sa[k]:5d} → {sb[k]:5d}  ({sign}{d:+5d}ms / {pct:+.1f}%) {arrow}")
    if a.get("dma_min") and b.get("dma_min"):
        print(f"DMA min : {a['dma_min']:7d} → {b['dma_min']:7d}  "
              f"({b['dma_min']-a['dma_min']:+d})")
    # Audio quality metrics
    for key, label in (("glitch_n", "Glitches"), ("stall_n", "CDN stalls")):
        va, vb = a.get(key), b.get(key)
        if va is not None or vb is not None:
            va = va or 0; vb = vb or 0
            arrow = "↓ besser" if vb < va else ("↑ schlechter" if vb > va else "= gleich")
            print(f"  {label:12s}: {va:4d} → {vb:4d}  {arrow}")
    # Network-Stats Diff (DNS/TCP/TLS)
    for stat_name in ("dns_stats", "tcp_stats", "tls_stats"):
        sa = (a.get("verdict") or {}).get(stat_name)
        sb = (b.get("verdict") or {}).get(stat_name)
        if not sa or not sb: continue
        for k in ("median_ms", "p95_ms", "max_ms"):
            d = sb[k] - sa[k]
            sign = "+" if d > 0 else ""
            print(f"  {stat_name:10s}.{k:9s}: {sa[k]:5d} → {sb[k]:5d}  ({sign}{d:+d}ms)")


def show_table(sessions):
    print(f"\n{'Session':32s}  {'Commit':10s}  {'Result':36s}  {'DMA':>7s}  {'Skip':>4s}  {'Glitch':>6s}  {'Stall':>5s}")
    print("-" * 110)
    for s in sessions:
        commit = s["meta"]["commit"] if s["meta"] else "?"
        dma = str(s.get("dma_min") or "-")
        glitch = str(s.get("glitch_n", "-"))
        stall = str(s.get("stall_n", "-"))
        print(f"{s['name']:32s}  {commit:10s}  {fmt_skip(s):36s}  {dma:>7s}  {s['skip_n']:>4d}  {glitch:>6s}  {stall:>5s}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sessions", nargs="*",
                    help="0=latest vs prev, 1=vs latest, 2=A vs B")
    ap.add_argument("--last", type=int, metavar="N",
                    help="Tabelle der letzten N Sessions")
    args = ap.parse_args()

    all_sessions = list_sessions()
    if not all_sessions:
        print("Keine Sessions in", SESSIONS)
        return 1

    if args.last:
        loaded = [load_session(n) for n in all_sessions[:args.last]]
        loaded = [s for s in loaded if s]
        show_table(loaded)
        return 0

    if len(args.sessions) == 0:
        if len(all_sessions) < 2:
            print("Brauche mind. 2 Sessions für Diff (nur 1 vorhanden)")
            return 1
        a = load_session(all_sessions[1])  # vorletzte
        b = load_session(all_sessions[0])  # neueste
    elif len(args.sessions) == 1:
        a = load_session(args.sessions[0])
        b = load_session(all_sessions[0])
    else:
        a = load_session(args.sessions[0])
        b = load_session(args.sessions[1])

    if not a or not b:
        print("Session nicht gefunden")
        return 1
    show_diff(a, b)
    return 0


if __name__ == "__main__":
    sys.exit(main())
