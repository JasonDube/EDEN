#!/usr/bin/env python3
"""
FMV Adventure — actor+judge conversation harness (terminal prototype).

Tests the core loop of the game with NO graphics:
  player text/gift  ->  LLM acts (in-character reply) AND judges (mood +
  relationship delta)  ->  we apply state, recompute tier, and report which
  animation clip would play (default for the mood) plus any tier-unlocked
  "reward" clips now available.

Clips are WORDLESS performances indexed by emotion; dialogue is the text reply
(which in the real game is also spoken via TTS). Mood = which clip pool is
active (volatile). Relationship score/tier = how deep into each pool you can
see (persistent).

Mirrors patterns from modules/ai_companion/backend/server.py:
  - Ollama /api/chat with stream:false
  - strip_think_tags() for qwen3.5 reasoning output
  - structured JSON verdict (like the NPC ACTION system)

Zero pip dependencies — stdlib urllib only.

Usage:
    python3 harness.py                       # talk to Lady Elara
    python3 harness.py characters/foo.json   # a different character

In-chat commands:
    /gift <item>   give a gift (judged against likes/dislikes)
    /state         show relationship + mood + unlocked clips
    /reset         wipe this character's save and start fresh
    /quit          exit
"""

import json
import os
import re
import sys
import urllib.request
import urllib.error

OLLAMA_URL = os.environ.get("OLLAMA_URL", "http://localhost:11434")
OLLAMA_MODEL = os.environ.get("OLLAMA_MODEL", "qwen3.5:9b")

HERE = os.path.dirname(os.path.abspath(__file__))
SAVE_DIR = os.path.join(HERE, "saves")

# Tier thresholds: relationship score -> highest unlocked clip tier.
TIER_THRESHOLDS = [(0, 0), (20, 1), (40, 2), (65, 3), (85, 4)]

# Per-exchange clamp so one message can't swing the whole relationship.
DELTA_CLAMP = 8


# ---------------------------------------------------------------------------
# Ollama (mirrors server.py call_ollama, but stdlib instead of httpx)
# ---------------------------------------------------------------------------
def call_ollama(messages, model=None):
    model = model or OLLAMA_MODEL
    payload = json.dumps({
        "model": model,
        "messages": messages,
        "stream": False,
        # nudge models that support it toward valid JSON
        "format": "json",
        # qwen3.5 is a reasoning model — its hidden <think> block is dead weight
        # for our actor+judge loop and can take minutes. Disable it: ~1-2s/turn.
        "think": False,
        "options": {"temperature": 0.8, "num_predict": 400},
    }).encode("utf-8")
    req = urllib.request.Request(
        f"{OLLAMA_URL}/api/chat",
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=240) as resp:
            result = json.loads(resp.read().decode("utf-8"))
    except urllib.error.URLError as e:
        raise SystemExit(f"\n[!] Could not reach Ollama at {OLLAMA_URL}: {e}\n"
                         f"    Start it from the in-game Server Manager, then retry.")
    return result.get("message", {}).get("content", "")


def strip_think_tags(text):
    """Remove <think>...</think> blocks from reasoning models (e.g. qwen3.5)."""
    return re.sub(r"<think>.*?</think>", "", text, flags=re.DOTALL).strip()


def extract_json(text):
    """Pull the first JSON object out of an LLM reply, tolerating fences/prose."""
    text = strip_think_tags(text)
    # ```json ... ``` fence
    m = re.search(r"```(?:json)?\s*(\{.*?\})\s*```", text, flags=re.DOTALL)
    if m:
        text = m.group(1)
    # else: first { ... last }
    elif "{" in text:
        text = text[text.index("{"): text.rindex("}") + 1]
    return json.loads(text)


# ---------------------------------------------------------------------------
# State
# ---------------------------------------------------------------------------
def tier_for_score(score):
    tier = 0
    for threshold, t in TIER_THRESHOLDS:
        if score >= threshold:
            tier = t
    return tier


def load_character(path):
    with open(path, "r") as f:
        return json.load(f)


def save_path(char):
    return os.path.join(SAVE_DIR, f"{char['id']}.json")


def load_state(char):
    os.makedirs(SAVE_DIR, exist_ok=True)
    p = save_path(char)
    if os.path.exists(p):
        with open(p, "r") as f:
            return json.load(f)
    return {
        "score": 0,
        "tier": 0,
        "interaction_count": 0,
        "gifts_given": [],
        "flags": [],
        "mood": "neutral",
        "history": [],  # [{role, content}] for the actor context
    }


