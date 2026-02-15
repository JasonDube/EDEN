"""
EDEN Random Planet Generator

Generates a planet profile including:
- Biome / climate
- Native species (from species_data_manager)
- Government type and tech tier
- Available resources
- Terrain parameters (maps to terrain editor settings)
- Settlement seed info for the AI Architect
"""

import random
from species_data_manager import SpeciesDataManager
from resource_data import RESOURCES, get_resources_for_planet, get_resource
from fauna_generator import generate_fauna


# ── Biome Definitions ──
# Each biome influences terrain generation, available resources, and building constraints

BIOMES = {
    "temperate_forest": {
        "name": "Temperate Forest",
        "description": "Dense forests with moderate climate, abundant wood and wildlife.",
        "terrain": {
            "heightScale": 120.0,
            "noiseScale": 0.004,
            "noiseOctaves": 5,
            "noisePersistence": 0.45,
        },
        "dominant_resources": ["wood"],
        "possible_resources": ["wood", "limestone", "iron"],
        "water_coverage": 0.15,
        "temperature": "moderate",
        "vegetation": "heavy",
    },
    "arid_desert": {
        "name": "Arid Desert",
        "description": "Vast sandy expanses with extreme heat, mineral-rich underground deposits.",
        "terrain": {
            "heightScale": 80.0,
            "noiseScale": 0.002,
            "noiseOctaves": 3,
            "noisePersistence": 0.35,
        },
        "dominant_resources": ["iron", "oil"],
        "possible_resources": ["iron", "oil", "limestone"],
        "water_coverage": 0.02,
        "temperature": "hot",
        "vegetation": "sparse",
    },
    "ocean_world": {
        "name": "Ocean World",
        "description": "Mostly water with scattered island chains and archipelagos.",
        "terrain": {
            "heightScale": 60.0,
            "noiseScale": 0.003,
            "noiseOctaves": 4,
            "noisePersistence": 0.50,
        },
        "dominant_resources": ["limestone"],
        "possible_resources": ["wood", "limestone"],
        "water_coverage": 0.70,
        "temperature": "moderate",
        "vegetation": "moderate",
    },
    "volcanic": {
        "name": "Volcanic",
        "description": "Geologically active world with lava fields, hot springs, and rich mineral deposits.",
        "terrain": {
            "heightScale": 250.0,
            "noiseScale": 0.005,
            "noiseOctaves": 6,
            "noisePersistence": 0.55,
        },
        "dominant_resources": ["iron"],
        "possible_resources": ["iron", "limestone", "oil"],
        "water_coverage": 0.05,
        "temperature": "extreme",
        "vegetation": "minimal",
    },
    "tundra": {
        "name": "Frozen Tundra",
        "description": "Ice-covered world with harsh winds, limited growing seasons, and buried resources.",
        "terrain": {
            "heightScale": 100.0,
            "noiseScale": 0.003,
            "noiseOctaves": 4,
            "noisePersistence": 0.40,
        },
        "dominant_resources": ["oil"],
        "possible_resources": ["iron", "oil"],
        "water_coverage": 0.10,
        "temperature": "frozen",
        "vegetation": "minimal",
    },
    "savanna": {
        "name": "Savanna",
        "description": "Open grasslands with scattered trees, seasonal rains, and diverse fauna.",
        "terrain": {
            "heightScale": 50.0,
            "noiseScale": 0.002,
            "noiseOctaves": 3,
            "noisePersistence": 0.30,
        },
        "dominant_resources": ["wood"],
        "possible_resources": ["wood", "limestone"],
        "water_coverage": 0.08,
        "temperature": "warm",
        "vegetation": "moderate",
    },
    "mountainous": {
        "name": "Mountainous",
        "description": "Towering peaks and deep valleys with rich mineral veins and sparse vegetation.",
        "terrain": {
            "heightScale": 300.0,
            "noiseScale": 0.006,
            "noiseOctaves": 6,
            "noisePersistence": 0.50,
        },
        "dominant_resources": ["iron", "limestone"],
        "possible_resources": ["iron", "limestone", "wood"],
        "water_coverage": 0.05,
        "temperature": "cold",
        "vegetation": "sparse",
    },
    "swamp": {
        "name": "Swamplands",
        "description": "Waterlogged terrain with dense vegetation, rich soil, and oil deposits.",
        "terrain": {
            "heightScale": 30.0,
            "noiseScale": 0.004,
            "noiseOctaves": 5,
            "noisePersistence": 0.60,
        },
        "dominant_resources": ["wood", "oil"],
        "possible_resources": ["wood", "oil"],
        "water_coverage": 0.40,
        "temperature": "warm",
        "vegetation": "heavy",
    },
    "crystalline": {
        "name": "Crystalline",
        "description": "Alien landscape of crystal formations and mineral-rich terrain. Low gravity feel.",
        "terrain": {
            "heightScale": 180.0,
            "noiseScale": 0.007,
            "noiseOctaves": 4,
            "noisePersistence": 0.35,
        },
        "dominant_resources": ["limestone", "iron"],
        "possible_resources": ["limestone", "iron"],
        "water_coverage": 0.03,
        "temperature": "moderate",
        "vegetation": "alien",
    },
    # ── New biomes from species classification ──
    "fertile_floodplains": {
        "name": "Fertile Floodplains",
        "description": "Rich alluvial plains along river deltas with deep soil and seasonal flooding.",
        "terrain": {
            "heightScale": 30.0,
            "noiseScale": 0.002,
            "noiseOctaves": 3,
            "noisePersistence": 0.35,
        },
        "dominant_resources": ["wood"],
        "possible_resources": ["wood", "limestone"],
        "water_coverage": 0.25,
        "temperature": "warm",
        "vegetation": "heavy",
    },
    "ocean_world_rocky_coast": {
        "name": "Ocean World (Rocky Coast)",
        "description": "Ocean world dominated by dramatic rocky coastlines, sea cliffs, and tidal pools.",
        "terrain": {
            "heightScale": 90.0,
            "noiseScale": 0.004,
            "noiseOctaves": 5,
            "noisePersistence": 0.50,
        },
        "dominant_resources": ["limestone"],
        "possible_resources": ["limestone", "iron"],
        "water_coverage": 0.60,
        "temperature": "moderate",
        "vegetation": "sparse",
    },
    "temperate_river": {
        "name": "Temperate River Valley",
        "description": "Gentle river valleys with fertile banks, deciduous forests, and mild seasons.",
        "terrain": {
            "heightScale": 60.0,
            "noiseScale": 0.003,
            "noiseOctaves": 4,
            "noisePersistence": 0.40,
        },
        "dominant_resources": ["wood"],
        "possible_resources": ["wood", "limestone", "iron"],
        "water_coverage": 0.20,
        "temperature": "moderate",
        "vegetation": "heavy",
    },
    "coastal_urban": {
        "name": "Coastal Urban",
        "description": "Densely developed coastal zones with harbors, infrastructure, and mixed terrain.",
        "terrain": {
            "heightScale": 40.0,
            "noiseScale": 0.003,
            "noiseOctaves": 3,
            "noisePersistence": 0.30,
        },
        "dominant_resources": ["iron", "limestone"],
        "possible_resources": ["iron", "limestone", "oil"],
        "water_coverage": 0.35,
        "temperature": "moderate",
        "vegetation": "sparse",
    },
    "mountainous_extreme": {
        "name": "Extreme Mountains",
        "description": "Jagged ultra-high peaks with thin atmosphere, perpetual snow, and extreme elevation changes.",
        "terrain": {
            "heightScale": 400.0,
            "noiseScale": 0.008,
            "noiseOctaves": 7,
            "noisePersistence": 0.55,
        },
        "dominant_resources": ["iron"],
        "possible_resources": ["iron", "limestone"],
        "water_coverage": 0.02,
        "temperature": "frozen",
        "vegetation": "minimal",
    },
    "gas_giant": {
        "name": "Gas Giant",
        "description": "Massive gaseous world with floating platforms amid swirling cloud layers. No solid surface.",
        "terrain": {
            "heightScale": 200.0,
            "noiseScale": 0.010,
            "noiseOctaves": 4,
            "noisePersistence": 0.60,
        },
        "dominant_resources": ["oil"],
        "possible_resources": ["oil"],
        "water_coverage": 0.0,
        "temperature": "extreme",
        "vegetation": "none",
        "liquid_type": "gas",
    },
    "void": {
        "name": "Void",
        "description": "Deep space habitat or interdimensional pocket — no natural terrain, fully artificial.",
        "terrain": {
            "heightScale": 10.0,
            "noiseScale": 0.001,
            "noiseOctaves": 2,
            "noisePersistence": 0.20,
        },
        "dominant_resources": [],
        "possible_resources": ["iron"],
        "water_coverage": 0.0,
        "temperature": "frozen",
        "vegetation": "none",
    },
    "ocean_world_archipelago": {
        "name": "Ocean Archipelago",
        "description": "Vast oceans dotted with chains of volcanic islands and coral atolls.",
        "terrain": {
            "heightScale": 70.0,
            "noiseScale": 0.005,
            "noiseOctaves": 5,
            "noisePersistence": 0.50,
        },
        "dominant_resources": ["limestone", "wood"],
        "possible_resources": ["limestone", "wood"],
        "water_coverage": 0.65,
        "temperature": "warm",
        "vegetation": "moderate",
    },
    "rocky": {
        "name": "Rocky Badlands",
        "description": "Barren rocky terrain with mesas, canyons, and exposed mineral strata.",
        "terrain": {
            "heightScale": 150.0,
            "noiseScale": 0.005,
            "noiseOctaves": 5,
            "noisePersistence": 0.40,
        },
        "dominant_resources": ["iron", "limestone"],
        "possible_resources": ["iron", "limestone"],
        "water_coverage": 0.03,
        "temperature": "hot",
        "vegetation": "minimal",
    },
    "arid_river_valley": {
        "name": "Arid River Valley",
        "description": "Desert canyon carved by an ancient river, with oasis settlements along the banks.",
        "terrain": {
            "heightScale": 100.0,
            "noiseScale": 0.004,
            "noiseOctaves": 4,
            "noisePersistence": 0.38,
        },
        "dominant_resources": ["iron"],
        "possible_resources": ["iron", "oil", "limestone"],
        "water_coverage": 0.08,
        "temperature": "hot",
        "vegetation": "sparse",
    },
    "arid_desert_underground_cities": {
        "name": "Underground Desert Cities",
        "description": "Scorched surface with vast underground city networks carved into bedrock.",
        "terrain": {
            "heightScale": 70.0,
            "noiseScale": 0.002,
            "noiseOctaves": 3,
            "noisePersistence": 0.30,
        },
        "dominant_resources": ["iron", "oil"],
        "possible_resources": ["iron", "oil", "limestone"],
        "water_coverage": 0.01,
        "temperature": "extreme",
        "vegetation": "none",
    },
    "temperate_forest_large_trees": {
        "name": "Ancient Forest",
        "description": "Primeval forest of colossal trees with canopy ecosystems and moss-covered floors.",
        "terrain": {
            "heightScale": 130.0,
            "noiseScale": 0.004,
            "noiseOctaves": 5,
            "noisePersistence": 0.48,
        },
        "dominant_resources": ["wood"],
        "possible_resources": ["wood", "limestone"],
        "water_coverage": 0.12,
        "temperature": "moderate",
        "vegetation": "heavy",
    },
    "mech_planet": {
        "name": "Mech Planet",
        "description": "Entirely mechanized world — metal plating, gears, and automated factories cover the surface.",
        "terrain": {
            "heightScale": 50.0,
            "noiseScale": 0.006,
            "noiseOctaves": 3,
            "noisePersistence": 0.25,
        },
        "dominant_resources": ["iron"],
        "possible_resources": ["iron", "oil"],
        "water_coverage": 0.0,
        "temperature": "moderate",
        "vegetation": "none",
    },
    "harsh": {
        "name": "Harsh Wasteland",
        "description": "Unforgiving toxic environment with acid rain, radiation storms, and scarce resources.",
        "terrain": {
            "heightScale": 120.0,
            "noiseScale": 0.005,
            "noiseOctaves": 5,
            "noisePersistence": 0.45,
        },
        "dominant_resources": ["oil"],
        "possible_resources": ["iron", "oil"],
        "water_coverage": 0.05,
        "temperature": "extreme",
        "vegetation": "minimal",
        "liquid_type": "toxic",
    },
    "asteroid": {
        "name": "Asteroid",
        "description": "Small rocky body in space with micro-gravity, surface mining operations, and cramped habitats.",
        "terrain": {
            "heightScale": 40.0,
            "noiseScale": 0.008,
            "noiseOctaves": 3,
            "noisePersistence": 0.30,
        },
        "dominant_resources": ["iron"],
        "possible_resources": ["iron", "limestone"],
        "water_coverage": 0.0,
        "temperature": "frozen",
        "vegetation": "none",
    },
    "space_station": {
        "name": "Space Station",
        "description": "Orbital habitat with artificial gravity, recycled atmosphere, and modular construction.",
        "terrain": {
            "heightScale": 20.0,
            "noiseScale": 0.003,
            "noiseOctaves": 2,
            "noisePersistence": 0.20,
        },
        "dominant_resources": ["iron"],
        "possible_resources": ["iron"],
        "water_coverage": 0.0,
        "temperature": "moderate",
        "vegetation": "none",
    },
}

