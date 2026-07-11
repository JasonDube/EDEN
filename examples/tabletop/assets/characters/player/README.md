# Player characters — authoring convention

Each playable character is **one folder** that holds its portrait and its 3D
models together. The portrait and the model are 1:1 — picking the portrait in
character creation picks that exact model.

## Where to put a new character

```
assets/characters/player/<race>/<gender>/<class>/<name>/
    portrait.jpg      (or .png/.jpeg/.webp)  — the face shown in char creation
    default.glb       — the resting/idle model  (required)
    attack_1.glb      — optional swing frame 1
    attack_2.glb      — optional swing frame 2
```

Example (already in the tree):

```
assets/characters/player/human/male/barbarian/example/
    default.glb  attack_1.glb  attack_2.glb
assets/characters/player/human/male/paladin/percy/
    default.glb
```

**Your rule as the artist:** make a folder under `<race>/<gender>/<class>/`, name
it for the character, and drop in `portrait.jpg` + `default.glb` (+ optional
attack frames). That's it — the character appears in the picker automatically, and
its model loads with it.

## What each path level means

- `<race>` / `<gender>` — filters which characters a player sees (a human male
  sees human-male characters). Also read by the game.
- `<class>` — **organizational only.** It is *not* a lock: a character you file
  under `fighter/` can still be chosen by a player creating a paladin. File by
  what the character *is*; any class can use them.
- `<name>` — the character's own folder. Each is unique and (once we wire it)
  consumed when chosen, so no two players share a face.

## How the game uses it

- **Char creation** scans these folders and shows one thumbnail per `portrait.*`.
- **Picking a portrait** stamps that folder's models onto the character (1:1).
- Before a portrait is picked (or if none exists yet), the game falls back to the
  first bundle matching the character's race/gender/class, then to Percy
  (`human/male/paladin/percy/`) as a last resort — so there's always a body.

## Notes

- The old `assets/portraits/` bank (79 orphan images with no models) has been set
  aside to `~/Desktop/image_and_model_dump/portraits_79_legacy/`. Going forward,
  portraits live with their models in the bundles above.
- `example/` and `percy/` are legacy model-only placeholders (no portrait yet);
  add a `portrait.jpg` to either and it'll show up in the picker.
