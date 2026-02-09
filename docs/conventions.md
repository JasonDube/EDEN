# EDEN Conventions

## Units

**1 unit = 1 meter**

All measurements in EDEN use real-world metric scale.

## Player Dimensions

| Property | Value | Real-world |
|----------|-------|------------|
| Eye height | 1.7m | ~5'7" eye level |
| Total height | ~1.85m | ~6'1" |
| Collision radius | 1.5m | Personal space |

## Terrain

| Property | Value |
|----------|-------|
| Chunk resolution | 64 vertices |
| Tile size | 2.0m |
| Chunk world size | 126m (63 tiles x 2m) |
| Default bounds | 32x32 chunks (-16 to 15) |
| Total terrain size | ~4km x 4km (16 sq km) |
| Height scale | 200m (max elevation) |

## Default Primitives

| Primitive | Dimensions |
|-----------|------------|
| Cube (model editor) | 1m × 1m × 1m |
| Cylinder | 4m height, 4m diameter (r=2m), 32 segments |
| Spawn marker | (uses cylinder) |

## Physics

| Property | Value |
|----------|-------|
| Gravity | 40 m/s² |
| Jump velocity | 15 m/s |
| Max slope angle | 80° |

## Action System

See [ENTITY_SYSTEM.md](../examples/terrain_editor/docs/ENTITY_SYSTEM.md) for the behavior/action instruction set.