# ── Planet Name Generator ──

_NAME_PREFIXES = [
    "Kel", "Vor", "Thal", "Nex", "Zar", "Ori", "Ven", "Kra", "Sol", "Axi",
    "Eld", "Myr", "Cae", "Dra", "Pha", "Lum", "Gal", "Ter", "Nov", "Arc",
    "Xen", "Pyr", "Cor", "Val", "Syl", "Ash", "Bel", "Cyr", "Dyn", "Eth",
]

_NAME_SUFFIXES = [
    "ara", "ion", "ius", "oth", "enn", "ari", "ux", "on", "is", "ax",
    "heim", "grad", "mir", "thon", "dar", "kel", "van", "nor", "tal", "rin",
    "wen", "dor", "lis", "mar", "sel", "tis", "vex", "zen", "gor", "pax",
]

_NAME_NUMERALS = ["", "", "", " II", " III", " IV", " V", " Prime", " Major", " Minor"]


def _generate_planet_name():
    name = random.choice(_NAME_PREFIXES) + random.choice(_NAME_SUFFIXES)
    name += random.choice(_NAME_NUMERALS)
    return name.strip()


# ── Resource Distribution ──

def _generate_resource_zones(biome_key, biome_data, tech_level, grid_size=126):
    """Generate resource zone placement hints based on biome and tech level.

    Uses the full 35-resource database filtered by biome affinity and tech tier.
    Returns a list of resource deposit descriptions (not full grid data —
    the C++ ZoneSystem handles the actual grid).
    """
    deposits = []

    # Get resources from the full database
    present, harvestable = get_resources_for_planet(biome_key, tech_level)

    # Place deposits for harvestable resources (these can actually be extracted)
    for res_name in harvestable:
        res_data = get_resource(res_name)
        if not res_data:
            continue
        # Higher-tier resources are rarer
        tier = res_data["tier"]
        if tier <= 2:
            count = random.randint(1, 3)
            density_range = (0.5, 1.0)
            radius_range = (3, 8)
        elif tier <= 3:
            count = random.randint(1, 2)
            density_range = (0.3, 0.7)
            radius_range = (2, 5)
        else:
            count = 1
            density_range = (0.1, 0.5)
            radius_range = (1, 3)

        for _ in range(count):
            deposits.append({
                "resource": res_name,
                "category": res_data["category"],
                "tier": tier,
                "density": round(random.uniform(*density_range), 2),
                "radius": random.randint(*radius_range),
                "offset_x": random.randint(-50, 50),
                "offset_z": random.randint(-50, 50),
                "extraction": res_data["extraction"],
                "storage": res_data["storage"],
                "harvestable": True,
            })

    # Also note present-but-not-harvestable resources (visible but locked)
    locked = [r for r in present if r not in harvestable]
    for res_name in locked:
        res_data = get_resource(res_name)
        if not res_data:
            continue
        deposits.append({
            "resource": res_name,
            "category": res_data["category"],
            "tier": res_data["tier"],
            "density": round(random.uniform(0.2, 0.6), 2),
            "radius": random.randint(2, 5),
            "offset_x": random.randint(-50, 50),
            "offset_z": random.randint(-50, 50),
            "extraction": res_data["extraction"],
            "storage": res_data["storage"],
            "harvestable": False,
            "locked_reason": f"Requires tech level {res_data['tier'] if res_data['tier'] <= 4 else 6}+",
        })

    return deposits, present, harvestable


