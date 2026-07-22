---
name: Universe Roadmap
category: design
tags: [meta, roadmap, ideas]
status: draft
related: [akelba, codex-schema]
---

# Universe & systems roadmap

Running list of where the Slag Legion universe and its engine are headed. Design
notes and diagrams live here; promote anything that becomes real into a canon entry.

## Planets

- **Done:** akelba-class **wrappable flat planets** — seeded FBM, rolling terrain,
  wrap at edges. New Level → "World (2.5 mi wrappable planet)" with a seed knob.
  A celestial body = config + seed. See [akelba](../worlds/akelba.md).
- **Next horizon: spherified cube (quad-sphere).** For *true* round planets you can
  approach from orbit, see the horizon curve, and circumnavigate: build from a
  **cube — 6 identical SQUARE faces** — and bend each face's grid onto the sphere.
  - Why not a UV sphere: its faces are pole-pinched trapezoids/triangles. The cube
    gives 6 square heightmaps — exactly what our terrain chunk already is.
  - Our seeded FBM drives it unchanged (sample noise in 3D on the surface; seams
    match for free).
  - Engine work required: per-face terrain transform (displace along face normal,
    not world-Y); collision on rotated heightfields; **radial gravity** (points at
    planet center — the character controller assumes world-down today);
    cross-seam sculpting (the hard one).
  - Reference: Outer Wilds / Kerbal-style walkable planets.

## Codex → in-game encyclopedia

- The codex is authored to be engine-readable (structured frontmatter). An in-game
  reader can render an infobox from frontmatter + the body as the article.
- Gate `status: stub` / uncharted entries behind [Aether Dynamics archive
  access](../lore/aether-dynamics.md) — the paid-data rule is already canon.

## Open threads / ideas

- Trade loop across [trade-goods](../trade-goods/) and [governments](../governments/)
  (compatibility matrix exists in EDEN_Reference).
- Faction layer: which [species](../species/) ally/oppose; the reference has a
  government-compatibility matrix to seed diplomacy.
- Promote captured play canon (`../../assets/codex.md`) into structured entries here.
