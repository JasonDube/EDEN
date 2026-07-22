---
name: QSC-9 Quantum Singularity Core
category: tech
tags: [drive, power-core, canon, aether-dynamics]
status: canon
manufacturer: Aether Dynamics
kind: system
criticality: critical
related: [the-ascendant, aether-dynamics, ada]
---

# QSC-9 — Quantum Singularity Core

The [Ascendant](../ships/the-ascendant.md)'s engine and power plant: a charged
micro black hole suspended in an electromagnetic containment trap. It is fed mass to
sustain output; its emission (Hawking/accretion radiation) is caught by a reflector
and directed as thrust, with a tap for ship power.

**The hard problem is heat.** In vacuum the core can only shed heat by radiation, and
it runs above its rated thermal envelope. Managing that — reading core temperature,
watching the coolant delta-T, scheduling burns and radiator deployment to stay in the
green without adding days to the voyage — is the drive's core gameplay loop, and a
recurring source of [Ada](../characters/ada.md)'s "current work" and worry.

> Canon spec source of truth: `../../assets/lore/qsc9.json` (parts, render
> locations, hotspot mapping). Keep them in sync.

## Components

| Part | Kind | Function |
|---|---|---|
| Singularity Core | visible | The charged micro black hole. Seen only as accretion glow + gravitational lensing. Converts fed mass to energy. |
| Electromagnetic Containment Trap ("the bottle") | visible | Holds the singularity suspended so it never touches matter. The single most critical safety system aboard. |
| Containment Vessel | visible | Armored chamber shell around the core and its field. |
| Containment Aperture | visible | Shielded port the instruments read the core through. |
| Radiation Reflector / Collector | conceptual | Parabolic mirror catching emission as thrust, tapping some for ship power. |
| Mass/Charge Feed Injector | conceptual | Feeds reaction mass and tops up the core's charge so the trap keeps its grip. |
| Coolant Loops & Pumps | visible | Carry heat off containment. The inlet/outlet delta-T is the number Ada watches most. |
| Heat Exchangers | conceptual | Hand heat from coolant to radiators. |
| Radiator Panels | external | Deploy to shed heat to space — exposing them to micrometeorites (a real tradeoff). |
| Spectro-Pyrometer | instrument | Reads core temp from the emission spectrum (Wien's law). The gauge the player first clicks without understanding. |
| RTD / Thermocouple Array | instrument | Direct-contact temperatures for everything touchable. |
| Drive Control Console | visible | Where margins are read and the burn / radiator schedule is set. |
| Core-Ejection System | conceptual | Jettisons the singularity if containment fails. A loose black hole means the ship is gone. |
| Backup Containment Power | conceptual | Redundant power to the bottle. Lose it and the core is free. |
| Thrust Nozzle | external | Where directed radiation becomes thrust. |

## Warranty

Containment integrity is covered by the [Aether Dynamics warranty](../lore/aether-dynamics.md)
— but combat damage, vacuum breaches, and unauthorized mods void it. A battle-caused
breach is on the Captain.
