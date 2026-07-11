# Character authoring

Everything about one character lives in a single folder:

    assets/characters/<npcid>/

`<npcid>` is the NPC's display name in lowercase (e.g. Orlen -> `orlen/`).

## Files

| File | Who writes it | Purpose |
|------|---------------|---------|
| `def.json`     | **You** | The NPC definition: `name`, `model` (path to their `.glb`), `role` (e.g. `shopkeeper`), `heightFt`, `script`. Required for the character to spawn in a level. |
| `model.glb`    | **You** | The character's 3D model (the `def.json` `model` field points at it). |
| `greeting.txt` | **You** (optional) | The opening line the character says when a conversation starts. |
| `persona.txt`  | **You** | The character's system prompt / personality. Free text — write as much as you want, no escaping. This is the preprompt fed to the LLM. If missing, a generic shopkeeper/townsfolk default is used. |
| `memory.json`  | **Game** (you may seed it) | Machine-managed state: disposition, remembered facts, rolling transcript history, and accepted quest ids. Created automatically after the first conversation. You can hand-add entries to `memories`; the game preserves them. |
| `quests.json`  | **You** | Tasks this NPC can give (see below). |
| `profile.jpg`  | **You** (optional) | Portrait shown in the left column of the talk screen (`.png` also accepted). |
| `traits.json`  | **You** (optional) | Secondary/personality traits shown under the portrait (see below). |
| `videos.json`  | **You** (optional) | Per-clip playback options — rest frames and loop overrides (see below). |
| `<state>.mp4`  | **You** | Video for each emotion state (see below). |

## traits.json

Secondary/personality traits (temper, ambition, bravery, …), 0–100, shown as
bars under the portrait on the talk screen. An ordered array — they display in
the order you write them.

```json
{
  "traits": [
    { "name": "Temper",     "value": 55 },
    { "name": "Ambition",   "value": 35 },
    { "name": "Narcissism", "value": 25 }
  ]
}
```

These are display-only for now (they don't yet influence the NPC's behavior or
the LLM). Any trait names you like; add or remove freely.

## quests.json

Quests the player can ask about via the **Quests** button in the dialogue header.
The NPC never raises quests on their own — the player opens the panel and chooses.
Each quest has two actions:

- **Ask about this** — the NPC speaks the `pitch` verbatim (with the optional
  `emotion` video). Pure information; the player can ask as often as they like.
- **Accept Quest** — the player commits. One-time: it's added to the journal, the
  NPC's disposition rises **+5**, a marker drops on the overland map at the quest
  `target`, and an `accepted_note` folds into the NPC's awareness. The button then
  greys out to "Accepted" so the +5 can't be farmed.

```json
{
  "quests": [
    {
      "id": "missing_shipment",
      "title": "Investigate the Missing Shipment",
      "category": "Personal",
      "desc": "Journal description of the task.",
      "objective": "The one-line journal objective.",
      "emotion": "thoughtful",
      "accepted_note": "What the NPC now knows once the player has taken the quest.",
      "target": { "col": 12, "row": 12, "label": "Missing Shipment" },
      "pitch": "What the NPC says, in their own voice, when the player asks about it."
    }
  ]
}
```

- `id` — unique key; used to remember the quest was taken (persists in memory.json).
- `title` / `desc` / `objective` / `category` — how it appears in the journal.
- `pitch` — the NPC's spoken words when asked. Write it in their voice.
- `emotion` — optional; the state video played while they deliver the pitch.
- `accepted_note` — optional; once accepted, this line is injected into the NPC's
  persona so the undone quest stays in their mind. Falls back to a generic note.
- `target` — optional overland-map cell `{col, row}` (0–23 each; row 12 is the
  road between Ferrohold in the west and Greywatch in the east) and a `label`. A
  gold diamond appears there once the quest is accepted.

An NPC can list several quests; Orlen has just the one here as a test.

## memory.json shape

```json
{
  "disposition": 50,
  "memories": [
    "The traveler haggled hard over a healing potion and won.",
    "Distrusts anyone who mentions the road east too eagerly."
  ],
  "history": [
    { "player": true,  "text": "what do you sell?" },
    { "player": false, "text": "Ale, dried meats, healing potions..." }
  ]
}
```

- `disposition` — 0..100, 50 = neutral. The LLM nudges it via a `[rel:+N]`/`[rel:-N]`
  tag; shown in the dialogue header.
- `memories` — durable facts injected into every conversation. Seed your own here;
  the game will keep them and can add more later.
- `history` — the last ~40 lines of past conversation, injected (most recent 8) so
  the character has continuity across visits. Trimmed automatically.

## Emotion-state videos

The dialogue screen loops the clip matching the NPC's current emotion, falling back
to `default.mp4`, then to a plain text placeholder. Filenames (the current states + fallback):

    default.mp4
    neutral.mp4   happy.mp4     sad.mp4        angry.mp4
    surprised.mp4 curious.mp4   afraid.mp4     amused.mp4
    annoyed.mp4   flirty.mp4    thoughtful.mp4 excited.mp4
    requesting.mp4

The emotion set will grow as we experiment with the models; new states just need a
matching `<state>.mp4` (missing ones fall back to default, so nothing breaks).

Two kinds of emotion use the same video files:
- **Conversation** — the LLM tags each reply with one of the states above.
- **Quest pitch** — the `emotion` field in `quests.json` (authored, free-form).
