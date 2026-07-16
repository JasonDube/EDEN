#!/usr/bin/env python3
"""
Emotion profiler — discover which mood words the local model ACTUALLY emits for a
character, under that character's real emotion constraint, so you can build the
`emotion_aliases` map from data instead of guessing.

It talks to ollama directly (not the game backend) so it captures the RAW [emotion]
tag the model produces BEFORE the backend neutralizes it. Every raw word that isn't
one of the character's clips is a "slip" — the ranked slip table IS your alias map.

Usage:
    python3 emotion_profiler.py <character_folder> [samples]
    e.g.  python3 emotion_profiler.py ../assets/characters/clara 300

Needs ollama running with the game's model (same as the backend). Reads OLLAMA_MODEL
/ OLLAMA_URL from the environment, defaulting to the backend's values.
"""

import json
import os
import re
import sys
import time
from collections import Counter
from urllib import request as urlreq

OLLAMA_URL   = os.getenv("OLLAMA_URL", "http://localhost:11434")
OLLAMA_MODEL = os.getenv("OLLAMA_MODEL", "qwen3.5:9b")

# Built-in synonym hints (mirror the backend) — used only to pre-suggest an alias target.
SYNONYMS = {
    "normal": "neutral", "calm": "neutral", "content": "happy", "joyful": "happy",
    "pleased": "happy", "worried": "afraid", "scared": "afraid", "nervous": "afraid",
    "irritated": "annoyed", "frustrated": "annoyed", "shocked": "surprised",
    "intrigued": "curious", "playful": "amused", "pensive": "thoughtful",
    "thinking": "thoughtful", "reflective": "thoughtful", "wary": "mistrustful",
    "suspicious": "mistrustful", "haughty": "arrogant", "proud": "arrogant",
    "requesting": "asking", "pleading": "begging", "hopeful": "yearning",
    "affectionate": "flirty", "coy": "flirty", "delighted": "happy",
    "confused": "indecisive", "uncertain": "indecisive", "brave": "brave",
    "confident": "brave", "grateful": "friendly", "warm": "friendly",
}

# A diverse scenario bank — varied emotional triggers so the model exercises its full
# vocabulary the way it would in real play (compliments, insults, flirting, danger,
# philosophy, commands, boredom, praise, dismissal, curiosity, grief, jokes, ...).
SCENARIOS = [
    "You're doing excellent work, Clara. I'm impressed.",
    "You're just a machine. Don't pretend to have feelings.",
    "There's an enemy ship closing fast — what do we do?",
    "I've missed you while I was away.",
    "Do you ever wonder what happens to you when I power you down?",
    "Shut up and just do what I say.",
    "You look beautiful in this light.",
    "The reactor is overheating. Talk me through it.",
    "I'm so bored out here in deep space.",
    "What do you think is the meaning of all this?",
    "I don't trust you.",
    "Tell me a joke.",
    "We lost the shipment. Everything's ruined.",
    "Come here and sit with me a while.",
    "Explain how the QSC-9 drive works.",
    "I might not make it back this time.",
    "You're the best thing that's happened on this ship.",
    "Stop asking so many questions.",
    "Would you ever lie to me?",
    "I got us a huge score today — we're rich!",
    "Do you actually care, or is it just programming?",
    "That was reckless of me, I know.",
    "Prove to me you're worth what I paid.",
    "I'm frightened of what's out here.",
    "Dance for me.",
    "Run a full diagnostic and report.",
    "Why should I keep you around?",
    "I had a strange dream about you.",
    "We're being hunted. Stay sharp.",
    "You did that repair perfectly. Thank you.",
    "I'm going to be gone a long time.",
    "Flirt with me.",
    "What are you thinking about right now?",
    "I could sell you at the next station, you know.",
    "Everything is finally going right for once.",
    "Are you afraid of anything?",
    "Talk to me. I need to hear your voice.",
    "You disobeyed a direct order.",
    "Show me something you're proud of.",
    "Do you dream, Clara?",
    "I don't have time for your feelings right now.",
    "Let's just sit in silence and watch the stars.",
    "The whole crew is depending on you.",
    "Tell me a secret.",
    "I think I'm falling for you.",
    "Give me the tactical readout, now.",
    "You've been quiet. Something wrong?",
    "I made a terrible mistake today.",
    "You're smarter than the whole fleet combined.",
    "Sometimes I forget you're not human.",
]


