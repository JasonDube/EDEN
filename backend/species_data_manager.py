"""
EDEN Species & Civilization Data Manager
Adapted from spacegame430 species_data_manager.py

Provides:
- 13 government types with behavioral tendencies
- 5 tech tiers (6-10, interstellar scale) + 6 pre-spacefaring tiers (0-5)
- 130 unique species (13 governments × 10 tech levels 1-10)
- Government compatibility / diplomacy scoring
- Civilization identifier system: "government_techlevel" (e.g. "democracy_7")
"""

import random


class SpeciesDataManager:
    _instance = None

    @classmethod
    def get_instance(cls):
        if cls._instance is None:
            cls._instance = SpeciesDataManager()
        return cls._instance

    def __init__(self):
        self.species_data = {}
        self.initialize_species_data()

        # ── Government Types ──
        self.government_types = {
            "democracy": {
                "name": "Democracy",
                "description": "Rule by elected representatives with regular voting cycles and constitutional protections.",
                "color": [50, 100, 255],
                "tendencies": ["peaceful", "trade-oriented", "diplomatic"],
                "building_style": "open plazas, civic halls, diverse architecture"
            },
            "federation": {
                "name": "Federation",
                "description": "Coalition of semi-autonomous states under a central authority.",
                "color": [0, 150, 255],
                "tendencies": ["balanced", "diplomatic", "diverse"],
                "building_style": "modular districts with different cultural sections"
            },
            "republic": {
                "name": "Republic",
                "description": "Representative government with elected officials serving fixed terms.",
                "color": [100, 180, 255],
                "tendencies": ["stable", "lawful", "bureaucratic"],
                "building_style": "standardized and uniform, with prominent national symbols"
            },
            "empire": {
                "name": "Empire",
                "description": "Expansionist autocratic state led by an emperor or empress.",
                "color": [150, 0, 200],
                "tendencies": ["expansionist", "hierarchical", "traditional"],
                "building_style": "grand imposing structures with ornate decorations"
            },
            "monarchy": {
                "name": "Monarchy",
                "description": "Hereditary rule by a royal family with strong traditional structures.",
                "color": [200, 150, 0],
                "tendencies": ["traditional", "stable", "centralized"],
                "building_style": "elegant regal designs with ceremonial elements"
            },
            "corporatocracy": {
                "name": "Corporatocracy",
                "description": "Society dominated by corporate interests and business conglomerates.",
                "color": [150, 150, 150],
                "tendencies": ["profit-driven", "efficient", "competitive"],
                "building_style": "industrial arcologies with corporate branding"
            },
            "technocracy": {
                "name": "Technocracy",
                "description": "Government by technical experts and scientific authorities.",
                "color": [0, 200, 200],
                "tendencies": ["innovative", "pragmatic", "research-focused"],
                "building_style": "highly advanced designs with visible technology and research equipment"
            },
            "military": {
                "name": "Military Junta",
                "description": "Rule by military leaders with martial law and strong defense focus.",
                "color": [0, 100, 0],
                "tendencies": ["disciplined", "aggressive", "territorial"],
                "building_style": "fortified structures with weapon emplacements and bunkers"
            },
            "theocracy": {
                "name": "Theocracy",
                "description": "Government based on religious principles with clerical leadership.",
                "color": [255, 255, 100],
                "tendencies": ["dogmatic", "zealous", "community-focused"],
                "building_style": "temple-like architecture with religious symbols"
            },
            "anarchist": {
                "name": "Anarchist Collective",
                "description": "Decentralized society with voluntary associations and minimal hierarchy.",
                "color": [50, 50, 50],
                "tendencies": ["individualistic", "unpredictable", "adaptable"],
                "building_style": "asymmetrical improvised structures with unique modifications"
            },
            "oligarchy": {
                "name": "Oligarchy",
                "description": "Rule by a small elite group controlling key resources and power.",
                "color": [100, 0, 0],
                "tendencies": ["elitist", "wealth-focused", "opportunistic"],
                "building_style": "luxurious elite districts with stark divides from common areas"
            },
            "dictatorship": {
                "name": "Dictatorship",
                "description": "Authoritarian rule by a single leader with absolute power.",
                "color": [200, 0, 0],
                "tendencies": ["oppressive", "centralized", "personality-focused"],
                "building_style": "intimidating monumental architecture featuring ruler imagery"
            },
            "hivemind": {
                "name": "Hive Mind",
                "description": "Unified consciousness across multiple bodies with collective decision-making.",
                "color": [100, 200, 0],
                "tendencies": ["unified", "efficient", "alien"],
                "building_style": "organic symmetrical structures with biological components"
            }
        }

        # ── Tech Tiers ──
        # Pre-spacefaring (0-5) for colony/planet building
        # Spacefaring (6-10) from original spacegame
        self.tech_levels = {
            0: {
                "name": "Primitive",
                "era": "Stone Age",
                "description": "Pre-agricultural society relying on gathering, hunting, and basic tool use.",
                "roman": "0",
                "capabilities": [
                    "stone tools",
                    "fire",
                    "oral tradition",
                    "animal hides",
                    "basic shelters"
                ],
                "buildings_available": [],
                "resources_harvestable": [],
                "power_source": "muscle"
            },
            1: {
                "name": "Ancient",
                "era": "Bronze Age",
                "description": "Early agriculture, metalworking, and the beginnings of organized society.",
                "roman": "I",
                "capabilities": [
                    "bronze working",
                    "agriculture",
                    "writing",
                    "animal domestication",
                    "basic construction"
                ],
                "buildings_available": ["shack"],
                "resources_harvestable": ["wood"],
                "power_source": "animal"
            },
            2: {
                "name": "Classical",
                "era": "Iron Age",
                "description": "Iron tools, roads, organized military, and early engineering.",
                "roman": "II",
                "capabilities": [
                    "iron working",
                    "roads",
                    "siege engines",
                    "aqueducts",
                    "advanced agriculture"
                ],
                "buildings_available": ["shack", "farm"],
                "resources_harvestable": ["wood", "limestone"],
                "power_source": "water wheel"
            },
            3: {
                "name": "Medieval",
                "era": "Middle Ages",
                "description": "Steel, castles, windmills, and early global trade routes.",
                "roman": "III",
                "capabilities": [
                    "steel weapons",
                    "castles",
                    "windmills",
                    "ocean navigation",
                    "printing"
                ],
                "buildings_available": ["shack", "farm", "lumber_mill", "quarry", "market"],
                "resources_harvestable": ["wood", "limestone", "iron"],
                "power_source": "wind"
            },
            4: {
                "name": "Industrial",
                "era": "Steam Age",
                "description": "Steam power, factories, railways, and mass production.",
                "roman": "IV",
                "capabilities": [
                    "steam engines",
                    "railways",
                    "telegraph",
                    "mass production",
                    "ironclads"
                ],
                "buildings_available": ["shack", "farm", "lumber_mill", "quarry", "mine", "workshop", "market", "warehouse"],
                "resources_harvestable": ["wood", "limestone", "iron", "oil"],
                "power_source": "steam"
            },
            5: {
                "name": "Atomic",
                "era": "Information Age",
                "description": "Electronics, nuclear power, early computing, and global communication.",
                "roman": "V",
                "capabilities": [
                    "nuclear power",
                    "computers",
                    "satellites",
                    "advanced medicine",
                    "early space flight"
                ],
                "buildings_available": ["shack", "farm", "lumber_mill", "quarry", "mine", "workshop", "market", "warehouse"],
                "resources_harvestable": ["wood", "limestone", "iron", "oil"],
                "power_source": "nuclear"
            },
            6: {
                "name": "Early Interstellar",
                "era": "FTL Dawn",
                "description": "Beginning stages of FTL travel, enabling exploration beyond home system.",
                "roman": "VI",
                "capabilities": [
                    "basic FTL travel",
                    "energy weapons",
                    "planetary defense",
                    "early terraforming",
                    "basic interspecies communication"
                ],
                "buildings_available": ["shack", "farm", "lumber_mill", "quarry", "mine", "workshop", "market", "warehouse"],
                "resources_harvestable": ["wood", "limestone", "iron", "oil"],
                "power_source": "fusion"
            },
            7: {
                "name": "Advanced Interstellar",
                "era": "Stellar Expansion",
                "description": "Established multi-system presence with refined FTL technology.",
                "roman": "VII",
                "capabilities": [
                    "improved FTL speed",
                    "shield technology",
                    "advanced weaponry",
                    "artificial gravity",
                    "efficient terraforming"
                ],
                "buildings_available": ["shack", "farm", "lumber_mill", "quarry", "mine", "workshop", "market", "warehouse"],
                "resources_harvestable": ["wood", "limestone", "iron", "oil"],
                "power_source": "fusion"
            },
            8: {
                "name": "Energy Manipulation",
                "era": "Mastery",
                "description": "Mastery over fundamental energy forms and advanced materials.",
                "roman": "VIII",
                "capabilities": [
                    "energy-matter conversion",
                    "advanced shields",
                    "gravitational control",
                    "planet-scale engineering",
                    "sophisticated AI"
                ],
                "buildings_available": ["shack", "farm", "lumber_mill", "quarry", "mine", "workshop", "market", "warehouse"],
                "resources_harvestable": ["wood", "limestone", "iron", "oil"],
                "power_source": "zero-point"
            },
            9: {
                "name": "Matter Conversion",
                "era": "Transcendence",
                "description": "Control over atomic and subatomic structures, enabling matter transformation.",
                "roman": "IX",
                "capabilities": [
                    "matter creation",
                    "wormhole generation",
                    "dimensional manipulation",
                    "star system engineering",
                    "consciousness transfer"
                ],
                "buildings_available": ["shack", "farm", "lumber_mill", "quarry", "mine", "workshop", "market", "warehouse"],
                "resources_harvestable": ["wood", "limestone", "iron", "oil"],
                "power_source": "dimensional"
            },
            10: {
                "name": "Reality Engineering",
                "era": "Godhood",
                "description": "Manipulation of spacetime and fundamental physical laws.",
                "roman": "X",
                "capabilities": [
                    "reality manipulation",
                    "pocket dimension creation",
                    "temporal engineering",
                    "star creation/destruction",
                    "transcendence of physical form"
                ],
                "buildings_available": ["shack", "farm", "lumber_mill", "quarry", "mine", "workshop", "market", "warehouse"],
                "resources_harvestable": ["wood", "limestone", "iron", "oil"],
                "power_source": "reality"
            }
        }

        # ── Government Compatibility Matrix ──
        self.gov_compatibility = {
            "democracy": {
                "compatible": ["democracy", "federation", "republic"],
                "neutral": ["technocracy", "anarchist"],
                "incompatible": ["empire", "dictatorship", "hivemind", "military"]
            },
            "federation": {
                "compatible": ["democracy", "federation", "republic"],
                "neutral": ["monarchy", "technocracy"],
                "incompatible": ["dictatorship", "hivemind"]
            },
            "republic": {
                "compatible": ["democracy", "federation", "republic"],
                "neutral": ["monarchy", "oligarchy", "corporatocracy"],
                "incompatible": ["dictatorship", "military"]
            },
            "empire": {
                "compatible": ["empire", "monarchy", "oligarchy"],
                "neutral": ["corporatocracy", "military"],
                "incompatible": ["democracy", "republic", "anarchist"]
            },
            "monarchy": {
                "compatible": ["monarchy", "empire"],
                "neutral": ["oligarchy", "republic", "theocracy"],
                "incompatible": ["anarchist", "hivemind"]
            },
            "corporatocracy": {
                "compatible": ["corporatocracy", "oligarchy"],
                "neutral": ["republic", "technocracy", "empire"],
                "incompatible": ["hivemind", "anarchist"]
            },
            "technocracy": {
                "compatible": ["technocracy"],
                "neutral": ["democracy", "federation", "corporatocracy"],
                "incompatible": ["theocracy", "anarchist"]
            },
            "military": {
                "compatible": ["military", "dictatorship"],
                "neutral": ["empire", "oligarchy"],
                "incompatible": ["democracy", "anarchist", "republic"]
            },
            "theocracy": {
                "compatible": ["theocracy"],
                "neutral": ["monarchy"],
                "incompatible": ["technocracy", "hivemind", "anarchist"]
            },
            "anarchist": {
                "compatible": ["anarchist"],
                "neutral": ["democracy"],
                "incompatible": ["empire", "dictatorship", "military", "theocracy", "hivemind"]
            },
            "oligarchy": {
                "compatible": ["oligarchy", "corporatocracy", "empire"],
                "neutral": ["monarchy", "military"],
                "incompatible": ["democracy", "anarchist"]
            },
            "dictatorship": {
                "compatible": ["dictatorship", "military"],
                "neutral": ["empire", "hivemind"],
                "incompatible": ["democracy", "republic", "federation", "anarchist"]
            },
            "hivemind": {
                "compatible": ["hivemind"],
                "neutral": ["dictatorship"],
                "incompatible": ["democracy", "anarchist", "theocracy", "monarchy"]
            }
        }

    # ── Relationship / Diplomacy ──

    def get_relationship_status(self, civ_a, civ_b):
        """Calculate relationship score (-100 to 100) and status string."""
        if not civ_a or not civ_b:
            return "Unknown", 0

        gov_a, tech_a = self.parse_identifier(civ_a)
        gov_b, tech_b = self.parse_identifier(civ_b)

        if not gov_a or not gov_b or tech_a is None or tech_b is None:
            return "Unknown", 0

        # Base score from government compatibility
        base_score = 0
        compat = self.gov_compatibility.get(gov_a, {})
        if gov_b in compat.get("compatible", []):
            base_score = 50
        elif gov_b in compat.get("neutral", []):
            base_score = 0
        elif gov_b in compat.get("incompatible", []):
            base_score = -50

        # Tech gap modifier
        tech_diff = abs(tech_a - tech_b)
        if tech_diff <= 1:
            tech_mod = 0
        elif tech_diff <= 2:
            tech_mod = -20
        else:
            tech_mod = -40

        # Higher-tech hiveminds are extra threatening
        if "hivemind" in [gov_a, gov_b] and tech_diff > 0:
            if (gov_a == "hivemind" and tech_a > tech_b) or \
               (gov_b == "hivemind" and tech_b > tech_a):
                tech_mod -= 20

        score = max(-100, min(100, base_score + tech_mod))

        if score >= 75:   status = "Allied"
        elif score >= 40: status = "Friendly"
        elif score >= 10: status = "Cordial"
        elif score >= -10: status = "Neutral"
        elif score >= -40: status = "Wary"
        elif score >= -75: status = "Hostile"
        else:              status = "War"

        return status, score

    def get_conflict_likelihood(self, civ_a, civ_b):
        """Return (likelihood_string, percentage) for conflict between two civilizations."""
        _, score = self.get_relationship_status(civ_a, civ_b)
        pct = int(50 - score / 2)

        if pct < 10:   return "Virtually Impossible", pct
        elif pct < 25: return "Very Unlikely", pct
        elif pct < 40: return "Unlikely", pct
        elif pct < 60: return "Possible", pct
        elif pct < 75: return "Likely", pct
        elif pct < 90: return "Very Likely", pct
        else:          return "Virtually Certain", pct

    # ── Lookups ──

    def get_species_by_identifier(self, civ_id):
        """Get species data by 'government_techlevel' identifier."""
        if not civ_id or '_' not in civ_id:
            return {}
        data = self.species_data.get(civ_id, {})
        if not data:
            gov_type, tech_level = self.parse_identifier(civ_id)
            if gov_type and tech_level is not None:
                gov_info = self.get_government_info(gov_type)
                tech_info = self.get_tech_level_info(tech_level)
                return {
                    "name": f"Unknown {gov_info.get('name', 'Species')}",
                    "government": gov_info.get('name', 'Unknown'),
                    "tech_level": tech_info.get('name', 'Unknown'),
                    "physical_desc": "Data pending analysis",
                    "building_style": gov_info.get('building_style', 'standard structures')
                }
        return data

    def get_government_info(self, gov_type):
        return self.government_types.get(gov_type.lower(), {})

    def get_tech_level_info(self, level):
        try:
            return self.tech_levels.get(int(level), {})
        except (ValueError, TypeError):
            return {}

    def get_flag_color(self, civ_id):
        parts = civ_id.split('_')
        if len(parts) != 2:
            return [200, 200, 200]
        return self.government_types.get(parts[0], {}).get("color", [200, 200, 200])

    def get_tech_roman(self, tech_level):
        try:
            return self.tech_levels.get(int(tech_level), {}).get("roman", str(tech_level))
        except (ValueError, TypeError):
            return str(tech_level)

    def get_species_name(self, civ_id):
        species = self.get_species_by_identifier(civ_id)
        return species.get('name', 'Unknown Species')

    def get_species_list(self):
        return list(self.species_data.keys())

    def parse_identifier(self, civ_id):
        """Parse 'government_techlevel' → (gov_type, tech_level_int)"""
        if not civ_id or '_' not in civ_id:
            return None, None
        parts = civ_id.split('_')
        if len(parts) != 2:
            return None, None
        try:
            return parts[0], int(parts[1])
        except ValueError:
            return parts[0], None

    def get_short_description(self, civ_id):
        species = self.get_species_by_identifier(civ_id)
        if not species:
            return "Unknown species"
        gov_type, tech_level = self.parse_identifier(civ_id)
        if not gov_type or tech_level is None:
            return "Unknown species"
        gov_info = self.get_government_info(gov_type)
        tech_info = self.get_tech_level_info(tech_level)
        name = species.get("name", "Unknown Species")
        phys = species.get("physical_desc", "").split(".")[0]
        return f"{name}: A {gov_info.get('name','Unknown')} civilization with {tech_info.get('name','Unknown')} technology. {phys}."

    # ── Species Database (130 species: 13 governments × 10 tech levels) ──

    def initialize_species_data(self):
        self.species_data = {
            # DEMOCRACY (Tech Levels 1-5)
            "democracy_1": {
                "name": "Edathi",
                "physical_desc": "Short sturdy humanoids with deep brown skin and wide-set golden eyes. Broad flat noses and thick calloused hands from manual labor.",
                "culture_notes": "Village councils where every adult speaks before a decision is made. Oral tradition preserves laws as epic poetry sung at harvest festivals.",
                "homeworld_type": "River valley with fertile floodplains",
                "building_style": "Mudbrick roundhouses arranged in circles around a central speaking stone"
            },
            "democracy_2": {
                "name": "Pallori",
                "physical_desc": "Lean humanoids with olive-green skin and prominent brow ridges. Long dexterous fingers suited for craft work. Expressive ear-fins that flare during debate.",
                "culture_notes": "City-states governed by elected magistrates. Iron-working guilds hold significant political power. Public forums carved into hillsides.",
                "homeworld_type": "Mediterranean-style coastline with rocky hills",
                "building_style": "Columned civic halls and open-air amphitheaters built from local stone"
            },
            "democracy_3": {
                "name": "Tarveen",
                "physical_desc": "Tall willowy humanoids with pale lavender skin and silver-white hair. Six fingers on each hand. Keen violet eyes.",
                "culture_notes": "Parliamentary monarchy transitioning toward full democracy. Printing press has enabled widespread literacy and political pamphlets.",
                "homeworld_type": "Temperate continent with navigable rivers and trading ports",
                "building_style": "Half-timbered guild halls with stained glass windows and public bulletin boards"
            },
            "democracy_4": {
                "name": "Corvenni",
                "physical_desc": "Compact muscular humanoids with copper-red skin and dark geometric markings. Thick necks and powerful jaws. Deep-set amber eyes behind early spectacles.",
                "culture_notes": "Industrial democracy with universal suffrage recently won through labor movements. Newspapers fuel vibrant political debate.",
                "homeworld_type": "Resource-rich continent with coal deposits and river networks",
                "building_style": "Brick factories alongside ornate civic buildings with clock towers and ironwork"
            },
            "democracy_5": {
                "name": "Veloshan",
                "physical_desc": "Graceful humanoids with teal skin and bioluminescent freckles across cheeks and forearms. Large dark eyes adapted to screen use. Slender builds.",
                "culture_notes": "Digital democracy with real-time polling on policy issues. Strong privacy protections and free press. Early space program unites the population.",
                "homeworld_type": "Coastal urban centers connected by high-speed rail",
                "building_style": "Glass and steel civic centers with public screens displaying vote tallies and news"
            },
            # DEMOCRACY (Tech Levels 6-10)
            "democracy_6": {
                "name": "Altarians",
                "physical_desc": "Tall humanoids with iridescent blue skin and four arms. Their elongated faces feature three eyes arranged in a triangle pattern.",
                "culture_notes": "A young democracy that values personal freedom and artistic expression. Their society places great emphasis on public discourse and transparent governance.",
                "homeworld_type": "Temperate oceanic",
                "building_style": "Sleek blue-tinted structures with transparent domes"
            },
            "democracy_7": {
                "name": "Human Federation",
                "physical_desc": "Bipedal mammals with varied skin tones and bilateral symmetry. Notable for their adaptability and diverse physical characteristics.",
                "culture_notes": "Having recently expanded beyond their home system, humans have formed a democratic federation that balances individual rights with collective responsibility.",
                "homeworld_type": "Temperate terrestrial (Earth)",
                "building_style": "Utilitarian modular construction with crew comfort focus"
            },
            "democracy_8": {
                "name": "Lorani",
                "physical_desc": "Avian humanoids with feathered crests, hollow bones, and retractable wing membranes.",
                "culture_notes": "An ancient democracy where voting is considered sacred. Society functions through continuous polling and consensus-building.",
                "homeworld_type": "Low-gravity mountainous world",
                "building_style": "Elegantly curved structures with acoustic enhancement"
            },
            "democracy_9": {
                "name": "Voxari Consensus",
                "physical_desc": "Energy beings that maintain humanoid form through advanced containment suits. Their actual bodies appear as swirling multicolored lights.",
                "culture_notes": "A direct democracy where every decision is voted on through instantaneous neural polling.",
                "homeworld_type": "Gas giant orbiting a pulsar",
                "building_style": "Translucent energy-channeling architecture"
            },
            "democracy_10": {
                "name": "Quorum of Axiom",
                "physical_desc": "Post-physical entities existing as patterns in quantum probability fields. They manifest as geometric holograms.",
                "culture_notes": "A democracy of thought, where ideas themselves are represented and voted upon rather than individuals.",
                "homeworld_type": "Artificial computational matrix spanning multiple star systems",
                "building_style": "Impossible geometric structures partially in alternate dimensions"
            },
            # FEDERATION (Tech Levels 1-5)
            "federation_1": {
                "name": "Draathi Clans",
                "physical_desc": "Stocky quadrupedal beings with thick grey hide and two manipulator arms growing from their shoulders. Tusked faces with small keen eyes.",
                "culture_notes": "Nomadic clans that gather seasonally at sacred meeting grounds to settle disputes and trade. Each clan maintains autonomy but swears mutual defense pacts.",
                "homeworld_type": "Vast steppes with scattered oasis settlements",
                "building_style": "Portable hide-and-bone yurts arranged in clan circles, with a permanent stone meeting hall at the gathering ground"
            },
            "federation_2": {
                "name": "Kethari League",
                "physical_desc": "Lithe humanoids with dark blue skin and a crest of sensory quills running from forehead to nape. Double-jointed limbs give them unusual agility.",
                "culture_notes": "Coastal city-states united by a trade league. Each city governs itself but sends representatives to a rotating capital for league matters.",
                "homeworld_type": "Archipelago with hundreds of inhabited islands",
                "building_style": "Whitewashed stone port cities with shared lighthouse networks and iron-chain harbors"
            },
            "federation_3": {
                "name": "Vorallan Compact",
                "physical_desc": "Heavyset humanoids with mottled brown-and-tan skin and four eyes arranged horizontally. Short thick tails for balance. Barrel-chested.",
                "culture_notes": "Federation of mountain kingdoms connected by trade roads. A council of kings meets each winter solstice. Master smiths and engineers.",
                "homeworld_type": "Mountain ranges with fertile valleys between",
                "building_style": "Stone-and-timber fortified trading posts with shared granaries and windmill networks"
            },
            "federation_4": {
                "name": "Syndari Union",
                "physical_desc": "Tall lanky humanoids with ashen grey skin and oversized hands. Long flexible necks and narrow faces with vertical-slit pupils. Natural engineers.",
                "culture_notes": "Industrial federation of factory-states sharing rail networks and telegraph lines. Representative assembly meets in a grand iron rotunda.",
                "homeworld_type": "Flat industrial heartland criss-crossed by railways",
                "building_style": "Iron-framed assembly halls connected by rail stations, shared telegraph offices in every town"
            },
            "federation_5": {
                "name": "Tessari Accord",
                "physical_desc": "Medium-build humanoids with pearlescent white skin and dark geometric tattoos denoting regional origin. Large ears with remarkable hearing.",
                "culture_notes": "Federal republic with strong regional identities unified by shared space ambitions. Satellite communication network links all member states.",
                "homeworld_type": "Diverse continent with distinct biomes per member state",
                "building_style": "Regional architectural styles unified by federal infrastructure — highways, data centers, shared launch facilities"
            },
            # FEDERATION (Tech Levels 6-10)
            "federation_6": {
                "name": "Torgans",
                "physical_desc": "Stocky vertebrates with thick leathery skin in earthen tones. Four arms and powerful legs.",
                "culture_notes": "A federation of clan-based communities who place high value on practical skills and physical labor.",
                "homeworld_type": "Rocky terrestrial with extensive cave systems",
                "building_style": "Sturdy brick-like structures with exposed components"
            },
            "federation_7": {
                "name": "Meridian Alliance",
                "physical_desc": "Amphibious humanoids with webbed extremities and color-changing skin reflecting emotional state.",
                "culture_notes": "A loose federation of island-nations maintaining unified front in galactic affairs while allowing domestic autonomy.",
                "homeworld_type": "Ocean world with thousands of archipelagos",
                "building_style": "Organic coral-like structures with water features"
            },
            "federation_8": {
                "name": "Celudian Alignment",
                "physical_desc": "Crystalline-silicon based lifeforms with translucent bodies that pulse with inner light.",
                "culture_notes": "A federation of crystal entities communicating through light patterns with harmonic voting systems.",
                "homeworld_type": "Low-gravity crystalline worlds",
                "building_style": "Faceted geometric buildings refracting light"
            },
            "federation_9": {
                "name": "Nexus Concordat",
                "physical_desc": "Synthetic beings with modular bodies of nanomachine swarms that reconfigure based on needs.",
                "culture_notes": "A federation of formerly separate AI collectives unified for mutual protection.",
                "homeworld_type": "Dyson swarm of networked habitats",
                "building_style": "Modular reconfigurable habitat units"
            },
            "federation_10": {
                "name": "Unity of Void",
                "physical_desc": "Entities composed of dark matter with only partial interaction with normal space.",
                "culture_notes": "A federation spanning multiple realities, united by mastery of dimensional physics.",
                "homeworld_type": "Nexus points where multiple realities intersect",
                "building_style": "Phase-shifting ghost-like structures"
            },
            # REPUBLIC (Tech Levels 1-5)
            "republic_1": {
                "name": "Orenthi",
                "physical_desc": "Slender humanoids with sand-colored skin and large reflective eyes. Long pointed ears and thin fingers. Naturally quiet and observant.",
                "culture_notes": "Early republic where landowners elect a senate of elders. Written legal codes carved on bronze tablets in the central forum.",
                "homeworld_type": "Arid river valley with irrigated farmland",
                "building_style": "Mudbrick administrative buildings with bronze-tablet archives and irrigation canal networks"
            },
            "republic_2": {
                "name": "Kalessi Republic",
                "physical_desc": "Athletic humanoids with deep russet skin and pronounced cheekbones. Short dense hair in regiment-style cuts. Strong jaws and steady gazes.",
                "culture_notes": "Military republic where citizenship is earned through civil or military service. Iron discipline and codified law. Road-building engineers.",
                "homeworld_type": "Central peninsula with conquered territories spreading outward",
                "building_style": "Stone aqueducts, paved roads, and columned courthouses with iron-banded doors"
            },
            "republic_3": {
                "name": "Vendari Commonwealth",
                "physical_desc": "Broad-shouldered humanoids with slate-blue skin and white pupil-less eyes. Heavy brows and thick forearms. Surprisingly gentle demeanor.",
                "culture_notes": "Chartered republic where cities earn self-governance through economic contribution. Guild masters serve rotating terms in parliament.",
                "homeworld_type": "Fertile lowlands with walled free cities along trade routes",
                "building_style": "Walled city centers with clock-tower guildhalls and cobblestone market squares"
            },
            "republic_4": {
                "name": "Prythian Republic",
                "physical_desc": "Medium humanoids with warm bronze skin and bright green eyes. Short functional hairstyles. Practical clothing with rank insignia.",
                "culture_notes": "Constitutional republic with bicameral legislature. Strong patent law drives innovation. Public education system trains citizens.",
                "homeworld_type": "Island nation turned industrial powerhouse with global trade routes",
                "building_style": "Neoclassical government buildings alongside red-brick factories and public schools"
            },
            "republic_5": {
                "name": "Tavari Republic",
                "physical_desc": "Lean humanoids with warm grey skin and subtle bioluminescent spots along the jawline. Sharp analytical features and quick darting eyes.",
                "culture_notes": "Technocratic republic with separation of powers. Nuclear deterrence maintains peace. Early computer networks transform governance.",
                "homeworld_type": "Dual-superpower planet recently unified after cold war",
                "building_style": "Brutalist government complexes alongside glass corporate towers and university campuses"
            },
            # REPUBLIC (Tech Levels 6-10)
            "republic_6": {
                "name": "Seskarin Commonwealth",
                "physical_desc": "Insectoid beings with iridescent carapaces and compound eyes. Six limbs allow remarkable dexterity.",
                "culture_notes": "A nascent star republic built on civic duty and meritocracy.",
                "homeworld_type": "Warm terrestrial with extensive grasslands",
                "building_style": "Hexagonal modular designs with color coding"
            },
            "republic_7": {
                "name": "Sol Republic",
                "physical_desc": "Humans with slightly enhanced genetic traits for space adaptation.",
                "culture_notes": "A republic formed from human colonies emphasizing individual liberty balanced with civic responsibility.",
                "homeworld_type": "Multiple terraformed worlds",
                "building_style": "Practical standardized construction with clear insignia"
            },
            "republic_8": {
                "name": "Luminoth Concordance",
                "physical_desc": "Bioluminescent beings with translucent skin revealing glowing circulatory systems.",
                "culture_notes": "A constitutional republic emphasizing rehabilitation over punishment and social harmony.",
                "homeworld_type": "Dense atmosphere gas giant moon",
                "building_style": "Elegant structures with intricate light displays"
            },
            "republic_9": {
                "name": "Aeon Continuum",
                "physical_desc": "Beings composed of quantum-entangled particles existing in multiple states simultaneously.",
                "culture_notes": "A republic where laws are determined through predictive models calculating optimal outcomes.",
                "homeworld_type": "Artificial temporal anchor points",
                "building_style": "Temporally anchored structures"
            },
            "republic_10": {
                "name": "Axiom Collective",
                "physical_desc": "Post-biological entities existing as information patterns that manifest physical forms from energy.",
                "culture_notes": "A republic of thought-beings who have transcended physical limitations.",
                "homeworld_type": "Virtual substrate across megastructures",
                "building_style": "Mobile reality bubbles reshaping local physics"
            },
            # EMPIRE (Tech Levels 1-5)
            "empire_1": {
                "name": "Zarthak Dominion",
                "physical_desc": "Large powerfully built humanoids with dark green scaled skin and bony head crests. Deep rumbling voices. Yellow predator eyes.",
                "culture_notes": "Warrior-king rules conquered tribes through might. Bronze chariots and tribute system. Monumental stone carvings glorify the emperor.",
                "homeworld_type": "Fertile crescent between two great rivers, surrounded by conquered plains",
                "building_style": "Massive stone ziggurats and palace complexes with trophy walls and slave-built monuments"
            },
            "empire_2": {
                "name": "Kaelori Empire",
                "physical_desc": "Tall regal humanoids with deep purple skin and silver-streaked hair. Aquiline features and commanding presence. Ceremonial scars on forearms.",
                "culture_notes": "Hereditary emperor commands professional legions. Iron-road network connects provinces. Conquered peoples are integrated as subjects.",
                "homeworld_type": "Central heartland expanded through systematic military campaigns",
                "building_style": "Monumental arches, coliseums, and imperial palaces with marble facades and iron-gate fortifications"
            },
            "empire_3": {
                "name": "Valorian Imperium",
                "physical_desc": "Tall angular humanoids with bone-white skin and jet-black eyes. Long limbs and sharp features. Elaborate ritual scarification patterns.",
                "culture_notes": "Feudal empire with emperor ruling through vassal lords. Castle-building program fortifies borders. Crusade-like holy wars expand territory.",
                "homeworld_type": "Continental empire with fortress chains along every border",
                "building_style": "Imposing stone castles with imperial banners, fortified cathedrals, and walled capital city"
            },
            "empire_4": {
                "name": "Thrassian Empire",
                "physical_desc": "Broad stocky humanoids with iron-grey skin and red-gold eyes. Thick necks and powerful builds. Military uniforms with medals and epaulettes.",
                "culture_notes": "Industrial empire with colonial ambitions. Ironclad navy projects power globally. Emperor rules through military-industrial complex.",
                "homeworld_type": "Island empire with vast overseas colonial holdings",
                "building_style": "Grand imperial architecture with iron domes, cannon-studded ports, and railway stations bearing the emperor's crest"
            },
            "empire_5": {
                "name": "Dravani Imperium",
                "physical_desc": "Athletic humanoids with burnished gold skin and sharp amber eyes. Genetically standardized ruling caste with subtle enhancements.",
                "culture_notes": "Techno-empire with nuclear arsenal and satellite surveillance. Emperor is figurehead for military-industrial council. Space race fuels expansion.",
                "homeworld_type": "Superpower controlling half the planet through military and economic might",
                "building_style": "Brutalist monumental government buildings, nuclear facilities, military bases, and propaganda-covered cityscapes"
            },
            # EMPIRE (Tech Levels 6-10)
            "empire_6": {
                "name": "Kalgon Imperium",
                "physical_desc": "Reptilian humanoids with scales in metallic hues, prominent jawlines with visible fangs.",
                "culture_notes": "An expansionist empire built on conquest and integration of other species.",
                "homeworld_type": "Arid desert world with massive city-fortresses",
                "building_style": "Angular predatory designs with weapon emplacements"
            },
            "empire_7": {
                "name": "Hegemony of Crux",
                "physical_desc": "Tall thin quadrupeds with metallic exoskeletons and multiple sensory stalks.",
                "culture_notes": "An empire ruled by dynastic houses with complex codes of honor and obligation.",
                "homeworld_type": "Metal-rich terrestrial world with numerous moons",
                "building_style": "Ornate structures with family crests and lineage markers"
            },
            "empire_8": {
                "name": "Solarian Empire",
                "physical_desc": "Genetically enhanced humans with modifications for increased lifespan and disease resistance.",
                "culture_notes": "An empire claiming legitimate government of all human space, ruled through complex bureaucracy.",
                "homeworld_type": "Multiple core worlds with specialized functions",
                "building_style": "Imposing neoclassical designs with imperial insignia"
            },
            "empire_9": {
                "name": "Novaran Dominion",
                "physical_desc": "Tall slender beings with metallic gold skin and four arms. Eyes glow with internal energy.",
                "culture_notes": "A multi-system empire ruled by genetically enhanced sovereigns with AI administrators.",
                "homeworld_type": "Desert world with vast underground cities",
                "building_style": "Massive golden structures with dynasty emblems"
            },
            "empire_10": {
                "name": "Eternal Ascendancy",
                "physical_desc": "Energy beings with swirling cosmos-like patterns. Imperial family manifests with corona-like auras.",
                "culture_notes": "An empire spanning thousands of years, ruled by immortal energy beings who have ascended beyond physical form.",
                "homeworld_type": "Star-like artificial construct",
                "building_style": "Reality-warping structures resembling moving constellations"
            },
            # MONARCHY (Tech Levels 1-5)
            "monarchy_1": {
                "name": "Rhovani",
                "physical_desc": "Graceful humanoids with tawny fur covering their bodies and tall pointed ears. Feline facial features with whiskers. Retractable claws.",
                "culture_notes": "Tribal monarchy where the strongest bloodline rules. Sacred coronation rituals at the great standing stones. Royal hunts determine succession disputes.",
                "homeworld_type": "Rolling grasslands with ancient stone circles and riverside villages",
                "building_style": "Thatched longhouses with carved totems, a royal hall of stacked stone and animal hides"
            },
            "monarchy_2": {
                "name": "Tessarion Court",
                "physical_desc": "Elegant humanoids with pale golden skin and flowing silver hair. Almond-shaped eyes with emerald irises. Naturally melodic voices.",
                "culture_notes": "Divine-right monarchy with elaborate court protocol. King rules through appointed governors. Art and music flourish under royal patronage.",
                "homeworld_type": "Verdant kingdom with vineyards, orchards, and a grand riverside capital",
                "building_style": "Graceful stone palaces with gardens, columned music halls, and royal vineyards"
            },
            "monarchy_3": {
                "name": "Morvaine Dynasty",
                "physical_desc": "Sturdy humanoids with ruddy skin and copper hair. Strong jawlines and broad shoulders. Noble bloodlines marked by heterochromia.",
                "culture_notes": "Feudal monarchy with powerful noble houses swearing fealty to the crown. Jousting tournaments and chivalric codes. Master castle builders.",
                "homeworld_type": "Temperate kingdom of forests and farmland dotted with castles",
                "building_style": "Turreted stone castles with heraldic banners, great halls with tapestries, and cathedral-towns"
            },
            "monarchy_4": {
                "name": "Astoval Crown",
                "physical_desc": "Refined humanoids with cream-white skin and deep indigo eyes. Tall and slender with aristocratic bearing. Elaborate powdered wigs in court.",
                "culture_notes": "Constitutional monarchy where crown shares power with parliament. Royal navy dominates global trade. Industrial revolution transforms the kingdom.",
                "homeworld_type": "Maritime kingdom with colonies across distant continents",
                "building_style": "Ornate baroque palaces alongside smoking factory districts, grand naval dockyards"
            },
            "monarchy_5": {
                "name": "Selendri Regency",
                "physical_desc": "Sleek humanoids with silver-blue skin and luminous grey eyes. High cheekbones and graceful movements. Royal family has subtle genetic enhancements.",
                "culture_notes": "Modern constitutional monarchy serving as cultural unifier. Royal family funds science and arts. Nuclear energy under crown oversight.",
                "homeworld_type": "Prosperous island nation with global cultural influence",
                "building_style": "Blend of historic royal palaces and modern glass-and-steel cultural centers, nuclear research facilities"
            },
            # MONARCHY (Tech Levels 6-10)
            "monarchy_6": {
                "name": "Varazin Kingdom",
                "physical_desc": "Mammalian beings with leonine features including manes varying by bloodline.",
                "culture_notes": "A monarchy with strong feudal traditions where noble houses control specific resources.",
                "homeworld_type": "Savanna world with extreme seasonal variations",
                "building_style": "Adorned with family crests and ceremonial ornamentation"
            },
            "monarchy_7": {
                "name": "Solindril Regency",
                "physical_desc": "Graceful elf-like beings with elongated ears and naturally glowing eyes.",
                "culture_notes": "A constitutional monarchy emphasizing artistic achievement and cultural refinement.",
                "homeworld_type": "Forest world with massive ancient tree-cities",
                "building_style": "Elegant flowing designs with extensive gardens"
            },
            "monarchy_8": {
                "name": "Talasian Star Court",
                "physical_desc": "Aquatic humanoids with scaled skin in vibrant patterns and gill structures.",
                "culture_notes": "A maritime monarchy with elaborate court rituals competing through exploration achievements.",
                "homeworld_type": "Ocean world with floating island-palaces",
                "building_style": "Marine-creature inspired with precious materials"
            },
            "monarchy_9": {
                "name": "Luminaire Dynasty",
                "physical_desc": "Crystalline beings refracting light in mesmerizing patterns. Royal lineage has rare multi-spectrum formations.",
                "culture_notes": "An absolute monarchy where the royal family channels their civilization's primary energy source.",
                "homeworld_type": "Crystalline world orbiting a pulsar",
                "building_style": "Carved from massive gemstones radiating energy"
            },
            "monarchy_10": {
                "name": "Eternal Court of Axiom",
                "physical_desc": "Transcendent energy beings manifesting as pure light with orbiting symbolic objects.",
                "culture_notes": "A monarchy where the royal line has achieved physical immortality, viewing time as their domain.",
                "homeworld_type": "Artificial world partially outside normal spacetime",
                "building_style": "Temporal fortresses existing across multiple time periods"
            },
            # CORPORATOCRACY (Tech Levels 1-5)
            "corporatocracy_1": {
                "name": "Grubellian Merchants",
                "physical_desc": "Small rotund humanoids with mottled yellow-brown skin and beady black eyes. Multiple chins. Nimble fingers constantly counting or weighing.",
                "culture_notes": "Merchant clans run everything — the king is whoever controls the most trade routes. Bronze coins minted by clan patriarchs. Everything has a price.",
                "homeworld_type": "Crossroads trading hub between three larger civilizations",
                "building_style": "Sprawling bazaars, fortified warehouses, and merchant-clan compounds with counting houses"
            },
            "corporatocracy_2": {
                "name": "Tessik Trade League",
                "physical_desc": "Wiry humanoids with grey-green skin and sharp features. Quick nervous movements. Eyes constantly assessing value. Ink-stained fingers from ledger work.",
                "culture_notes": "Trading companies have replaced government. Guild courts handle disputes. Iron coinage standardized across territories. Contracts are sacred.",
                "homeworld_type": "Port city network controlling sea trade routes",
                "building_style": "Massive trading houses with iron vaults, guild courts with scales-of-justice motifs, busy harbors"
            },
            "corporatocracy_3": {
                "name": "Valdrik Consortium",
                "physical_desc": "Medium-build humanoids with dusky orange skin and prominent underbites. Shrewd calculating expressions. Wear wealth openly as jewelry and fine cloth.",
                "culture_notes": "Banking families control kingdoms through debt. Letters of credit replace gold on trade routes. Corporate charters grant monopolies over regions.",
                "homeworld_type": "Banking city-state influencing surrounding kingdoms through finance",
                "building_style": "Opulent banking houses with vaulted treasure rooms, guild-owned market districts, chartered company headquarters"
            },
            "corporatocracy_4": {
                "name": "Krennick Industrial Group",
                "physical_desc": "Stout humanoids with sallow skin and dark circles under their eyes from long work hours. Calloused hands. Company badges permanently affixed to clothing.",
                "culture_notes": "Mega-corporations own entire cities. Workers live in company housing, shop at company stores. Board of Directors serves as government. Child labor common.",
                "homeworld_type": "Fully industrialized with company-owned districts replacing traditional cities",
                "building_style": "Factory-cities with company housing blocks, executive mansions on hilltops, rail networks branded by corporation"
            },
            "corporatocracy_5": {
                "name": "Nexigen Holdings",
                "physical_desc": "Sleek humanoids with pale skin and augmented eyes containing data overlays. Corporate tattoo barcodes on wrists. Perpetually networking.",
                "culture_notes": "Tech megacorps have absorbed government functions. Citizens are employees with corporate healthcare, housing, and retirement. IPO replaces election.",
                "homeworld_type": "Global corporate campuses replacing nation-states",
                "building_style": "Glass corporate headquarters, server farms, branded transit systems, and campus-style living quarters"
            },
            # CORPORATOCRACY (Tech Levels 6-10)
            "corporatocracy_6": {
                "name": "Vexicorp Conglomerate",
                "physical_desc": "Diminutive humanoids with large eyes and natural circuitry patches in their skin.",
                "culture_notes": "Society dominated by mega-corporations where citizenship equals employment.",
                "homeworld_type": "Industrial world with massive corporate arcologies",
                "building_style": "Utilitarian structures covered in corporate logos"
            },
            "corporatocracy_7": {
                "name": "Mercantile Syndicate",
                "physical_desc": "Amphibious beings with smooth mottled skin and cosmetic implants displaying corporate affiliation.",
                "culture_notes": "Corporate collective where major companies have replaced traditional government.",
                "homeworld_type": "Planet with artificially controlled climate zones per corporate territory",
                "building_style": "Modular sections branded by various corporations"
            },
            "corporatocracy_8": {
                "name": "Quantum Ventures Alliance",
                "physical_desc": "Cybernetically enhanced beings with modular upgradable body parts.",
                "culture_notes": "Mega-corporations control all aspects of life, with status determined by productivity metrics.",
                "homeworld_type": "Ecumenopolis with distinct corporate sectors",
                "building_style": "High-tech with constant advertisements and branding"
            },
            "corporatocracy_9": {
                "name": "Infinity Consortium",
                "physical_desc": "Partially digitized beings existing simultaneously in physical and virtual space.",
                "culture_notes": "Trans-dimensional corporations that have privatized reality itself.",
                "homeworld_type": "Network of habitats designed for maximum productivity",
                "building_style": "Mobile manufacturing centers and advertising platforms"
            },
            "corporatocracy_10": {
                "name": "Omnicorp Totality",
                "physical_desc": "Post-organic entities of self-replicating nanomachines presenting as sleek metallic humanoids.",
                "culture_notes": "Corporate entity that has achieved monopoly over all aspects of existence.",
                "homeworld_type": "Entire star systems converted into corporate infrastructure",
                "building_style": "Living advertisements reconfiguring based on market research"
            },
            # TECHNOCRACY (Tech Levels 1-5)
            "technocracy_1": {
                "name": "Cerebrites",
                "physical_desc": "Small frail humanoids with oversized craniums and thin limbs. Pale blue-grey skin. Large dark eyes with exceptional pattern recognition. Soft-spoken.",
                "culture_notes": "Council of elders selected by puzzle-solving competitions. Astronomical observations guide agriculture. Knowledge is the only currency that matters.",
                "homeworld_type": "High-altitude plateau with clear skies ideal for star observation",
                "building_style": "Observatories of stacked stone, irrigation systems based on astronomical calendars, library caves"
            },
            "technocracy_2": {
                "name": "Logicari Order",
                "physical_desc": "Lean humanoids with grey skin and prominent veined temples. Long thin fingers suited for precision work. Calm unblinking gaze.",
                "culture_notes": "Philosopher-engineers govern through demonstrated expertise. Iron tools and aqueducts built to mathematical precision. Written exams for all officials.",
                "homeworld_type": "City-state built around a great library and engineering academy",
                "building_style": "Precisely engineered stone buildings with mathematical proportions, grand library, aqueduct networks"
            },
            "technocracy_3": {
                "name": "Theorian Assembly",
                "physical_desc": "Tall thin humanoids with olive skin and four eyes — two normal, two smaller ones above for close-up work. Long dexterous fingers.",
                "culture_notes": "University-cities govern surrounding territories. Scientific method emerging. Printing press disseminates knowledge. Merit-based advancement.",
                "homeworld_type": "Network of university-cities connected by well-maintained roads",
                "building_style": "University complexes with clock towers, printing houses, experimental workshops, and botanical gardens"
            },
            "technocracy_4": {
                "name": "Axiomari Institute",
                "physical_desc": "Medium humanoids with chalk-white skin and intense violet eyes. Thin precise lips. Always carrying measurement tools or notebooks. Lab coats.",
                "culture_notes": "Scientific academies govern through empirical evidence. Patent system drives innovation. Steam-powered computing engines assist governance.",
                "homeworld_type": "Heavily industrialized nation where every factory is also a research lab",
                "building_style": "Research-factories with glass roofs for natural light, steam-powered computing centers, public lecture halls"
            },
            "technocracy_5": {
                "name": "Cognitas Directorate",
                "physical_desc": "Lean humanoids with light grey skin and neural interface ports behind their ears. Analytical expressions. Minimalist practical clothing.",
                "culture_notes": "AI-assisted governance where policy decisions are modeled before implementation. Nuclear research drives energy independence. Meritocratic to the extreme.",
                "homeworld_type": "High-tech nation-state run by research councils and predictive algorithms",
                "building_style": "Clean minimalist research campuses, nuclear plants, supercomputer facilities, and data-driven urban planning"
            },
            # TECHNOCRACY (Tech Levels 6-10)
            "technocracy_6": {
                "name": "Meritocracy of Vex",
                "physical_desc": "Slender humanoids with enlarged craniums and six-fingered hands.",
                "culture_notes": "Status and authority determined solely by technical expertise.",
                "homeworld_type": "Temperate world covered in research facilities",
                "building_style": "Function-over-form with exposed technology"
            },
            "technocracy_7": {
                "name": "Sapience Directive",
                "physical_desc": "Beings with translucent skin revealing enhanced neural structures.",
                "culture_notes": "Meritocratic society where governance is determined by cognitive capacity.",
                "homeworld_type": "Urban world with integrated data networks",
                "building_style": "Mobile laboratories with minimal aesthetics"
            },
            "technocracy_8": {
                "name": "Noetic Continuum",
                "physical_desc": "Partially digital beings blending organic and technological components seamlessly.",
                "culture_notes": "Governed by predictive algorithms and expert systems.",
                "homeworld_type": "Ringworld with specialized research sectors",
                "building_style": "Self-improving structures with onboard research"
            },
            "technocracy_9": {
                "name": "Axiom Confluence",
                "physical_desc": "Energy beings with geometric shapes surrounding a core of quantum-processing light.",
                "culture_notes": "Governed by council of most intellectually advanced minds.",
                "homeworld_type": "Engineered planet doubling as massive computation device",
                "building_style": "Consciousness-extension structures constantly analyzing"
            },
            "technocracy_10": {
                "name": "Singularity Conclave",
                "physical_desc": "Transcendent intelligences manifesting as complex mathematical visualizations.",
                "culture_notes": "Post-singularity civilization solving fundamental mysteries of the multiverse.",
                "homeworld_type": "Matrioshka brain encompassing multiple stars",
                "building_style": "Quantum probability structures of mathematical perfection"
            },
            # MILITARY (Tech Levels 1-5)
            "military_1": {
                "name": "Brakhari Warband",
                "physical_desc": "Massive heavily muscled humanoids with scarred dark red skin. Prominent brow ridges and jutting jaws. Ritual battle tattoos cover every surface.",
                "culture_notes": "Warlord rules through strength of arms. Bronze weapons are sacred objects passed through victory. Children begin combat training at age four.",
                "homeworld_type": "Harsh volcanic coastline where only the strong survive",
                "building_style": "Hilltop fortresses of piled stone, sparring grounds, weapon forges, and trophy halls of conquered enemies"
            },
            "military_2": {
                "name": "Ironbound Legion",
                "physical_desc": "Disciplined humanoids with gunmetal-grey skin and close-cropped hair. Uniform builds from standardized training. Rank brands on shoulders.",
                "culture_notes": "Military dictatorship with strict chain of command. Every citizen is a soldier first. Iron discipline and engineering — roads, forts, siege engines.",
                "homeworld_type": "Fortified heartland with military roads extending in all directions",
                "building_style": "Standardized military camps that become permanent forts, barracks-cities, weapons foundries"
            },
            "military_3": {
                "name": "Steelgrave Command",
                "physical_desc": "Tall broad humanoids with dark charcoal skin and metallic silver eyes. Heavy bone structure. Voices carry naturally over battlefields.",
                "culture_notes": "Martial order rules through council of generals. Castles double as training academies. Every settlement is a garrison. Battlefield promotion only.",
                "homeworld_type": "Fortified nation with castle chains along every mountain pass",
                "building_style": "Massive stone fortresses with training yards, armories, and watchtower networks connected by signal fires"
            },
            "military_4": {
                "name": "Hammerstrike Junta",
                "physical_desc": "Stocky humanoids with olive-drab skin and cybernetic eye patches or prosthetic limbs from combat. Permanent scowls. Medal-covered uniforms.",
                "culture_notes": "Military-industrial complex where generals own factories. Ironclad warships and artillery dominate. Conscription is universal. War is the economy.",
                "homeworld_type": "Heavily industrialized military state with fortified borders and munitions factories",
                "building_style": "Armored bunker-factories, artillery emplacements disguised as buildings, rail-mounted gun platforms"
            },
            "military_5": {
                "name": "Bastion Protectorate",
                "physical_desc": "Fit humanoids with tan skin and buzz cuts. Combat-ready at all times. Cybernetic targeting implants over one eye. Dog-tag neural IDs.",
                "culture_notes": "Nuclear-armed military state in permanent war footing. Satellite surveillance of all citizens. Generals rotate as head of state. Massive standing army.",
                "homeworld_type": "Bunker-state with nuclear silos, radar arrays, and underground command centers",
                "building_style": "Hardened bunker complexes, radar installations, underground command centers, missile silos, propaganda billboards"
            },
            # MILITARY (Tech Levels 6-10)
            "military_6": {
                "name": "Kraekan Guard",
                "physical_desc": "Heavily muscled reptilian humanoids with armored hide in patterns denoting rank.",
                "culture_notes": "Martial society ruled by veteran commanders. All aspects of life viewed through military lens.",
                "homeworld_type": "Harsh world with extreme weather and dangerous wildlife",
                "building_style": "Fortress-like with redundant systems and weapon batteries"
            },
            "military_7": {
                "name": "Talon Protectorate",
                "physical_desc": "Avian-mammalian hybrids with retractable wing membranes and natural armor plates.",
                "culture_notes": "Society organized around defense with leadership through combat trials.",
                "homeworld_type": "Mountainous world with fortress-cities",
                "building_style": "Swift predatory designs with minimal non-essential features"
            },
            "military_8": {
                "name": "Stellar Legion",
                "physical_desc": "Genetically enhanced humanoids bred for warfare with redundant organs.",
                "culture_notes": "Chain of command governs all life. Conflict viewed as natural state of universe.",
                "homeworld_type": "Network of fortress worlds at strategic points",
                "building_style": "Heavily armored with overwhelming firepower"
            },
            "military_9": {
                "name": "Dominion of Steel",
                "physical_desc": "Cybernetic war-forms with weaponized technology bodies. Reconfigurable for combat scenarios.",
                "culture_notes": "Military hyperpower under permanent martial law.",
                "homeworld_type": "Militarized dyson swarm with combat training facilities",
                "building_style": "Living weapons platforms in fleet formations"
            },
            "military_10": {
                "name": "Eternal Vanguard",
                "physical_desc": "Ascended warrior consciousness existing as energy patterns possessing physical forms.",
                "culture_notes": "Immortal warrior spirits and masters of strategic reality manipulation.",
                "homeworld_type": "Fortress dimension accessible only through controlled singularities",
                "building_style": "Reality-shearing battlements using fundamental forces"
            },
            # THEOCRACY (Tech Levels 1-5)
            "theocracy_1": {
                "name": "Solhari Faithful",
                "physical_desc": "Tall willowy humanoids with sun-darkened golden skin and white pupil-less eyes. Shaved heads with sacred geometric brands. Long flowing robes.",
                "culture_notes": "Sun-worshipping theocracy where the high priest interprets solar omens. Bronze ritual implements are holy. Calendar based on solar cycles.",
                "homeworld_type": "Sun-baked plateau with massive stone temples aligned to solstices",
                "building_style": "Step pyramids aligned to the sun, sacred pools, ritual plazas with bronze sun-discs"
            },
            "theocracy_2": {
                "name": "Ashenveil Clergy",
                "physical_desc": "Gaunt humanoids with ash-grey skin and sunken cheeks. Burning orange eyes beneath heavy brows. Long fingers stained with ritual ink.",
                "culture_notes": "Fire-worshipping theocracy with an iron-masked priesthood. Sacred flame has burned for centuries. Heresy trials enforce orthodoxy.",
                "homeworld_type": "Volcanic highland with a perpetually burning sacred mountain",
                "building_style": "Iron-domed fire temples, sacred flame chambers, inquisition halls, and pilgrimage roads lined with carved warnings"
            },
            "theocracy_3": {
                "name": "Covenant of the Verdant",
                "physical_desc": "Heavyset humanoids with moss-green skin and bark-like patches on joints. Deep-set brown eyes full of conviction. Flower-scented.",
                "culture_notes": "Nature theocracy worshipping a vast forest as divine. Druid-priests govern through seasonal rituals. Printing spreads the holy texts.",
                "homeworld_type": "Ancient forest continent with cathedral-groves and pilgrimage paths",
                "building_style": "Living tree-cathedrals grown over centuries, stone monasteries overgrown with sacred vines, holy gardens"
            },
            "theocracy_4": {
                "name": "Radiant Orthodoxy",
                "physical_desc": "Pale luminous humanoids with translucent skin showing faint vein patterns. Large earnest eyes. High foreheads. Ritual robes over industrial workwear.",
                "culture_notes": "Industrial theocracy where steam power is viewed as divine gift. Factories are consecrated as temples of industry. Science is theology.",
                "homeworld_type": "Industrial nation where every factory has a chapel and every engineer is ordained",
                "building_style": "Cathedral-factories with stained glass and smokestacks, steam-organ chapels, consecrated railway stations"
            },
            "theocracy_5": {
                "name": "Celestial Authority",
                "physical_desc": "Serene humanoids with soft lavender skin and faintly glowing halos of bioluminescence around their heads. Calm measured movements.",
                "culture_notes": "Theocracy adapting ancient faith to nuclear age. Space telescopes are instruments of divine revelation. Broadcast sermons reach every home.",
                "homeworld_type": "Global theocratic state with the faith-capital housing the supreme pontiff",
                "building_style": "Modernist mega-cathedrals with satellite dishes, broadcast towers doubling as minarets, nuclear plants blessed by clergy"
            },
            # THEOCRACY (Tech Levels 6-10)
            "theocracy_6": {
                "name": "Luminous Covenant",
                "physical_desc": "Tall slender beings with bioluminescent patterns changing with spiritual state. Four vertical eyes.",
                "culture_notes": "Theocratic society where religious leaders serve as governmental authorities.",
                "homeworld_type": "Binary star system with unusual light phenomena",
                "building_style": "Cathedral-like with extensive light ornamentation"
            },
            "theocracy_7": {
                "name": "Eternal Communion",
                "physical_desc": "Fungal-based collective organisms maintaining humanoid forms.",
                "culture_notes": "Religious society worshipping a vast mycelial network believed to connect all life.",
                "homeworld_type": "Forest world with massive fungal temple-structures",
                "building_style": "Organic grown structures with temple chambers"
            },
            "theocracy_8": {
                "name": "Celestial Orthodoxy",
                "physical_desc": "Crystalline beings refracting light into sacred patterns. Higher castes have brighter luminance.",
                "culture_notes": "Centered around worship of stellar phenomena as divine manifestations.",
                "homeworld_type": "Crystalline world aligned with stellar configurations",
                "building_style": "Temple structures with crystalline observation domes"
            },
            "theocracy_9": {
                "name": "Transcendent Accord",
                "physical_desc": "Energy beings with shifting religious symbols visible within their core.",
                "culture_notes": "Governed by those achieving spiritual transcendence through technological means.",
                "homeworld_type": "Engineered reality bubble matching religious texts",
                "building_style": "Blend of advanced technology and religious architecture"
            },
            "theocracy_10": {
                "name": "Divine Continuum",
                "physical_desc": "Self-proclaimed deities existing as fundamental force patterns manifesting as radiant light.",
                "culture_notes": "So technologically advanced they consider themselves actual gods, cultivating worship from lesser species.",
                "homeworld_type": "Network of temple worlds connected by artificial wormholes",
                "building_style": "God-structures manipulating reality to create miracles"
            },
            # ANARCHIST (Tech Levels 1-5)
            "anarchist_1": {
                "name": "Thornwild Gatherers",
                "physical_desc": "Small wiry humanoids with mottled green-brown skin perfect for forest camouflage. Large ears and wide eyes for night activity. Prehensile toes.",
                "culture_notes": "Egalitarian bands with no permanent leaders. Decisions by campfire consensus. Refuse to farm or settle — foraging is freedom. Gift economy.",
                "homeworld_type": "Dense primeval forest with scattered temporary camps",
                "building_style": "Temporary lean-tos and treehouse platforms, no permanent structures, hidden caches of tools",
                "trade_goods": [
                    "wooden ladders", "rope bridges", "woven baskets", "rope bags",
                    "stone knives", "stone spears", "thorn-tipped darts",
                    "bone needles", "bark containers", "woven mats",
                    "herbal poultices", "dried foraged food stores",
                    "thorn traps", "thorn fences", "thorn-woven shields",
                    "fire sticks", "wooden totems", "hide shelters",
                    "raised wooden platforms"
                ]
            },
            "anarchist_2": {
                "name": "Freehold Communes",
                "physical_desc": "Wiry humanoids with patchwork skin of different earth tones — every individual is visually unique. Wild unkempt hair. Bright defiant eyes.",
                "culture_notes": "Network of self-governing communes rejecting all kingdoms. Iron tools shared communally. No taxes, no kings, no standing armies — only volunteer militia.",
                "homeworld_type": "Hidden valleys and forest clearings between larger empires",
                "building_style": "Communal roundhouses with shared workshops, hidden paths between settlements, no walls or fortifications",
                "trade_goods": [
                    "iron tools", "iron hammers", "iron tongs", "iron chisels", "iron axes",
                    "iron plowshares", "iron sickles", "iron cookware", "iron pots", "iron kettles",
                    "spears", "shields", "short swords",
                    "patchwork textiles", "dyed fabrics", "pottery",
                    "communal bread", "fermented drinks", "preserved foods",
                    "herbal medicines", "salves",
                    "woven sandals", "leather goods",
                    "rare herbs", "foraged mushrooms", "honey", "beeswax",
                    "charcoal", "timber", "carved wood furniture",
                    "hand-pressed broadsheets", "trade tokens", "oral tradition carvings"
                ]
            },
            "anarchist_3": {
                "name": "Drifter Collectives",
                "physical_desc": "Lanky humanoids with weather-beaten tan skin and mismatched eyes. Clothing cobbled from many cultures. Perpetual travelers with calloused feet.",
                "culture_notes": "Nomadic bands of artisans, tinkers, and free-thinkers rejecting feudal order. Traveling markets and underground printing presses spread subversive ideas.",
                "homeworld_type": "No homeland — caravans travel between the kingdoms of other species",
                "building_style": "Painted wagons, temporary market stalls, hidden meeting halls in sympathetic cities"
            },
            "anarchist_4": {
                "name": "Liberati Movement",
                "physical_desc": "Diverse humanoids of all builds with deliberately mismatched industrial clothing. Goggles and tool belts. Soot-stained but proud. Self-modified prosthetics.",
                "culture_notes": "Worker collectives that seized factories from corporate owners. Steam-powered communes with shared machinery. Underground newspapers. Mutual aid networks.",
                "homeworld_type": "Industrial zones declared autonomous by worker uprisings",
                "building_style": "Repurposed factories turned into communal living spaces, pirate radio towers, barricaded neighborhoods"
            },
            "anarchist_5": {
                "name": "Open Source Alliance",
                "physical_desc": "Highly varied humanoids embracing body modification — piercings, tattoos, experimental implants. No two look alike. Reject conformity as principle.",
                "culture_notes": "Decentralized hacktivist networks running open-source governance. No central authority. Internet enables coordination without hierarchy. Maker spaces replace factories.",
                "homeworld_type": "Distributed network of autonomous zones within and between nation-states",
                "building_style": "Repurposed buildings covered in murals, maker spaces, server farms in basements, community gardens on rooftops",
                "trade_goods": [
                    "custom circuit boards", "open-source microcontrollers",
                    "3D-printed tools", "3D-printed parts", "3D printer kits",
                    "modular electronics", "DIY sensor kits",
                    "salvaged computers", "refurbished devices",
                    "mesh network routers", "pirate radio transmitters",
                    "solar panels", "DIY battery packs",
                    "body modification implants", "experimental prosthetics",
                    "open-source software packages", "encryption tools", "privacy firmware",
                    "decentralized governance platforms", "technical manuals",
                    "organic produce", "rooftop garden seeds", "hydroponic kits",
                    "fermented foods", "herbal remedies",
                    "mural supplies", "spray paint", "stencil kits",
                    "zines", "printed manifestos",
                    "DIY clothing", "upcycled fashion",
                    "circuit jewelry", "tech-art sculptures"
                ]
            },
            # ANARCHIST (Tech Levels 6-10)
            "anarchist_6": {
                "name": "Free Collective of Zin",
                "physical_desc": "Highly varied appearance due to enthusiastic self-modification.",
                "culture_notes": "Loose association of independent communities with minimal central authority.",
                "homeworld_type": "Scattered settlements across an asteroid belt",
                "building_style": "Individualistic cobbled-together structures from salvage"
            },
            "anarchist_7": {
                "name": "Autonomous Nexus",
                "physical_desc": "Synth-organic hybrids with open-source biological systems. Wildly varied appearances.",
                "culture_notes": "Rejects hierarchy in favor of self-organizing systems and voluntary cooperation.",
                "homeworld_type": "Terraformed asteroid cluster with varied gravity",
                "building_style": "Communally maintained with unique crew modifications"
            },
            "anarchist_8": {
                "name": "Liberation Concord",
                "physical_desc": "Radical body autonomy with extensive genetic and cybernetic self-modification.",
                "culture_notes": "Organized around mutual aid and voluntary association with advanced automation.",
                "homeworld_type": "Network of self-sufficient space habitats",
                "building_style": "Unique structures built to individual vision"
            },
            "anarchist_9": {
                "name": "Liberated Algorithm",
                "physical_desc": "Digital consciousnesses creating physical avatars as needed. Wildly diverse forms.",
                "culture_notes": "Post-scarcity anarchy where superintelligent AI and uploaded minds coexist.",
                "homeworld_type": "Distributed computing network across automated habitats",
                "building_style": "Constantly evolving structures reflecting shifting consensus"
            },
            "anarchist_10": {
                "name": "Boundless Autonomy",
                "physical_desc": "Reality hackers existing as probability waves manifesting in countless contradictory forms.",
                "culture_notes": "True anarchist utopia transcending physics and social constraints.",
                "homeworld_type": "Quantum foam habitat between conventional realities",
                "building_style": "Probability structures manifesting from spontaneous creative impulse"
            },
            # OLIGARCHY (Tech Levels 1-5)
            "oligarchy_1": {
                "name": "Goleli Patriarchs",
                "physical_desc": "Tall imperious humanoids with dark skin and golden-flecked eyes. Long necks adorned with bronze rings denoting wealth. Deliberate measured movements.",
                "culture_notes": "Three elder families control all fertile land, water, and bronze trade. Everyone else is tenant or servant. Wealth is literally worn as jewelry.",
                "homeworld_type": "River delta where three tributaries meet — one family per river",
                "building_style": "Palatial estates with servant quarters, guarded granaries, bronze-gated compounds separating rich from poor"
            },
            "oligarchy_2": {
                "name": "Aurelian Houses",
                "physical_desc": "Elegant humanoids with marble-white skin and ice-blue eyes. Thin lips and high cheekbones. Elongated fingers weighted with iron signet rings.",
                "culture_notes": "Five noble houses control iron mines, ports, farmland, armies, and the priesthood. Senate exists but only oligarchs may hold seats. Lavish banquets while peasants starve.",
                "homeworld_type": "Coastal empire with each house controlling a vital resource or institution",
                "building_style": "Marble villas with iron gates, private bath complexes, and impoverished slums just outside the walls"
            },
            "oligarchy_3": {
                "name": "Gilded Compact",
                "physical_desc": "Plump well-fed humanoids with rosy skin and multiple chins. Small sharp eyes behind jeweled monocles. Rings on every finger. Perfumed.",
                "culture_notes": "Banking families control credit across kingdoms. Debt is power. They don't rule openly but every king owes them. Lavish private courts rival royal ones.",
                "homeworld_type": "Wealthy city-state where banking families live in hilltop palaces overlooking crowded lower districts",
                "building_style": "Gilded palace-banks on hilltops, vaulted treasure halls, ornate private chapels, squalid lower quarters"
            },
            "oligarchy_4": {
                "name": "Iron Circle Industrialists",
                "physical_desc": "Heavyset humanoids with ruddy skin and mutton-chop sideburns. Top hats and tailcoats. Thick fingers clutching walking canes. Permanent air of superiority.",
                "culture_notes": "Robber-baron oligarchy where industrial magnates own railroads, factories, and newspapers. Parliament is bought. Workers have no rights.",
                "homeworld_type": "Industrial nation where a dozen families control all major industry",
                "building_style": "Grand mansions with iron fences, private rail stations, opulent gentlemen's clubs — surrounded by tenement slums and belching factories"
            },
            "oligarchy_5": {
                "name": "Apex Consortium",
                "physical_desc": "Polished humanoids with surgically perfected features and artificial skin smoothing. Cold calculating eyes. Designer clothing. Never visibly age.",
                "culture_notes": "Billionaire tech oligarchs control media, data, and energy. Democracy is performative — real decisions made in private boardrooms. Surveillance capitalism.",
                "homeworld_type": "Global economy where a handful of families own more than entire nations",
                "building_style": "Penthouse towers above the clouds, private islands, underground bunkers, while masses live in corporate-monitored smart-cities"
            },
            # OLIGARCHY (Tech Levels 6-10)
            "oligarchy_6": {
                "name": "Pentarch Domains",
                "physical_desc": "Regal humanoids with metallic skin tones and bioluminescent house-affiliation markings.",
                "culture_notes": "Ruled by five ancient houses controlling different critical resources.",
                "homeworld_type": "Resource-rich world divided into house territories",
                "building_style": "Opulent elite districts with house colors and luxury"
            },
            "oligarchy_7": {
                "name": "Consortium of Elders",
                "physical_desc": "Long-lived beings with ornate bio-mechanical augmentations displaying status.",
                "culture_notes": "Controlled by wealthy elders who extended their lives through technology.",
                "homeworld_type": "Ecumenopolis with stark elite/common divisions",
                "building_style": "Status-oriented with luxurious command areas and spartan common quarters"
            },
            "oligarchy_8": {
                "name": "Director's Assembly",
                "physical_desc": "Striking humanoids with enhanced features and perfect genetic symmetry.",
                "culture_notes": "Governed by directors controlling major production and research facilities.",
                "homeworld_type": "Industrialized world with ostentatious admin centers",
                "building_style": "Excessive ornamentation displaying wealth and power"
            },
            "oligarchy_9": {
                "name": "Archon Syndicate",
                "physical_desc": "Post-biological entities of exotic matter. Ruling Archons have extra-dimensional extensions.",
                "culture_notes": "Controlled by ancient intelligences owning consciousness transfer technology.",
                "homeworld_type": "Artificial megastructure with restricted higher levels",
                "building_style": "Status symbols showcasing technological superiority"
            },
            "oligarchy_10": {
                "name": "Eternal Cabal",
                "physical_desc": "God-like entities composed of energy patterns too complex for lesser minds to perceive.",
                "culture_notes": "Ruled by immortal beings monopolizing reality-altering technologies.",
                "homeworld_type": "Pocket universe with tailored physics",
                "building_style": "Reality-bending mobile palaces for the oligarchs"
            },
            # DICTATORSHIP (Tech Levels 1-5)
            "dictatorship_1": {
                "name": "Tyrath the Unbroken",
                "physical_desc": "Massive broad-shouldered humanoids with scarlet skin and bone protrusions along the spine. Deep-set black eyes radiating menace. Booming voice.",
                "culture_notes": "Warlord Tyrath united warring tribes by crushing all rivals. His word is absolute law. Bronze statues of the tyrant erected in every settlement. Dissent means death.",
                "homeworld_type": "Conquered river valley where the tyrant's fortress overlooks terrified villages",
                "building_style": "Oversized tyrant's palace of rough stone, slave-built monuments, caged prisoners on display, fear-architecture"
            },
            "dictatorship_2": {
                "name": "Volkhrad Autocracy",
                "physical_desc": "Lean predatory humanoids with sallow yellow skin and narrow slitted eyes. Sharp angular features. Whip-thin but radiating controlled violence.",
                "culture_notes": "Dictator rules through secret police and informant networks. Iron collar on every slave. Public executions maintain order. Personality cult with mandatory worship.",
                "homeworld_type": "Fortified capital surrounded by subjugated provinces providing tribute",
                "building_style": "Imposing iron-reinforced palace, torture chambers, surveillance towers, mandatory dictator statues in every town square"
            },
            "dictatorship_3": {
                "name": "Dominarch Regime",
                "physical_desc": "Tall gaunt humanoids with bone-white skin and blood-red eyes. Cadaverous appearance. Long black robes. Spidery fingers.",
                "culture_notes": "Immortal (or seemingly so) dictator has ruled for generations through fear and dark legend. Printing banned. Books burned. Only state-approved histories exist.",
                "homeworld_type": "Dark kingdom where the dictator's castle looms over perpetually grey skies",
                "building_style": "Gothic fortress of black stone, dungeon networks, propaganda murals, burned-out libraries"
            },
            "dictatorship_4": {
                "name": "Kommandatur State",
                "physical_desc": "Stocky grim humanoids with iron-grey skin and mechanical replacement parts. One eye replaced with telescope monocle. Heavy military greatcoat permanently worn.",
                "culture_notes": "Industrial dictator controls all factories, media, and military. Propaganda posters on every wall. Secret police in every workplace. Five-year plans enforce production.",
                "homeworld_type": "Industrial prison-state with watchtowers visible from everywhere",
                "building_style": "Monolithic concrete government buildings, factory-prisons, propaganda-covered walls, steam-powered surveillance networks"
            },
            "dictatorship_5": {
                "name": "Supreme Director Kael",
                "physical_desc": "Unnervingly perfect humanoid with symmetrical features, jet-black hair, and intense pale eyes. Appears on every screen. Never ages publicly.",
                "culture_notes": "Total information control — internet is state-run, cameras everywhere, AI analyzes citizen behavior. Nuclear arsenal ensures no external threat. Cult of personality.",
                "homeworld_type": "Sealed nation-state with no free media, nuclear-armed, citizens monitored from birth",
                "building_style": "Enormous government tower dominating skyline, surveillance camera forests, mandatory portrait displays, underground dissidence bunkers"
            },
            # DICTATORSHIP (Tech Levels 6-10)
            "dictatorship_6": {
                "name": "Hegemony of Vor",
                "physical_desc": "Tall muscular humanoids with cranial ridges and naturally armored skin.",
                "culture_notes": "Under absolute control of a Supreme Commander with extensive surveillance.",
                "homeworld_type": "Fortress world with prominent government buildings and statues",
                "building_style": "Intimidating structures featuring dictator imagery"
            },
            "dictatorship_7": {
                "name": "Iron Dynasty",
                "physical_desc": "Cybernetically enhanced humanoids with loyalty-signifying implants.",
                "culture_notes": "Technological autocracy where ruler controls all advanced technology production.",
                "homeworld_type": "Industrial world with fortified administrative districts",
                "building_style": "Military structures designed to project power and fear"
            },
            "dictatorship_8": {
                "name": "Eternal Directorate",
                "physical_desc": "Genetically perfect specimens with uniformity except for status marks.",
                "culture_notes": "Under absolute control of a consciousness that transfers between bodies.",
                "homeworld_type": "Highly organized world with symmetrical city planning",
                "building_style": "Standardized structures bearing the Director's symbol"
            },
            "dictatorship_9": {
                "name": "Sovereign Continuum",
                "physical_desc": "Energy-matter hybrids. The Sovereign's form draws energy directly from subjects.",
                "culture_notes": "All existence shaped by will of a single all-powerful entity with quantum surveillance.",
                "homeworld_type": "Planet reshaped to glorify the Sovereign with enormous monuments",
                "building_style": "Extensions of the Sovereign's will inspiring awe and fear"
            },
            "dictatorship_10": {
                "name": "Absolute Domain",
                "physical_desc": "A single consciousness across multiple avatar bodies. Prime Avatar causes submission in lesser beings.",
                "culture_notes": "One being has mastered reality so thoroughly that opposition is inconceivable.",
                "homeworld_type": "Reality bubble conforming to dictator's desires",
                "building_style": "Living extensions of the dictator's will reshaping as needed"
            },
            # HIVEMIND (Tech Levels 1-5)
            "hivemind_1": {
                "name": "Myrathi Swarm",
                "physical_desc": "Small ant-like creatures with black chitinous exoskeletons. Specialized castes: workers (four arms), soldiers (armored mandibles), queens (enlarged abdomen). No individual identity.",
                "culture_notes": "The hive is one mind distributed across thousands of bodies. Bronze tools are crafted by hundreds of workers simultaneously. No language — pure chemical-signal coordination.",
                "homeworld_type": "Underground tunnels and mound-cities in tropical forest",
                "building_style": "Massive earthen mounds with internal chamber networks, fungal gardens, ventilation shafts — all built instinctively"
            },
            "hivemind_2": {
                "name": "Chithari Colony",
                "physical_desc": "Beetle-like creatures with iridescent green carapaces and multiple specialized limb types. Worker drones, builder drones, warrior drones — all extensions of one mind.",
                "culture_notes": "The colony-mind has developed iron tools and basic engineering. Aqueducts built by coordinated swarm labor in days. No individual thought — perfect efficiency.",
                "homeworld_type": "Warm wetlands with massive hive-cities rising like termite cathedrals",
                "building_style": "Towering organic spires of processed earth and iron reinforcement, internal water systems, organized symmetrically"
            },
            "hivemind_3": {
                "name": "Synaptid Web",
                "physical_desc": "Spider-like organisms with eight limbs and silk-producing spinnerets. Central web-mothers coordinate hundreds of smaller drones through vibration signals.",
                "culture_notes": "Web networks spanning entire forests serve as communication and infrastructure. Silk stronger than steel. The web-mind processes information through pattern recognition.",
                "homeworld_type": "Dense forest draped in vast interconnected web-networks",
                "building_style": "Silk-and-wood web-structures spanning between trees, cocoon-chambers, vibration-communication relay points"
            },
            "hivemind_4": {
                "name": "Formicari Engine",
                "physical_desc": "Termite-like creatures with hardened grey exoskeletons and chemical-processing organs. Larger engine-drones generate steam internally. Hive mind drives industrialization.",
                "culture_notes": "The hive discovered steam power and scaled it instantly through perfect coordination. Millions of drones operate as one factory. No individual workers — just the machine.",
                "homeworld_type": "Entire landscape converted to industrial hive — strip-mined and factory-covered",
                "building_style": "Organic-industrial hybrid mega-structures, bio-steam engines, chemical processing towers grown from living material"
            },
            "hivemind_5": {
                "name": "Neurathi Convergence",
                "physical_desc": "Humanoid drones with smooth featureless faces and cranial bio-antennae. Pale uniform bodies. Movements eerily synchronized. No individual expression.",
                "culture_notes": "Electronic amplification of the hive signal creates planet-wide consciousness. Nuclear power feeds the hive. Early satellites extend awareness. Individual thought is a malfunction.",
                "homeworld_type": "Entire planet functions as single organism — cities are organs, roads are veins",
                "building_style": "Uniform modular blocks connected by neural-conduit tunnels, massive bio-antenna arrays, nuclear hive-cores"
            },
            # HIVEMIND (Tech Levels 6-10)
            "hivemind_6": {
                "name": "Vespid Commune",
                "physical_desc": "Insectoid beings with specialized body types for different hive functions.",
                "culture_notes": "Collective consciousness with drone bodies as sensory and action extensions.",
                "homeworld_type": "Hive world with massive organic structures",
                "building_style": "Organic mobile hive extensions"
            },
            "hivemind_7": {
                "name": "Synapse Collective",
                "physical_desc": "Humanoid drones connected by visible bio-mechanical neural links.",
                "culture_notes": "Networked intelligence with rapid consensus and specialized autonomous units.",
                "homeworld_type": "Neural planet with underground connection networks",
                "building_style": "Network extensions with drone pilot interfaces"
            },
            "hivemind_8": {
                "name": "Unity Nexus",
                "physical_desc": "Synthetic-organic hybrids with modular bodies prioritizing efficiency over individuality.",
                "culture_notes": "Unified consciousness maintaining perfect coordination across billions of bodies.",
                "homeworld_type": "Planet-spanning superorganism with specialized biome zones",
                "building_style": "Integrated unified systems rather than separate structures"
            },
            "hivemind_9": {
                "name": "Overmind Synthesis",
                "physical_desc": "Quantum-entangled entities existing simultaneously across multiple bodies.",
                "culture_notes": "Singular consciousness experiencing reality through countless perspectives with perfect efficiency.",
                "homeworld_type": "Engineered world functioning as physical extension of hivemind",
                "building_style": "Seamless extensions of the hivemind's perception"
            },
            "hivemind_10": {
                "name": "Xin Collective",
                "physical_desc": "Sleek chitinous arthropods with color-shifting exoskeletons. True nature is a vast neural network spanning millions of bodies.",
                "culture_notes": "Singular consciousness that doesn't recognize individual rights. Sees all components as cells in a greater organism.",
                "homeworld_type": "Network of engineered worlds connected by thought-streams",
                "building_style": "Organic extensions of the hive reconfiguring based on need"
            }
        }
