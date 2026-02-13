"""
EDEN Planetary Resource Database
Adapted from spacegame430 resource_data.py

35 resources across 7 categories, each with:
- Extraction facilities required
- Storage facilities required
- Tech tier (1-5) gating when the resource becomes harvestable
- Power usage and staff requirements
- Biome affinities (which biomes naturally contain this resource)

The tech tiers here (1-5) map to the species_data_manager tiers as follows:
  Resource tier 1 → harvestable at Tech Level 1+ (Ancient / Bronze Age)
  Resource tier 2 → harvestable at Tech Level 2+ (Classical / Iron Age)
  Resource tier 3 → harvestable at Tech Level 3+ (Medieval)
  Resource tier 4 → harvestable at Tech Level 4+ (Industrial)
  Resource tier 5 → harvestable at Tech Level 6+ (Early Interstellar)
"""


# ── Master Resource Database ──

RESOURCES = {
    # === WATER-BASED ===
    "Water": {
        "category": "water",
        "extraction": ["Pump Array", "Water Treatment Plant", "Drilling Platform"],
        "storage": ["Storage Tank", "Reservoir", "Purification System"],
        "tier": 1,
        "power_usage": 20,
        "staff_required": 5,
        "description": "Essential resource for life support and industrial processes.",
        "biome_affinity": ["temperate_forest", "ocean_world", "swamp", "savanna"],
    },
    "Water Ice": {
        "category": "water",
        "extraction": ["Thermal Drill", "Ice Mining Rig", "Surface Scraper"],
        "storage": ["Refrigerated Vault", "Insulated Container"],
        "tier": 1,
        "power_usage": 25,
        "staff_required": 8,
        "description": "Frozen water found on icy moons and cold planets.",
        "biome_affinity": ["tundra"],
    },
    "Salt Compounds": {
        "category": "water",
        "extraction": ["Desalination Plant", "Evaporation Pond"],
        "storage": ["Silo Storage", "Moisture-Controlled Warehouse"],
        "tier": 1,
        "power_usage": 15,
        "staff_required": 4,
        "description": "Various salt minerals extracted primarily from oceanic worlds.",
        "biome_affinity": ["ocean_world", "arid_desert"],
    },
    "Marine Biomass": {
        "category": "water",
        "extraction": ["Aquaculture Facility", "Biomass Extractor"],
        "storage": ["Bioreactor", "Refrigerated Storage"],
        "tier": 2,
        "power_usage": 30,
        "staff_required": 12,
        "description": "Organic material harvested from oceanic ecosystems.",
        "biome_affinity": ["ocean_world", "swamp"],
    },

    # === ATMOSPHERIC ===
    "Oxygen": {
        "category": "atmospheric",
        "extraction": ["Air Separation Unit", "Electrolysis Plant"],
        "storage": ["Compression Tank", "Cryogenic Storage"],
        "tier": 1,
        "power_usage": 18,
        "staff_required": 3,
        "description": "Essential gas for life support systems.",
        "biome_affinity": ["temperate_forest", "savanna", "swamp", "ocean_world"],
    },
    "Hydrogen": {
        "category": "atmospheric",
        "extraction": ["Electrolysis Array", "Gas Separator"],
        "storage": ["Pressure Vessel", "Cryogenic Storage"],
        "tier": 2,
        "power_usage": 25,
        "staff_required": 4,
        "description": "Versatile fuel resource with many industrial applications.",
        "biome_affinity": ["volcanic", "ocean_world"],
    },
    "Nitrogen": {
        "category": "atmospheric",
        "extraction": ["Atmospheric Processor", "Distillation Column"],
        "storage": ["Pressurized Tank", "Cryogenic Storage"],
        "tier": 1,
        "power_usage": 20,
        "staff_required": 3,
        "description": "Essential for agriculture and many industrial processes.",
        "biome_affinity": ["temperate_forest", "savanna", "swamp"],
    },
    "Helium": {
        "category": "atmospheric",
        "extraction": ["Gas Capture Array", "Diffusion Separator"],
        "storage": ["High-Pressure Tank", "Leak-Proof Containment"],
        "tier": 2,
        "power_usage": 28,
        "staff_required": 5,
        "description": "Valuable noble gas used in various scientific applications.",
        "biome_affinity": ["volcanic", "crystalline"],
    },
    "Helium-3": {
        "category": "atmospheric",
        "extraction": ["Isotope Separator", "Regolith Processor"],
        "storage": ["Specialized Containment Unit"],
        "tier": 4,
        "power_usage": 60,
        "staff_required": 15,
        "description": "Rare isotope primarily used in advanced fusion energy.",
        "biome_affinity": ["tundra", "crystalline"],
    },
    "Methane": {
        "category": "atmospheric",
        "extraction": ["Gas Extractor", "Hydrocarbon Processor"],
        "storage": ["Pressurized Tank", "Refrigerated Storage"],
        "tier": 2,
        "power_usage": 22,
        "staff_required": 6,
        "description": "Hydrocarbon gas found on many cold worlds.",
        "biome_affinity": ["swamp", "tundra"],
    },
    "Ammonia": {
        "category": "atmospheric",
        "extraction": ["Gas Collection System", "Chemical Synthesizer"],
        "storage": ["Pressurized Refrigerated Tank"],
        "tier": 2,
        "power_usage": 25,
        "staff_required": 7,
        "description": "Compound used in agriculture and chemical manufacturing.",
        "biome_affinity": ["volcanic", "tundra"],
    },
    "Carbon Dioxide": {
        "category": "atmospheric",
        "extraction": ["Atmospheric Carbon Scrubber", "Greenhouse Gas Collector", "Thermal Vent Tap"],
        "storage": ["Pressurized Tank", "Cryogenic Storage", "Geological Sequestration Cavity"],
        "tier": 2,
        "power_usage": 22,
        "staff_required": 7,
        "description": "Common greenhouse gas. Essential for plant cultivation and chemical manufacturing.",
        "biome_affinity": ["volcanic", "arid_desert"],
    },

    # === MINERAL ===
    "Iron": {
        "category": "mineral",
        "extraction": ["Mining Complex", "Magnetic Separator"],
        "storage": ["Open Stockpile", "Warehouse"],
        "tier": 1,
        "power_usage": 30,
        "staff_required": 15,
        "description": "Fundamental structural material for construction.",
        "biome_affinity": ["mountainous", "volcanic", "arid_desert", "crystalline"],
    },
    "Silicon": {
        "category": "mineral",
        "extraction": ["Quarry", "Silicon Refinery"],
        "storage": ["Warehouse Storage", "Dust-Free Container"],
        "tier": 2,
        "power_usage": 35,
        "staff_required": 12,
        "description": "Essential material for electronics and solar technology.",
        "biome_affinity": ["arid_desert", "crystalline", "mountainous"],
    },
    "Nickel": {
        "category": "mineral",
        "extraction": ["Deep Mining Operation", "Electromagnetic Separator"],
        "storage": ["Storage Bunker", "Oxidation-Controlled Warehouse"],
        "tier": 2,
        "power_usage": 32,
        "staff_required": 14,
        "description": "Important metal for creating advanced alloys.",
        "biome_affinity": ["volcanic", "mountainous"],
    },
    "Carbon": {
        "category": "mineral",
        "extraction": ["Carbon Mine", "Graphite Extractor"],
        "storage": ["Sealed Warehouse", "Anti-Combustion Storage"],
        "tier": 1,
        "power_usage": 25,
        "staff_required": 10,
        "description": "Versatile element used in countless applications.",
        "biome_affinity": ["temperate_forest", "swamp", "mountainous"],
    },
    "Aluminum": {
        "category": "mineral",
        "extraction": ["Bauxite Processing Facility", "Electrolysis Plant"],
        "storage": ["Moisture-Controlled Warehouse"],
        "tier": 2,
        "power_usage": 40,
        "staff_required": 16,
        "description": "Lightweight structural material with excellent properties.",
        "biome_affinity": ["savanna", "mountainous", "arid_desert"],
    },
    "Limestone": {
        "category": "mineral",
        "extraction": ["Quarry", "Crushing Plant"],
        "storage": ["Open Stockpile", "Warehouse"],
        "tier": 1,
        "power_usage": 20,
        "staff_required": 10,
        "description": "Sedimentary rock essential for cement and construction.",
        "biome_affinity": ["mountainous", "temperate_forest", "ocean_world", "savanna"],
    },
    "Titanium": {
        "category": "mineral",
        "extraction": ["Heavy Mineral Separation Plant", "Advanced Refinery"],
        "storage": ["Oxidation-Protected Storage"],
        "tier": 3,
        "power_usage": 45,
        "staff_required": 20,
        "description": "High-strength lightweight metal for aerospace applications.",
        "biome_affinity": ["volcanic", "mountainous"],
    },
    "Platinum": {
        "category": "mineral",
        "extraction": ["Precision Mining Operation", "Chemical Separation Unit"],
        "storage": ["High-Security Vault"],
        "tier": 4,
        "power_usage": 50,
        "staff_required": 25,
        "description": "Precious metal used as catalyst and in electronics.",
        "biome_affinity": ["volcanic", "crystalline"],
    },
    "Gold": {
        "category": "mineral",
        "extraction": ["Gravity Concentration Facility", "Cyanide Leaching Plant"],
        "storage": ["High-Security Vault"],
        "tier": 4,
        "power_usage": 45,
        "staff_required": 25,
        "description": "Precious metal used as conductor and currency reserve.",
        "biome_affinity": ["mountainous", "arid_desert"],
    },
    "Silver": {
        "category": "mineral",
        "extraction": ["Electrolytic Refining Plant", "Precipitation System"],
        "storage": ["Tarnish-Protected Vault"],
        "tier": 3,
        "power_usage": 40,
        "staff_required": 20,
        "description": "Valuable metal with excellent conductive properties.",
        "biome_affinity": ["mountainous", "volcanic"],
    },
    "Uranium": {
        "category": "mineral",
        "extraction": ["Radiation-Shielded Mining", "Yellowcake Processing Plant"],
        "storage": ["Radiation-Shielded Containment"],
        "tier": 4,
        "power_usage": 55,
        "staff_required": 30,
        "description": "Radioactive element used for nuclear power generation.",
        "biome_affinity": ["mountainous", "arid_desert", "tundra"],
    },

    # === ORGANIC ===
    "Organic Matter": {
        "category": "organic",
        "extraction": ["Harvesting Machine", "Biomass Processor"],
        "storage": ["Refrigerated Silo", "Anaerobic Storage"],
        "tier": 1,
        "power_usage": 18,
        "staff_required": 8,
        "description": "Raw biological material from vegetation-rich worlds.",
        "biome_affinity": ["temperate_forest", "swamp", "savanna", "ocean_world"],
    },
    "Rare Flora": {
        "category": "organic",
        "extraction": ["Botanical Collection Dome", "Cultivation Chamber"],
        "storage": ["Climate-Controlled Greenhouse", "Seed Bank"],
        "tier": 3,
        "power_usage": 35,
        "staff_required": 15,
        "description": "Unique plant species with scientific or pharmaceutical value.",
        "biome_affinity": ["temperate_forest", "swamp", "crystalline"],
    },
    "Wood": {
        "category": "organic",
        "extraction": ["Lumber Mill", "Forestry Station"],
        "storage": ["Timber Yard", "Drying Shed"],
        "tier": 1,
        "power_usage": 15,
        "staff_required": 8,
        "description": "Fundamental construction and fuel material from forests.",
        "biome_affinity": ["temperate_forest", "swamp", "savanna"],
    },

    # === GEOLOGICAL ===
    "Mineral Deposits": {
        "category": "geological",
        "extraction": ["Specialized Mining Equipment", "Geological Scanner"],
        "storage": ["Sorted Storage Yard", "Classification Facility"],
        "tier": 1,
        "power_usage": 25,
        "staff_required": 10,
        "description": "Various valuable minerals found in planetary crust.",
        "biome_affinity": ["mountainous", "volcanic", "crystalline", "arid_desert"],
    },
    "Rare Crystals": {
        "category": "geological",
        "extraction": ["Precision Extraction Equipment", "Crystal Growth Chamber"],
        "storage": ["Cushioned Container", "Vibration-Free Storage"],
        "tier": 3,
        "power_usage": 40,
        "staff_required": 18,
        "description": "Unusual crystal formations with unique properties.",
        "biome_affinity": ["crystalline", "volcanic"],
    },
    "Diamond": {
        "category": "geological",
        "extraction": ["Diamond Drilling Operation", "Kimberlite Processing"],
        "storage": ["High-Security Vault", "Hardness-Graded Storage"],
        "tier": 3,
        "power_usage": 45,
        "staff_required": 20,
        "description": "Ultra-hard carbon crystals for industrial and scientific use.",
        "biome_affinity": ["volcanic", "mountainous"],
    },
    "Sulfur": {
        "category": "geological",
        "extraction": ["Volcanic Gas Collector", "Precipitation System"],
        "storage": ["Sealed Container", "Temperature-Controlled Storage"],
        "tier": 2,
        "power_usage": 25,
        "staff_required": 8,
        "description": "Element often found near volcanic activity.",
        "biome_affinity": ["volcanic", "swamp"],
    },
    "Geothermal Energy": {
        "category": "geological",
        "extraction": ["Thermal Tap Station", "Heat Exchanger"],
        "storage": ["Thermal Battery Array", "Insulated System"],
        "tier": 2,
        "power_usage": 10,
        "staff_required": 6,
        "description": "Heat energy directly harvested from planetary crust.",
        "biome_affinity": ["volcanic", "mountainous"],
    },
    "Oil": {
        "category": "geological",
        "extraction": ["Drilling Rig", "Pump Jack", "Offshore Platform"],
        "storage": ["Tank Farm", "Refinery Storage"],
        "tier": 2,
        "power_usage": 30,
        "staff_required": 12,
        "description": "Fossil hydrocarbon fuel formed from ancient organic deposits.",
        "biome_affinity": ["arid_desert", "swamp", "tundra", "ocean_world"],
    },

    # === RARE / EXOTIC ===
    "Dark Matter": {
        "category": "rare",
        "extraction": ["Exotic Particle Collector", "Gravitometric Array"],
        "storage": ["Specialized Containment Field", "Zero-Point Chamber"],
        "tier": 5,
        "power_usage": 100,
        "staff_required": 40,
        "description": "Mysterious matter that interacts primarily through gravity.",
        "biome_affinity": ["crystalline"],
    },
    "Exotic Matter": {
        "category": "rare",
        "extraction": ["Quantum State Manipulator", "Probability Field Generator"],
        "storage": ["Quantum-Stabilized Container", "Temporal Stasis Field"],
        "tier": 5,
        "power_usage": 120,
        "staff_required": 45,
        "description": "Matter with unusual quantum properties defying normal physics.",
        "biome_affinity": ["crystalline"],
    },
    "Ancient Artifacts": {
        "category": "rare",
        "extraction": ["Archaeological Excavation Dome", "Preservation Robot"],
        "storage": ["Climate-Controlled Museum", "Stasis Field"],
        "tier": 4,
        "power_usage": 35,
        "staff_required": 25,
        "description": "Relics from previous civilizations requiring careful handling.",
        "biome_affinity": ["arid_desert", "mountainous", "temperate_forest", "crystalline"],
    },
}


