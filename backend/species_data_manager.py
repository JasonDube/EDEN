"""
EDEN Species & Civilization Data Manager
Adapted from spacegame430 species_data_manager.py

Provides:
- 13 government types with behavioral tendencies
- 5 tech tiers (6-10, interstellar scale) + 6 pre-spacefaring tiers (0-5)
- 65 unique species (government × tech level combinations)
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

    # ── Species Database (65 species from spacegame430) ──

    def initialize_species_data(self):
        self.species_data = {
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
