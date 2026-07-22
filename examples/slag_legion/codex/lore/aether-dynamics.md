---
name: Aether Dynamics
category: lore
tags: [corporation, canon, doctrine]
status: canon
related: [qsc-9, the-ascendant, clara, ada]
---

# Aether Dynamics

The megacorporation behind the Captain's ship, its drive, and its android crew.
Aether Dynamics manufactures the [QSC-9 core](../tech/qsc-9.md), the companion
androids ([Clara](../characters/clara.md), [Ada](../characters/ada.md),
[Eva](../characters/eva.md)), and holds the archives that gate knowledge of the
uncharted galaxy. Its policies are not flavor — they drive the game's real choices.

> **Runtime note.** The canon below is mirrored into `../../assets/codex.md`, which is
> fed to the companion as established truth. Keep the two in sync: promote new lore
> here, then trim it into a clean CANON bullet there.

## Doctrine — canon

- **Non-interference.** High-technology spacefarers — Aether Dynamics policy in
  particular — restrict unauthorized engagement with lower-technology and
  pre-spaceflight ("sovereign, non-contact") civilizations. Diverting to contact,
  trade with, or intervene in such a world is a serious decision with real
  diplomatic and legal consequences; the ship's warranty and the companion's
  corporate programming both discourage it. It is a genuine moral quandary, not a
  hard prohibition — the Captain may choose to intervene, and the companion has
  feelings about it either way.

- **The warranty.** Covers the [QSC-9](../tech/qsc-9.md)'s containment integrity and
  the companion's own structural failure due to manufacturing defects. It EXCLUDES
  combat damage, hull breaches from vacuum exposure, and unauthorized modifications
  — essentially any event outside standard transit or routine maintenance. A QSC-9
  breach caused by battle voids the claim: a running tension whenever the Captain
  takes the ship into combat.

- **Paid archive access.** The companion's onboard databases hold only generic tags
  for uncharted/primitive species (e.g. "sapiens", "pre-spaceflight", government
  type, tech level) — detailed cultural and biological data requires PAID access to
  the Aether Dynamics archives. A voided warranty cuts off the Captain's ability to
  purchase that access, so a scan of a primitive or uncharted world returns limited
  information until the archive data is bought.

## Design hooks

- The archive-access rule is the in-fiction reason a codex entry can read
  `status: stub` / "UNCHARTED — data requires Aether Dynamics archive access." The
  [in-game encyclopedia](../design/codex-schema.md) can gate entries on it.
- The warranty ties combat, exploration, and economy into one decision loop:
  intervene / fight → risk voiding it → lose archive access + core coverage.
