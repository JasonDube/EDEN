"""
EDEN Fauna / Ecosystem Generator
Adapted from spacegame430 planet_classifier_animal_integration.py

Generates a living ecosystem layer for a planet based on:
- Biome type (determines which creature categories are possible)
- Habitability score (0-100, determines density and diversity)
- Tech level (determines what can be domesticated or harvested)

Fauna produce resources that sit alongside mineral/geological resources:
- Food sources (huntable, farmable)
- Materials (hides, wool, chitin, bone)
- Biochemicals (medicinal, exotic compounds)
- Domesticable species (labor, mounts, companions)
"""

import random


# ── Fauna Categories ──
# Each entry: (name, description, min_habitability, resource_type, domesticable)

TERRESTRIAL_FAUNA = [
    ("Native Herbivores",
     "Grazing animals adapted to local vegetation. Potential food and leather source.",
     40, "food", True),
    ("Predator Species",
     "Apex predators maintaining ecosystem balance. Dangerous but valuable hides.",
     50, "material", False),
    ("Burrowing Mammals",
     "Subterranean creatures with dense fur. Potential wool/fiber source.",
     45, "material", True),
    ("Edible Insects",
     "High-protein invertebrates found in large colonies. Easily farmed.",
     30, "food", True),
    ("Pack Beasts",
     "Large sturdy animals capable of carrying heavy loads. Domesticable for labor.",
     55, "labor", True),
    ("Avian Species",
     "Flying creatures providing eggs, feathers, and pest control.",
     40, "food", True),
    ("Reptilian Grazers",
     "Scaled herbivores with tough hides. Slow-growing but resilient.",
     35, "material", True),
]

AQUATIC_FAUNA = [
    ("Fish Schools",
     "Dense populations of edible aquatic species. Primary protein source on ocean worlds.",
     35, "food", True),
    ("Shellfish Beds",
     "Filter-feeding invertebrates producing shells and pearls.",
     30, "material", True),
    ("Aquatic Megafauna",
     "Large marine creatures. Dangerous but valuable for oil and bone.",
     50, "material", False),
    ("Bioluminescent Organisms",
     "Light-producing marine life. Source of exotic biochemicals.",
     45, "biochemical", False),
    ("Coral Colonies",
     "Reef-building organisms creating habitat structure and construction material.",
     40, "material", False),
]

EXOTIC_FAUNA = [
    ("Spore Creatures",
     "Fungal-animal hybrids releasing nutrient-rich spores. Unique to damp biomes.",
     50, "biochemical", False),
    ("Crystal Symbiotes",
     "Organisms that incorporate mineral crystals into their biology.",
     55, "biochemical", False),
    ("Thermal Vent Life",
     "Extremophile organisms thriving near volcanic activity. Rich in exotic compounds.",
     25, "biochemical", False),
    ("Desert Adapted Fauna",
     "Hardy creatures surviving extreme heat with minimal water. Tough hides.",
     30, "material", True),
    ("Tundra Megafauna",
     "Cold-adapted large animals with thick insulating fur and fat reserves.",
     35, "material", True),
    ("Exotic Biochemical Producers",
     "Organisms synthesizing compounds with medicinal or industrial applications.",
     60, "biochemical", False),
]


# ── Biome → Fauna Pool Mapping ──

BIOME_FAUNA_POOLS = {
    "temperate_forest": {
        "base_habitability": 75,
        "pools": [
            (TERRESTRIAL_FAUNA, 1.0),   # full access to terrestrial
            (AQUATIC_FAUNA, 0.3),        # some streams/rivers
            (EXOTIC_FAUNA, 0.1),         # rare exotics
        ],
    },
    "arid_desert": {
        "base_habitability": 35,
        "pools": [
            (TERRESTRIAL_FAUNA, 0.4),
            (EXOTIC_FAUNA, 0.5),         # desert-adapted + biochemicals
        ],
    },
    "ocean_world": {
        "base_habitability": 65,
        "pools": [
            (AQUATIC_FAUNA, 1.0),        # full aquatic
            (TERRESTRIAL_FAUNA, 0.2),    # island species
            (EXOTIC_FAUNA, 0.3),
        ],
    },
    "volcanic": {
        "base_habitability": 20,
        "pools": [
            (EXOTIC_FAUNA, 0.8),         # thermal vent life dominates
            (TERRESTRIAL_FAUNA, 0.1),
        ],
    },
    "tundra": {
        "base_habitability": 30,
        "pools": [
            (TERRESTRIAL_FAUNA, 0.5),    # hardy species
            (AQUATIC_FAUNA, 0.2),        # under-ice aquatics
            (EXOTIC_FAUNA, 0.4),         # tundra megafauna
        ],
    },
    "savanna": {
        "base_habitability": 70,
        "pools": [
            (TERRESTRIAL_FAUNA, 1.0),    # rich terrestrial diversity
            (AQUATIC_FAUNA, 0.2),        # watering holes
            (EXOTIC_FAUNA, 0.2),
        ],
    },
    "mountainous": {
        "base_habitability": 45,
        "pools": [
            (TERRESTRIAL_FAUNA, 0.6),
            (EXOTIC_FAUNA, 0.3),
        ],
    },
    "swamp": {
        "base_habitability": 60,
        "pools": [
            (TERRESTRIAL_FAUNA, 0.7),
            (AQUATIC_FAUNA, 0.8),        # lots of water
            (EXOTIC_FAUNA, 0.5),         # spore creatures etc
        ],
    },
    "crystalline": {
        "base_habitability": 25,
        "pools": [
            (EXOTIC_FAUNA, 1.0),         # crystal symbiotes dominate
            (TERRESTRIAL_FAUNA, 0.1),
        ],
    },
}