# ── Category Lists ──

RESOURCE_CATEGORIES = {
    "water":       [k for k, v in RESOURCES.items() if v["category"] == "water"],
    "atmospheric": [k for k, v in RESOURCES.items() if v["category"] == "atmospheric"],
    "mineral":     [k for k, v in RESOURCES.items() if v["category"] == "mineral"],
    "organic":     [k for k, v in RESOURCES.items() if v["category"] == "organic"],
    "geological":  [k for k, v in RESOURCES.items() if v["category"] == "geological"],
    "rare":        [k for k, v in RESOURCES.items() if v["category"] == "rare"],
}


# ── Tier Lists ──

TIER_RESOURCES = {}
for name, data in RESOURCES.items():
    t = data["tier"]
    TIER_RESOURCES.setdefault(t, []).append(name)


# ── Lookup Functions ──

def get_resource(name):
    """Get full resource data by name. Returns None if not found."""
    return RESOURCES.get(name)


def get_resources_by_tier(tier):
    """Get all resources available at a specific tech tier."""
    return TIER_RESOURCES.get(tier, [])


def get_resources_by_category(category):
    """Get all resources in a category ('water', 'mineral', etc.)."""
    return RESOURCE_CATEGORIES.get(category.lower(), [])


def get_resources_for_biome(biome_key):
    """Get all resources that have affinity for a given biome."""
    return [name for name, data in RESOURCES.items() if biome_key in data.get("biome_affinity", [])]


def get_harvestable_resources(tech_level):
    """Get all resources harvestable at a given tech level.

    Mapping: resource tier 1-4 → tech level 1-4, tier 5 → tech level 6+
    """
    results = []
    for name, data in RESOURCES.items():
        rtier = data["tier"]
        min_tl = rtier if rtier <= 4 else 6
        if tech_level >= min_tl:
            results.append(name)
    return results


def get_resources_for_planet(biome_key, tech_level):
    """Get resources that exist on a biome AND are harvestable at a tech level.

    Returns two lists: (present, harvestable)
    - present: all resources with affinity for this biome
    - harvestable: subset the civilization can actually extract
    """
    present = get_resources_for_biome(biome_key)
    harvestable_set = set(get_harvestable_resources(tech_level))
    harvestable = [r for r in present if r in harvestable_set]
    return present, harvestable
