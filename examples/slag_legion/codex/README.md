# The Slag Legion Codex

The single, centralized book of our universe — species, factions, worlds, ships,
trade goods, tech, terrain, ecologies, characters, lore, and design notes. One
place for pictures, designs, canon, code, diagrams, and ideas, kept structured so
that **someday the game can read it directly as an in-game encyclopedia.**

> This is a living reference. As we build Slag Legion, new things we invent get an
> entry here. Keep an eye to it — the codex and the game grow together.

## How it's organized

Every entry is one Markdown file with a **YAML frontmatter** header (structured,
machine-readable fields) followed by a **body** (lore, prose, images, diagrams,
code). Files are grouped into category folders:

| Folder | What lives here |
|---|---|
| `worlds/` | Planets & celestial bodies (akelba…) |
| `biomes/` | Ecologies / world-types and their traits |
| `governments/` | Government archetypes |
| `factions/` | Named polities & powers (the portrait catalogue) |
| `species/` | Peoples & lifeforms |
| `ships/` | Vessel classes (the Ascendant…) |
| `tech/` | Systems & devices (QSC-9 core…) |
| `tech-levels/` | The 0–10 tech-tier ladder |
| `trade-goods/` | Resources & commodities |
| `fauna/` | Wildlife |
| `terrain-features/` | Landmarks & set-pieces (mountains, ruins…) |
| `characters/` | Named individuals (Clara, Ada…) |
| `lore/` | Canon, doctrines, history |
| `design/` | Design notes, diagrams, roadmap, schema — the meta layer |

`_templates/` holds a blank starter for each category. `_tools/` holds the
generator that (re)builds reference entries from source docs.

## The frontmatter schema

See [`design/codex-schema.md`](design/codex-schema.md) for the full field list.
Every entry carries at least:

```yaml
---
name: Akelba
category: world          # matches the folder
tags: [planet, temperate, starter-world]
status: canon            # canon | draft | stub
related: [temperate-forest, the-ascendant]   # slugs of other entries
image: relative/path/to/hero.jpg             # optional
---
```

- **`status: canon`** — established, real in the game.
- **`status: draft`** — being worked out.
- **`status: stub`** — a placeholder (e.g. a faction we have art for but no lore
  yet). Fill these in over time; a viewer can grey them out.
- **`related`** — the cross-links that make this a *book* rather than a pile of
  files. List the slugs (filenames without `.md`) of connected entries.

## Where things come from

Some entries are **hand-authored canon**. Others are **generated** from existing
source material so nothing is stranded:

- `biomes/`, `governments/`, `tech-levels/`, `trade-goods/`, `fauna/` are extracted
  from `docs/EDEN_Reference.md` by [`_tools/build_from_reference.py`](_tools/build_from_reference.py).
  Re-run it after editing that guide.
- `factions/` are stubbed from the portrait set in `../assets/species/` by the same
  tool — image + name in place, lore to be written.

Regenerate with:

```sh
python3 codex/_tools/build_from_reference.py
```

Hand-authored entries are never overwritten by the tool (it only writes files that
carry a `generated: true` marker in their frontmatter).

## The runtime lore file is separate (on purpose)

`../assets/codex.md` is a *different thing* — a tight CANON file fed into Clara's
persona at runtime as established truth. The Codex here is the broad reference;
that file is the curated subset the companion is told to believe. When we promote
lore here to something Clara should know, mirror it there too. See
[`lore/aether-dynamics.md`](lore/aether-dynamics.md).
