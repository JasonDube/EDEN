# FMV Adventure — prototype

A high-fantasy FMV/point-and-click adventure with a Sims-style relationship sim
underneath. You play a noble in a small castle; you navigate via scene images,
talk to characters, complete optional quests, and build romantic/diplomatic
relationships while uncovering your family's history and the world's.

## Core mechanic (the "actor + judge" loop)

Dialogue is driven by an LLM that does TWO jobs per exchange:
1. **Actor** — replies in-character (text, spoken via TTS in the full game).
2. **Judge** — emits a structured verdict: `mood`, `relationship_delta`, reason.

Video clips are **wordless emotional performances** indexed by emotion, NOT by
dialogue line — this is what keeps the FMV library finite and authorable.

- **Mood** (volatile, per-conversation) = *which* clip pool plays.
- **Relationship score/tier** (persistent) = *how deep* into each pool you can
  see. Higher tiers unlock "reward" performances across every mood.
- Relationship grows via genuine words, liked gifts, and interactions; flattery
  and disliked topics cost you. Locked story topics gate until trust is earned.

## Files

- `characters/lady_elara.json` — character def: persona, likes/dislikes, the
  mood→tier animation grid, tier-gated `secret_topics`.
- `harness.py` — terminal prototype of the loop (zero pip deps, stdlib only).
- `saves/<id>.json` — persistent per-character relationship state (auto-saved).

## Run

    python3 harness.py                       # talk to Lady Elara
    python3 harness.py characters/foo.json   # another character

In-chat: `/gift <item>`  `/state`  `/reset`  `/quit`

## Tech notes

- Talks to local **Ollama** (`OLLAMA_URL`, default `localhost:11434`,
  model `qwen3.5:9b`). Start it from the EDEN in-game Server Manager.
- **`think: false` is required** — qwen3.5 is a reasoning model; its hidden
  think block takes minutes. Disabled, turns run ~1-2s. `format: "json"` plus a
  one-shot retry handle occasional non-JSON replies.
- This prototype is rendering-agnostic; the Vulkan/ImGui video layer comes later.

## Status

✅ Actor+judge loop proven against local model. Flattery penalized, genuine
insight & liked gifts rewarded, tier-locked secrets correctly withheld,
clip selection by mood+tier working.

## Next

1. Navigation layer — scene/hotspot graph (rooms, doors, items).
2. Video playback de-risk — FFmpeg decode → Vulkan textured quad + synced audio.