# ── Generator ──

def generate_fauna(biome_key, habitability_override=None):
    """Generate fauna for a planet.

    Args:
        biome_key: One of the BIOME_FAUNA_POOLS keys
        habitability_override: If provided, use this instead of biome default

    Returns:
        dict with:
          - habitability: int (0-100)
          - species: list of fauna dicts
          - food_sources: list of names (food-type fauna)
          - materials: list of names (material-type fauna)
          - biochemicals: list of names (biochemical-type fauna)
          - domesticable: list of names (can be tamed/farmed)
          - ecosystem_richness: str ("barren", "sparse", "moderate", "rich", "thriving")
    """
    biome_data = BIOME_FAUNA_POOLS.get(biome_key)
    if not biome_data:
        return _empty_fauna(0)

    habitability = habitability_override if habitability_override is not None else biome_data["base_habitability"]
    # Add some randomness
    if habitability_override is None:
        habitability = max(0, min(100, habitability + random.randint(-15, 15)))

    if habitability < 10:
        return _empty_fauna(habitability)

    species = []
    for pool, weight in biome_data["pools"]:
        for creature in pool:
            name, desc, min_hab, res_type, domesticable = creature
            if habitability < min_hab:
                continue
            # Roll for presence based on weight and habitability
            chance = weight * (habitability / 100.0) * 0.8
            if random.random() < chance:
                # Abundance scales with habitability
                abundance = _roll_abundance(habitability, min_hab)
                species.append({
                    "name": name,
                    "description": desc,
                    "resource_type": res_type,
                    "domesticable": domesticable,
                    "abundance": abundance,
                })

    # Classify outputs
    food_sources = [s["name"] for s in species if s["resource_type"] == "food"]
    materials = [s["name"] for s in species if s["resource_type"] == "material"]
    biochemicals = [s["name"] for s in species if s["resource_type"] == "biochemical"]
    labor = [s["name"] for s in species if s["resource_type"] == "labor"]
    domesticable = [s["name"] for s in species if s["domesticable"]]

    # Ecosystem richness
    count = len(species)
    if count == 0:
        richness = "barren"
    elif count <= 2:
        richness = "sparse"
    elif count <= 5:
        richness = "moderate"
    elif count <= 8:
        richness = "rich"
    else:
        richness = "thriving"

    return {
        "habitability": habitability,
        "species": species,
        "species_count": count,
        "food_sources": food_sources,
        "materials": materials,
        "biochemicals": biochemicals,
        "labor": labor,
        "domesticable": domesticable,
        "ecosystem_richness": richness,
    }


def _roll_abundance(habitability, min_hab):
    """Roll an abundance level based on how far above minimum the habitability is."""
    margin = habitability - min_hab
    roll = random.random() * 100
    if margin > 40 and roll < 30:
        return "abundant"
    elif margin > 20 and roll < 50:
        return "common"
    elif margin > 5:
        return "uncommon"
    else:
        return "rare"


def _empty_fauna(habitability):
    return {
        "habitability": habitability,
        "species": [],
        "species_count": 0,
        "food_sources": [],
        "materials": [],
        "biochemicals": [],
        "labor": [],
        "domesticable": [],
        "ecosystem_richness": "barren",
    }


# ── CLI test ──

if __name__ == "__main__":
    import json
    for biome in BIOME_FAUNA_POOLS:
        f = generate_fauna(biome)
        print(f"\n=== {biome} (habitability {f['habitability']}, {f['ecosystem_richness']}) ===")
        for s in f["species"]:
            tag = " [domesticable]" if s["domesticable"] else ""
            print(f"  {s['name']} ({s['resource_type']}, {s['abundance']}){tag}")
