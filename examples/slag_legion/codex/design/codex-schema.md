---
name: Codex Schema
category: design
tags: [meta, schema, format]
status: canon
related: []
---

# Codex entry schema

The contract every entry follows so the game engine (and any viewer) can parse the
Codex. Frontmatter is YAML between `---` fences; the body is free Markdown.

## Universal fields (every entry)

| Field | Type | Notes |
|---|---|---|
| `name` | string | Display name. |
| `category` | enum | One of the folder names: `world`, `biome`, `government`, `faction`, `species`, `ship`, `tech`, `tech-level`, `trade-good`, `fauna`, `terrain-feature`, `character`, `lore`, `design`. |
| `tags` | list | Free keywords for search/filtering. |
| `status` | enum | `canon` \| `draft` \| `stub`. |
| `related` | list | Slugs (filename without `.md`) of connected entries — the cross-link graph. |
| `image` | string | Optional hero image, path relative to the entry file. |
| `generated` | bool | Present + `true` only on tool-built entries; hand-authored entries omit it and are never overwritten. |

## Category-specific fields

These are conventions, not enforced — add fields freely; the game reads what it
knows and ignores the rest. Keep names stable once the engine depends on them.

- **world**: `radius_km`, `biome`, `government`, `tech_level`, `habitability`, `wrappable`, `seed`
- **biome**: `temperature`, `vegetation`, `water_pct`, `habitability`, `dominant_resources`, `height_scale_m`
- **government**: `color_rgb`, `tendencies`, `building_style`
- **faction / species**: `government`, `homeworld`, `tech_level`, `disposition`
- **ship**: `class`, `manufacturer`, `drive`, `role`, `crew`
- **tech**: `manufacturer`, `kind` (`system`/`device`/`console`), `criticality`
- **tech-level**: `tier` (0–10), `power_source`, `buildings`, `resources`, `population`, `starting_credits`
- **trade-good**: `resource_class` (water/atmospheric/mineral/organic/geological/exotic), `rarity`, `uses`
- **fauna**: `habitat`, `role`, `biomes`
- **terrain-feature**: `world`, `asset` (path to GLB), `scale`
- **character**: `model`, `manufacturer`, `role`, `ship`

## Cross-linking

In the body, reference other entries with normal Markdown links using their slug,
e.g. `[the Ascendant](../ships/the-ascendant.md)`. Mirror the important ones in the
`related:` list so a graph/encyclopedia view can wire them without parsing prose.

## In-game encyclopedia (future)

Because fields are structured, an in-game codex reader can render an infobox from
frontmatter (name, image, key stats) and the body as the article. `status: stub`
entries can be hidden or shown as "UNCHARTED — data requires Aether Dynamics archive
access" (ties into existing canon: paid archive data for uncharted worlds). See
[`../lore/aether-dynamics.md`](../lore/aether-dynamics.md).