# ── Main Generator ──

def generate_planet(seed=None, biome=None, government=None, tech_level=None):
    """Generate a complete random planet profile.

    All parameters are optional — pass them to constrain the generation.
    Returns a dict with everything needed to initialize a planet in the terrain editor.
    """
    if seed is not None:
        random.seed(seed)

    sdm = SpeciesDataManager.get_instance()

    # Pick biome
    if biome and biome in BIOMES:
        biome_key = biome
    else:
        biome_key = random.choice(list(BIOMES.keys()))
    biome_data = BIOMES[biome_key]

    # Pick government
    gov_types = list(sdm.government_types.keys())
    if government and government in sdm.government_types:
        gov = government
    else:
        gov = random.choice(gov_types)

    # Pick tech level (weighted toward lower tiers for colony gameplay)
    if tech_level is not None and 0 <= tech_level <= 10:
        tl = tech_level
    else:
        # Weight: 0-5 are more common for planets you'd colonize/develop
        weights = [5, 10, 15, 20, 15, 10, 8, 5, 3, 2, 1]  # TL 0-10
        tl = random.choices(range(11), weights=weights, k=1)[0]

    # Build civilization identifier
    civ_id = f"{gov}_{tl}"
    species = sdm.get_species_by_identifier(civ_id)
    gov_info = sdm.get_government_info(gov)
    tech_info = sdm.get_tech_level_info(tl)

    # Planet name
    planet_name = _generate_planet_name()

    # Resource deposits (full 35-resource system)
    deposits, present_resources, harvestable_resources = _generate_resource_zones(biome_key, biome_data, tl)

    # Fauna / ecosystem layer
    fauna = generate_fauna(biome_key)

    # Determine available buildings from tech level
    buildings_available = tech_info.get("buildings_available", [])

    # Starting treasury scales with tech level
    starting_credits = 1000 + (tl * 500)

    # Population estimate (tech level affects density)
    if tl <= 1:
        pop_range = (50, 500)
    elif tl <= 3:
        pop_range = (500, 5000)
    elif tl <= 5:
        pop_range = (5000, 100000)
    elif tl <= 7:
        pop_range = (100000, 10000000)
    else:
        pop_range = (10000000, 1000000000)
    population = random.randint(*pop_range)

    planet = {
        "name": planet_name,
        "seed": seed if seed is not None else random.randint(0, 999999),
        "biome": biome_key,
        "biome_name": biome_data["name"],
        "biome_description": biome_data["description"],
        "terrain_params": biome_data["terrain"],
        "water_coverage": biome_data["water_coverage"],
        "temperature": biome_data["temperature"],
        "vegetation": biome_data["vegetation"],

        "civilization_id": civ_id,
        "species_name": species.get("name", "Unknown"),
        "species_physical_desc": species.get("physical_desc", ""),
        "species_culture": species.get("culture_notes", ""),
        "building_style": species.get("building_style", gov_info.get("building_style", "")),

        "government_type": gov,
        "government_name": gov_info.get("name", "Unknown"),
        "government_description": gov_info.get("description", ""),
        "government_tendencies": gov_info.get("tendencies", []),
        "government_color": gov_info.get("color", [200, 200, 200]),

        "tech_level": tl,
        "tech_name": tech_info.get("name", "Unknown"),
        "tech_era": tech_info.get("era", "Unknown"),
        "tech_description": tech_info.get("description", ""),
        "tech_capabilities": tech_info.get("capabilities", []),
        "power_source": tech_info.get("power_source", "unknown"),

        "buildings_available": buildings_available,
        "resource_deposits": deposits,
        "resources_present": present_resources,
        "resources_harvestable": harvestable_resources,
        "resources_locked": [r for r in present_resources if r not in harvestable_resources],

        "fauna": fauna,

        "starting_credits": starting_credits,
        "population": population,
    }

    return planet


# ── Current Planet State ──
# Singleton holding the active planet (one at a time)

_current_planet = None


def get_current_planet():
    global _current_planet
    return _current_planet


def set_current_planet(planet):
    global _current_planet
    _current_planet = planet


# ── CLI test ──

if __name__ == "__main__":
    import json
    p = generate_planet()
    print(json.dumps(p, indent=2))
