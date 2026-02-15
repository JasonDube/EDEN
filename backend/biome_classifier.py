#!/usr/bin/env python3
"""
Biome Classifier Tool
Goes through all 130 species homeworld descriptions and lets you
assign each one to an existing biome or create a new one.

Saves progress after each answer so you can quit (Ctrl+C) and resume.

Usage:
    cd backend/
    python biome_classifier.py

Output: biome_mappings.json
"""

import json
import os
import sys
from species_data_manager import SpeciesDataManager

SAVE_FILE = "biome_mappings.json"

# Starting biomes from planet_generator.py
DEFAULT_BIOMES = [
    "temperate_forest",
    "arid_desert",
    "ocean_world",
    "volcanic",
    "tundra",
    "savanna",
    "mountainous",
    "swamp",
    "crystalline",
]


def load_progress():
    if os.path.exists(SAVE_FILE):
        with open(SAVE_FILE, "r") as f:
            return json.load(f)
    return {"mappings": {}, "biomes": list(DEFAULT_BIOMES)}


def save_progress(data):
    with open(SAVE_FILE, "w") as f:
        json.dump(data, f, indent=2)


def clear_screen():
    os.system("clear" if os.name != "nt" else "cls")


def print_biome_list(biomes):
    print("\n  Available biomes:")
    for i, b in enumerate(biomes, 1):
        marker = "  " if b in DEFAULT_BIOMES else " *"
        print(f"    {i:3d}. {b}{marker}")
    print(f"    {'n':>3s}. [NEW] — type a new biome name")
    print(f"    {'s':>3s}. [SKIP] — come back to this one later")
    print()


def main():
    data = load_progress()
    mappings = data["mappings"]
    biomes = data["biomes"]

    sdm = SpeciesDataManager.get_instance()
    all_ids = sdm.get_species_list()

    # Figure out what's left
    remaining = [cid for cid in all_ids if cid not in mappings]
    total = len(all_ids)
    done = total - len(remaining)

    clear_screen()
    print("=" * 60)
    print("  EDEN Biome Classifier")
    print("=" * 60)
    print(f"\n  {done}/{total} classified  |  {len(remaining)} remaining")
    print(f"  {len(biomes)} biomes ({len(biomes) - len(DEFAULT_BIOMES)} new)")
    if done > 0:
        print(f"\n  Resuming from where you left off.")
    print(f"\n  Ctrl+C to quit (progress auto-saved)")
    print("=" * 60)

    try:
        for i, cid in enumerate(remaining):
            species = sdm.get_species_by_identifier(cid)
            name = species.get("name", "Unknown")
            homeworld = species.get("homeworld_type", "No description")
            building_style = species.get("building_style", "")
            gov, tl = sdm.parse_identifier(cid)

            current = done + i + 1

            print(f"\n{'─' * 60}")
            print(f"  [{current}/{total}]  {name}")
            print(f"  ID: {cid}  |  Gov: {gov}  |  Tech Level: {tl}")
            print(f"{'─' * 60}")
            print(f"\n  Homeworld:  {homeworld}")
            if building_style:
                print(f"  Buildings:  {building_style}")

            print_biome_list(biomes)

            while True:
                choice = input("  Choice: ").strip().lower()

                if choice == "s":
                    print("  → Skipped")
                    break
                elif choice == "n":
                    new_name = input("  New biome name (snake_case): ").strip().lower()
                    new_name = new_name.replace(" ", "_")
                    if new_name and new_name not in biomes:
                        biomes.append(new_name)
                        print(f"  → Created biome: {new_name}")
                    elif new_name in biomes:
                        print(f"  → Biome '{new_name}' already exists, using it")
                    else:
                        print("  → Invalid name, try again")
                        continue
                    mappings[cid] = new_name
                    save_progress({"mappings": mappings, "biomes": biomes})
                    print(f"  ✓ {name} → {new_name}")
                    break
                else:
                    try:
                        idx = int(choice) - 1
                        if 0 <= idx < len(biomes):
                            mappings[cid] = biomes[idx]
                            save_progress({"mappings": mappings, "biomes": biomes})
                            print(f"  ✓ {name} → {biomes[idx]}")
                            break
                        else:
                            print("  → Number out of range, try again")
                    except ValueError:
                        print("  → Enter a number, 'n' for new, or 's' to skip")

    except KeyboardInterrupt:
        print("\n\n  Progress saved! Run again to resume.\n")
        save_progress({"mappings": mappings, "biomes": biomes})
        sys.exit(0)

    # Final summary
    save_progress({"mappings": mappings, "biomes": biomes})
    skipped = [cid for cid in all_ids if cid not in mappings]

    clear_screen()
    print("=" * 60)
    print("  CLASSIFICATION COMPLETE!")
    print("=" * 60)

    # Count per biome
    counts = {}
    for b in mappings.values():
        counts[b] = counts.get(b, 0) + 1

    print(f"\n  Biome Distribution:")
    for b in sorted(counts, key=counts.get, reverse=True):
        bar = "█" * counts[b]
        new = " *" if b not in DEFAULT_BIOMES else ""
        print(f"    {b:25s} {counts[b]:3d}  {bar}{new}")

    if skipped:
        print(f"\n  {len(skipped)} skipped — run again to classify them")

    print(f"\n  Results saved to: {SAVE_FILE}")
    print()


if __name__ == "__main__":
    main()