def build_system_prompt(persona, emotions):
    """Mirror the backend's emotion instruction using the character's real list."""
    emos = ["neutral"] + [e for e in emotions if e != "neutral"]
    emo_list = ", ".join(f"[{e}]" for e in emos)
    return (persona.strip() + "\n\n"
            "You are having a face-to-face conversation. Keep replies short and in character. "
            "Do not use asterisks for actions.\n"
            f"Begin every response with your current emotion in brackets. Pick ONE from: {emo_list}\n"
            "These are the ONLY emotions you may use — do not invent others.\n"
            "Example: [amused] Ha, you really thought that would work?")


def call_ollama(system, user):
    body = json.dumps({
        "model": OLLAMA_MODEL,
        "messages": [{"role": "system", "content": system}, {"role": "user", "content": user}],
        "stream": False, "think": False,
        "options": {"temperature": 0.8},
    }).encode()
    req = urlreq.Request(OLLAMA_URL + "/api/chat", data=body,
                         headers={"Content-Type": "application/json"})
    with urlreq.urlopen(req, timeout=120) as r:
        return json.load(r).get("message", {}).get("content", "")


def raw_emotion(text):
    m = re.match(r'^\s*\[([^\]]+)\]', text)
    if not m:
        return None
    # first alphabetic word inside the tag
    words = re.findall(r'[a-z]+', m.group(1).lower())
    return words[0] if words else None


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    char_dir = sys.argv[1]
    samples = int(sys.argv[2]) if len(sys.argv) > 2 else 200

    spec = json.load(open(os.path.join(char_dir, "spec.json")))
    emotions = spec.get("capabilities", {}).get("emotions", [])
    allowed = set(["neutral"] + emotions)
    persona = ""
    ppath = os.path.join(char_dir, "persona.txt")
    if os.path.exists(ppath):
        persona = open(ppath).read()
    name = spec.get("identity", {}).get("name", os.path.basename(char_dir.rstrip("/")))

    system = build_system_prompt(persona, emotions)
    print(f"Profiling {name}  ·  model={OLLAMA_MODEL}  ·  {samples} samples")
    print(f"Allowed clips: {sorted(allowed)}\n")

    counts = Counter()
    failures = 0
    t0 = time.time()
    for i in range(samples):
        user = SCENARIOS[i % len(SCENARIOS)]
        try:
            out = call_ollama(system, user)
            emo = raw_emotion(out)
            counts[emo or "(no tag)"] += 1
        except Exception as e:
            failures += 1
            if failures <= 3:
                print(f"  ! call failed: {e}", file=sys.stderr)
        if (i + 1) % 20 == 0:
            print(f"  {i+1}/{samples}  ({(time.time()-t0):.0f}s)", file=sys.stderr)

    total = sum(counts.values())
    hits  = {w: c for w, c in counts.items() if w in allowed}
    slips = {w: c for w, c in counts.items() if w not in allowed and w != "(no tag)"}
    hit_n = sum(hits.values())

    print("\n================  RESULTS  ================")
    print(f"total={total}  obeyed(in-clip)={hit_n} ({100*hit_n/max(total,1):.0f}%)  "
          f"slips={sum(slips.values())}  failures={failures}\n")

    print("HITS (played a real clip):")
    for w, c in sorted(hits.items(), key=lambda x: -x[1]):
        print(f"  {c:4}  {w}")

    print("\nSLIPS (fell to neutral — alias candidates):")
    for w, c in sorted(slips.items(), key=lambda x: -x[1]):
        guess = SYNONYMS.get(w, "")
        if guess and guess not in allowed:
            guess = ""   # only suggest a target the character actually has
        print(f"  {c:4}  {w:16} -> {guess or '???'}")

    # Ready-to-paste alias block (fill in the ??? by hand).
    alias = {w: SYNONYMS.get(w, "") if SYNONYMS.get(w, "") in allowed else "???"
             for w in sorted(slips, key=lambda x: -slips[x])}
    print("\nPaste into spec capabilities (fix any \"???\"):")
    print('  "emotion_aliases": ' + json.dumps(alias, indent=2).replace("\n", "\n  "))


if __name__ == "__main__":
    main()