def save_state(char, state):
    with open(save_path(char), "w") as f:
        json.dump(state, f, indent=2)


# ---------------------------------------------------------------------------
# Prompting
# ---------------------------------------------------------------------------
def build_system_prompt(char, state):
    moods = ", ".join(sorted(char["animations"].keys()))
    likes = ", ".join(char.get("likes", []))
    dislikes = ", ".join(char.get("dislikes", []))
    # Which secret topics are unlocked at the current tier?
    locked_note = ""
    for topic, desc in char.get("secret_topics", {}).items():
        req = re.search(r"tier (\d)", desc)
        req_tier = int(req.group(1)) if req else 0
        if state["tier"] < req_tier:
            locked_note += (f"\n- You will NOT discuss '{topic}' yet "
                            f"(needs trust tier {req_tier}; current {state['tier']}). "
                            f"Deflect if pushed.")

    return f"""You are role-playing as {char['name']}.

CHARACTER:
{char['persona']}

You LIKE: {likes}
You DISLIKE: {dislikes}

CURRENT RELATIONSHIP WITH THE PLAYER (a noble):
- Relationship score: {state['score']}/100 (trust tier {state['tier']})
- Total interactions so far: {state['interaction_count']}
- Your current mood entering this exchange: {state['mood']}
{locked_note}

YOUR JOB EACH TURN — do BOTH:
1. ACT: reply IN CHARACTER to the player's latest message. Keep it to 1-3
   sentences, in {char['name']}'s voice. This text is spoken aloud, so no
   stage directions or asterisks — dialogue only.
2. JUDGE: assess how the player's message affected things.

Choose mood from EXACTLY this list: {moods}

Score the relationship change honestly:
- Genuine honesty, clever insight, or a gift you LIKE -> positive.
- Flattery, boasting, prying about locked topics, or things you DISLIKE -> negative.
- Neutral smalltalk -> near zero.
- Range -{DELTA_CLAMP} to +{DELTA_CLAMP}. Be stingy; trust is earned slowly.

Respond with ONLY this JSON (no prose, no code fence):
{{
  "reply": "<your in-character spoken line>",
  "mood": "<one mood from the list above>",
  "intensity": <0.0-1.0>,
  "relationship_delta": <integer -{DELTA_CLAMP}..{DELTA_CLAMP}>,
  "delta_reason": "<short why>",
  "unlocked_topic": "<topic id or empty string>"
}}"""


def pick_clips(char, mood, tier):
    """Return (default_clip, reward_clips) for a mood at the current tier."""
    pool = char["animations"].get(mood, char["animations"].get("neutral", []))
    available = [c for c in pool if c["tier"] <= tier]
    if not available:
        available = [pool[0]] if pool else [{"clip": "(none)", "note": ""}]
    default = available[0]
    # "rewards" = anything beyond the tier-0 default that the player has unlocked
    rewards = [c for c in available[1:]]
    return default, rewards


# ---------------------------------------------------------------------------
# Display
# ---------------------------------------------------------------------------
C_DIM = "\033[2m"; C_BOLD = "\033[1m"; C_GRN = "\033[32m"; C_YEL = "\033[33m"
C_CYN = "\033[36m"; C_MAG = "\033[35m"; C_RST = "\033[0m"


def show_state(char, state):
    print(f"\n{C_BOLD}── {char['name']} ──{C_RST}")
    print(f"  score {C_GRN}{state['score']}/100{C_RST}  "
          f"tier {C_CYN}{state['tier']}{C_RST}  "
          f"mood {C_MAG}{state['mood']}{C_RST}  "
          f"interactions {state['interaction_count']}")
    if state["gifts_given"]:
        print(f"  gifts: {', '.join(state['gifts_given'])}")
    if state["flags"]:
        print(f"  flags: {', '.join(state['flags'])}")
    print(f"  {C_DIM}unlocked clips by mood:{C_RST}")
    for mood, pool in char["animations"].items():
        avail = [c["clip"] for c in pool if c["tier"] <= state["tier"]]
        locked = len(pool) - len(avail)
        line = f"    {mood:<12} {', '.join(avail)}"
        if locked:
            line += f"  {C_DIM}(+{locked} locked){C_RST}"
        print(line)
    print()


# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------
def turn(char, state, player_msg, is_gift=False):
    state["history"].append({"role": "user", "content": player_msg})
    messages = [{"role": "system", "content": build_system_prompt(char, state)}]
    messages += state["history"][-12:]  # recent context window

    verdict = None
    for attempt in range(2):  # occasional non-JSON reply -> one cheap retry (~1-2s)
        msgs = messages
        if attempt > 0:
            msgs = messages + [{"role": "user",
                                "content": "Your last reply was not valid JSON. "
                                "Respond with ONLY the JSON object, nothing else."}]
        raw = call_ollama(msgs)
        try:
            verdict = extract_json(raw)
            break
        except Exception:
            continue
    if verdict is None:
        print(f"{C_DIM}[parse fallback — model returned:]{C_RST} {strip_think_tags(raw)[:200]}")
        verdict = {"reply": strip_think_tags(raw)[:300], "mood": state["mood"],
                   "intensity": 0.5, "relationship_delta": 0,
                   "delta_reason": "unparsed", "unlocked_topic": ""}

    reply = verdict.get("reply", "...")
    mood = verdict.get("mood", state["mood"])
    if mood not in char["animations"]:
        mood = "neutral"
    delta = int(verdict.get("relationship_delta", 0))
    delta = max(-DELTA_CLAMP, min(DELTA_CLAMP, delta))
    if is_gift and delta == 0:
        delta = 1  # giving anything is a small gesture

    # apply state
    old_tier = state["tier"]
    state["score"] = max(0, min(100, state["score"] + delta))
    state["tier"] = tier_for_score(state["score"])
    state["mood"] = mood
    state["interaction_count"] += 1
    topic = verdict.get("unlocked_topic", "")
    if topic and topic not in state["flags"]:
        state["flags"].append(topic)
    state["history"].append({"role": "assistant", "content": reply})

    # report
    default, rewards = pick_clips(char, mood, state["tier"])
    sign = f"{C_GRN}+{delta}{C_RST}" if delta > 0 else (
        f"{C_YEL}{delta}{C_RST}" if delta < 0 else f"{C_DIM}0{C_RST}")

    print(f"\n{C_MAG}[mood: {mood} ({verdict.get('intensity', 0.5)})]{C_RST} "
          f"{C_DIM}▶ plays: {default['clip']} — {default.get('note','')}{C_RST}")
    print(f"{C_BOLD}{char['name']}:{C_RST} {reply}")
    print(f"  {C_DIM}relationship {sign} {C_DIM}({verdict.get('delta_reason','')}) "
          f"-> {state['score']}/100 tier {state['tier']}{C_RST}")

    if state["tier"] > old_tier:
        print(f"  {C_CYN}★ TRUST DEEPENED — reached tier {state['tier']}! "
              f"New performances unlocked across every mood.{C_RST}")
    if rewards:
        print(f"  {C_YEL}🎬 reward clips you can now watch in '{mood}':{C_RST}")
        for r in rewards:
            print(f"     - {r['clip']} ({r.get('note','')})")
    if topic:
        print(f"  {C_CYN}🔑 topic surfaced: {topic}{C_RST}")

    save_state(char, state)


def main():
    char_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        HERE, "characters", "lady_elara.json")
    char = load_character(char_path)
    state = load_state(char)

    print(f"{C_BOLD}FMV Adventure — talking to {char['name']}{C_RST} "
          f"{C_DIM}(model: {OLLAMA_MODEL}){C_RST}")
    print(f"{C_DIM}commands: /gift <item>  /state  /reset  /quit{C_RST}")
    show_state(char, state)

    while True:
        try:
            msg = input(f"{C_GRN}you ▸ {C_RST}").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            break
        if not msg:
            continue
        if msg in ("/quit", "/exit"):
            break
        if msg == "/state":
            show_state(char, state)
            continue
        if msg == "/reset":
            state = load_state.__wrapped__(char) if hasattr(load_state, "__wrapped__") else {
                "score": 0, "tier": 0, "interaction_count": 0, "gifts_given": [],
                "flags": [], "mood": "neutral", "history": []}
            save_state(char, state)
            print(f"{C_DIM}save wiped.{C_RST}")
            show_state(char, state)
            continue
        if msg.startswith("/gift"):
            item = msg[len("/gift"):].strip() or "a small token"
            state["gifts_given"].append(item)
            turn(char, state, f"(The player offers you a gift: {item}.)", is_gift=True)
            continue
        turn(char, state, msg)

    print(f"{C_DIM}saved. farewell.{C_RST}")


if __name__ == "__main__":
    main()
