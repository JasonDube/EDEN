#!/usr/bin/env python3
"""
Transcript report — turn the live play transcripts (written by the backend every time
you talk to a character) into the same mood table the profiler produces, PLUS example
statements per mood. Use it to keep studying her as you play-test.

The backend logs each exchange to  modules/ai_companion/backend/transcripts/<npc>.jsonl
with both the RAW mood word the model wrote and the RESOLVED clip emotion (after aliases).

Usage:
    python3 transcript_report.py <char_name | path-to.jsonl> [options]
      --master           write the numbered, human-readable MASTER LIST (prompt #, date,
                         full prompt, model's one-word mood, in-list/slip, full response)
                         to <char>_master.txt beside the log
      -n, --samples N    (table mode) show up to N example statements per mood (default 2)
      --player-only      ignore stage-direction exchanges (user line starting with '[')
      --raw              (table mode) tally the RAW model words instead of resolved emotions

    e.g.  python3 transcript_report.py clara --master        # the master list
          python3 transcript_report.py clara -n 3            # the frequency table
"""

import json
import os
import sys
from collections import Counter, defaultdict
from datetime import datetime

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
MODEL = os.getenv("OLLAMA_MODEL", "qwen3.5:9b")


def fmt_date(ts):
    try:
        d = datetime.fromisoformat(ts)
        return d.strftime("%B %-d, %Y  %H:%M")
    except Exception:
        return ts or "?"


def resolve_path(arg):
    if arg.endswith(".jsonl") and os.path.exists(arg):
        return arg
    safe = "".join(c if c.isalnum() or c in "_-" else "_" for c in arg).lower()
    return os.path.join(REPO, "modules/ai_companion/backend/transcripts", f"{safe}.jsonl")


def load_allowed(char):
    """The character's clip emotions (for hit/slip marking), if the spec is findable."""
    spec = os.path.join(REPO, "examples/slag_legion/assets/characters", char, "spec.json")
    if os.path.exists(spec):
        try:
            e = json.load(open(spec)).get("capabilities", {}).get("emotions", [])
            return set(["neutral"] + e)
        except Exception:
            pass
    return None


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    arg = sys.argv[1]
    samples = 2
    player_only = "--player-only" in sys.argv
    use_raw = "--raw" in sys.argv
    if "-n" in sys.argv:      samples = int(sys.argv[sys.argv.index("-n") + 1])
    if "--samples" in sys.argv: samples = int(sys.argv[sys.argv.index("--samples") + 1])

    path = resolve_path(arg)
    if not os.path.exists(path):
        print(f"No transcript at {path}\n(Talk to her in-game first — the backend writes it live.)")
        sys.exit(1)

    allowed = load_allowed(arg if not arg.endswith(".jsonl") else "")
    field = "raw_emotion" if use_raw else "emotion"

    # Load records (in order), optionally dropping stage-directions.
    records = []
    for line in open(path):
        line = line.strip()
        if not line:
            continue
        try:
            rec = json.loads(line)
        except Exception:
            continue
        if player_only and str(rec.get("user", "")).lstrip().startswith("["):
            continue
        records.append(rec)

    # --master: the numbered, human-readable master list (writes a .txt beside the log).
    if "--master" in sys.argv:
        out_path = os.path.splitext(path)[0] + "_master.txt"
        lines = []
        for i, rec in enumerate(records, 1):
            raw = rec.get("raw_emotion") or "(none)"
            resolved = rec.get("emotion") or "(none)"
            status = ("IN LIST" if allowed is None or raw in allowed else "SLIP")
            lines.append(f"PROMPT #{i:03d}   ·   {fmt_date(rec.get('ts',''))}")
            lines.append(f"PROMPT:   {rec.get('user','')}")
            lines.append(f"INTERACTION ({MODEL}):   {rec.get('interaction') or '-'}")
            tail = f"   →  resolved to [{resolved}]" if status == "SLIP" else ""
            lines.append(f"MOOD ({MODEL}, one word):   {raw}   [{status}]{tail}")
            lines.append(f"RESPONSE ({MODEL}):")
            lines.append(f"   {rec.get('reply','')}")
            lines.append("=" * 68)
        text = "\n".join(lines) + "\n"
        with open(out_path, "w") as f:
            f.write(text)
        print(text)
        print(f"[wrote master list — {len(records)} entries — to {out_path}]")
        return

    counts = Counter()
    examples = defaultdict(list)
    n = 0
    for rec in records:
        mood = rec.get(field) or "(none)"
        counts[mood] += 1
        n += 1
        if samples and len(examples[mood]) < samples and rec.get("reply"):
            examples[mood].append((rec.get("user", ""), rec["reply"]))

    print(f"\n{arg}  ·  {n} exchanges  ·  tallying {'RAW model words' if use_raw else 'resolved clip emotions'}")
    if player_only:
        print("(player messages only — stage-directions excluded)")
    print("=" * 52)
    for mood, c in sorted(counts.items(), key=lambda x: -x[1]):
        mark = ""
        if allowed is not None and mood not in ("(none)",):
            mark = "  ✅" if mood in allowed else "  ⚠ slip"
        print(f"  {c:5}  {100*c/max(n,1):4.0f}%  {mood}{mark}")
        for u, r in examples.get(mood, []):
            u1 = (u[:60] + "…") if len(u) > 60 else u
            r1 = (r[:80] + "…") if len(r) > 80 else r
            print(f"           you: {u1}")
            print(f"           her: {r1}")


if __name__ == "__main__":
    main()
