# Modular Character System — Design Document

## Core Concept
Standardized, interchangeable body components assembled into full characters. Parts snap together via hookup rings with a strict vertex contract at boundaries.

## Rules

### Within a Species Group
- All variants share identical topology (poly count, edge flow, UV islands)
- Variants differ only in vertex positions (shape)
- Sliders morph between variants via blend shapes
- Example: 5 female human torsos, all same mesh structure, different body shapes

### Across Species
- Topology, poly count, and UVs can be completely different
- Hookup rings at connection boundaries MUST match — same vertex count, same winding order, same local-space positioning
- Any head connects to any torso, any arm to any torso, regardless of species

## Component Types
| Component | Hookup Points |
|-----------|--------------|
| Head      | neck (bottom) |
| Torso     | neck (top), L/R shoulder, waist (bottom) |
| Arms      | shoulder (top), wrist (bottom) |
| Legs      | waist (top), ankle (bottom) |
| Hands     | wrist (top) |
| Feet      | ankle (top) |

## Hookup Ring Spec
- Each hookup is a ring of N vertices at the boundary edge
- Vertex count per hookup type: **TBD — will define from real examples**
- Vertices ordered consistently (e.g. clockwise when viewed from outside)
- Ring sits on a known local-space plane (e.g. neck ring centered at origin, Y-up)

## Species Groups (Planned)
- *To be populated as examples are built*

## Slider Parameters (Intra-Species)
- *To be defined per component type as variants are created*

## UV & Skeleton Strategy
- Combined mesh: each part keeps its own UV islands in shared 0-1 space
- **Multi-material (DECIDED)** — one material per component part, no atlas baking
- Textures per part: small, ~256x256 max, possibly lower (64x64 for small details)
- Different parts can use different resolutions as needed
- One master skeleton drives the entire combined mesh
- Hookup ring vertices at boundaries share identical bone weights — no seam tearing
- Each part does NOT carry its own skeleton — master rig is applied after assembly

## Open Questions
- Hookup ring vertex counts per connection type
- How to handle asymmetric parts (e.g. one arm different from another)
- LOD strategy — do lower LODs also follow the hookup contract?
- Atlas packing strategy — manual or auto-pack?

## Progress Log
- 2026-02-17: Initial design doc created. Jason building first example meshes.
