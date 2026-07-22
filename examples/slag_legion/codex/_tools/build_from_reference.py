#!/usr/bin/env python3
"""Build Codex entries from existing source material.

Extracts the structured sections of docs/EDEN_Reference.md (biomes, governments,
tech levels, resources, fauna) into one Markdown-with-frontmatter entry per item,
and stubs a faction entry per portrait in assets/species/.

SAFETY: only writes files that don't exist or that carry `generated: true` in
their frontmatter. Hand-authored entries are never clobbered. Re-run any time the
reference guide changes:  python3 codex/_tools/build_from_reference.py
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
CODEX = os.path.dirname(HERE)                                   # .../codex
SLAG = os.path.dirname(CODEX)                                   # .../examples/slag_legion
REPO = os.path.dirname(os.path.dirname(SLAG))                   # repo root
REFERENCE = os.path.join(REPO, "docs", "EDEN_Reference.md")
PORTRAITS = os.path.join(SLAG, "assets", "species")


def slugify(s):
    s = s.lower().strip()
    s = re.sub(r"[^\w\s-]", "", s)
    s = re.sub(r"[\s_]+", "-", s)
    return re.sub(r"-+", "-", s).strip("-")


def titleize(stem):
    return " ".join(w.capitalize() for w in re.split(r"[_\s-]+", stem))


def yaml_list(items):
    return "[" + ", ".join(items) + "]" if items else "[]"


def write_entry(folder, slug, frontmatter, body):
    """Write only if new or previously generated. Returns 'wrote'|'skipped'|'kept'."""
    path = os.path.join(CODEX, folder, slug + ".md")
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            head = f.read(600)
        if "generated: true" not in head:
            return "kept"          # hand-authored — never touch
    fm = "---\n" + "".join(f"{k}: {v}\n" for k, v in frontmatter.items()) + "---\n"
    with open(path, "w", encoding="utf-8") as f:
        f.write(fm + "\n" + body.rstrip() + "\n")
    return "wrote"


# ---- bullet-style section parser (biomes / governments / tech-levels) -------

def parse_bullets(block):
    """Return (fields dict, prose list) from an entry's body lines.
    A bullet may hold several **Key:** value pairs split by ' | '. Bullets with
    no **Key:** are prose. Non-bullet lines (images, prompt quotes) are dropped."""
    fields, prose = {}, []
    for line in block:
        s = line.strip()
        if not s.startswith("- "):
            continue
        s = s[2:]
        if "**" in s and ":**" in s:
            for part in s.split(" | "):
                m = re.match(r"\*\*(.+?):\*\*\s*(.*)", part.strip())
                if m:
                    fields[m.group(1).strip()] = m.group(2).strip()
        else:
            prose.append(s)
    return fields, prose


def extract_sections(text):
    """Yield (top_section, subsection, heading_level, name, block_lines)."""
    lines = text.splitlines()
    top = sub = None
    cur = None  # (level, name, [lines])
    for line in lines:
        if line.startswith("# "):
            if cur: yield (top, sub, *cur); cur = None
            top = line[2:].strip(); sub = None
        elif line.startswith("### "):
            if cur: yield (top, sub, *cur); cur = None
            cur = (3, line[4:].strip(), [])
        elif line.startswith("## "):
            if cur: yield (top, sub, *cur); cur = None
            name = line[3:].strip()
            # a "## X (N items)" grouping header vs a real level-2 entry: treat as
            # subsection when the active section extracts level-3 entries.
            sub = name
            cur = (2, name, [])
        elif cur:
            cur[2].append(line)
    if cur: yield (top, sub, *cur)


# category config: top-section name -> (folder, category, entry-heading-level)
BULLET_SECTIONS = {
    "BIOMES":      ("biomes",      "biome",      2),
    "GOVERNMENTS": ("governments", "government", 2),
    "TECH LEVELS": ("tech-levels", "tech-level", 2),
    "RESOURCES":   ("trade-goods", "trade-good", 3),
}

# which bullet **Keys** to promote into frontmatter, per category (rest stay prose-ish)
FIELD_KEYS = {
    "biome":      ["Temperature", "Vegetation", "Water", "Dominant resources",
                   "Base habitability", "Terrain"],
    "government": ["Color", "Tendencies", "Building style"],
    "tech-level": ["Power source", "Capabilities", "Buildings available",
                   "Harvestable resources", "NEW resources unlocked",
                   "Population range", "Starting credits"],
    "trade-good": ["Tier", "Category", "Extraction", "Storage", "Power", "Staff",
                   "Biome affinity"],
    "species": ["Physical", "Culture", "Homeworld", "Building style"],
}


def portrait_for(slug):
    """Return the portrait filename if one matches this slug, else None."""
    for ext in (".jpg", ".jpeg", ".png"):
        if os.path.exists(os.path.join(PORTRAITS, slug.replace("-", "_") + ext)):
            return slug.replace("-", "_") + ext
    return None


def run_bullets(text):
    counts = {}
    for top, sub, level, name, block in extract_sections(text):
        if top not in BULLET_SECTIONS:
            continue
        folder, category, want_level = BULLET_SECTIONS[top]
        if level != want_level:
            continue
        fields, prose = parse_bullets(block)
        slug = slugify(name)
        # resource class comes from the "Category" bullet (water/mineral/...), not
        # the "(N resources)" grouping header.
        extra_tag = slugify(fields["Category"]) if category == "trade-good" and "Category" in fields else None
        fm = {
            "name": name,
            "category": category,
            "tags": yaml_list([slugify(category)] + ([extra_tag] if extra_tag else [])),
            "status": "canon",
            "generated": "true",
            "related": "[]",
        }
        for k in FIELD_KEYS.get(category, []):
            if k in fields:
                key = k.lower().replace(" ", "_")
                if key == "category":          # avoid clobbering the entry's category
                    key = "resource_class"
                val = fields[k]
                fm[key] = f'"{val}"' if ("," in val or ":" in val) else val
        body = f"# {name}\n\n"
        if prose:
            body += "\n".join(prose) + "\n\n"
        if fields:
            body += "## Attributes\n\n" + "\n".join(f"- **{k}:** {v}" for k, v in fields.items()) + "\n"
        res = write_entry(folder, slug, fm, body)
        counts[res] = counts.get(res, 0) + 1
    return counts


# ---- species (rich entries, grouped by government) --------------------------

def run_species(text):
    """Extract the SPECIES section. Returns (counts, set-of-slugs) so factions can
    skip portraits that are actually species."""
    counts, slugs = {}, set()
    for top, sub, level, name, block in extract_sections(text):
        if top != "SPECIES" or level != 3:
            continue
        # heading: "Edathi — democracy_1 (Ancient / Bronze Age)"
        head = name.replace("—", "-")
        disp = head.split(" - ")[0].strip()
        era = None
        m = re.search(r"\((.*?)\)", head)
        if m: era = m.group(1).strip()
        ref_id = None
        rest = head.split(" - ", 1)[1] if " - " in head else ""
        if rest:
            ref_id = rest.split("(")[0].strip()
        government = re.sub(r"\s*Species.*$", "", sub or "").strip()
        slug = slugify(disp)
        slugs.add(slug)
        fields, prose = parse_bullets(block)
        img = portrait_for(slug)
        fm = {
            "name": disp, "category": "species",
            "tags": yaml_list(["species"] + ([slugify(government)] if government else [])),
            "status": "canon", "generated": "true",
            "related": yaml_list([slugify(government)] if government else []),
        }
        if government: fm["government"] = government
        if era: fm["era"] = f'"{era}"'
        if ref_id: fm["ref_id"] = ref_id
        if "Homeworld" in fields: fm["homeworld"] = f'"{fields["Homeworld"]}"'
        if img: fm["image"] = f"../../assets/species/{img}"
        body = f"# {disp}\n\n"
        if img: body += f"![{disp}](../../assets/species/{img})\n\n"
        for key in ("Physical", "Culture", "Homeworld", "Building style"):
            if key in fields:
                body += f"**{key}.** {fields[key]}\n\n"
        if government:
            body += f"Government archetype: [{government}](../governments/{slugify(government)}.md)"
            if era: body += f" · Era: {era}"
            body += "\n"
        res = write_entry("species", slug, fm, body)
        counts[res] = counts.get(res, 0) + 1
    return counts, slugs


# ---- fauna tables -----------------------------------------------------------

def run_fauna(text):
    counts = {}
    for top, sub, level, name, block in extract_sections(text):
        if top != "FAUNA" or level != 2:
            continue
        if "Weight" in name:        # "Biome Fauna Weights" is a lookup table, not fauna
            continue
        habitat = re.sub(r"\s*\(.*\)\s*$", "", name)   # "Terrestrial Fauna"
        rows = [l for l in block if l.strip().startswith("|")]
        if len(rows) < 3:
            continue
        for row in rows[2:]:                            # skip header + separator
            cells = [c.strip() for c in row.strip().strip("|").split("|")]
            if len(cells) < 5 or not cells[0]:
                continue
            fname, desc, minhab, resource, domest = cells[:5]
            slug = slugify(fname)
            fm = {
                "name": fname, "category": "fauna",
                "tags": yaml_list(["fauna", slugify(habitat.replace(" Fauna", ""))]),
                "status": "canon", "generated": "true", "related": "[]",
                "habitat": f'"{habitat}"',
                "min_habitability": minhab, "resource": resource,
                "domesticable": ("true" if domest.lower().startswith("y") else "false"),
            }
            body = f"# {fname}\n\n{desc}\n\n- **Habitat:** {habitat}\n- **Yields:** {resource}\n- **Min habitability:** {minhab}\n- **Domesticable:** {domest}\n"
            res = write_entry("fauna", slug, fm, body)
            counts[res] = counts.get(res, 0) + 1
    return counts


# ---- faction stubs from portraits ------------------------------------------

def run_factions(exclude=frozenset()):
    counts = {}
    if not os.path.isdir(PORTRAITS):
        return counts
    names = []
    for fn in sorted(os.listdir(PORTRAITS)):
        if fn.startswith("."):               # nameless dotfile like ".jpeg" — skip
            print(f"  skipping unnamed portrait: {fn!r} (rename it to catalogue it)")
            continue
        if not fn.lower().endswith((".jpg", ".jpeg", ".png")):
            continue
        stem = os.path.splitext(fn)[0]
        name = titleize(stem)
        slug = slugify(stem)
        if slug in exclude:          # this portrait is a species, not a faction
            continue
        names.append((name, slug, fn))
        fm = {
            "name": name, "category": "faction",
            "tags": yaml_list(["faction", "portrait"]),
            "status": "stub", "generated": "true", "related": "[]",
            "image": f"../../assets/species/{fn}",
        }
        body = (f"# {name}\n\n"
                f"![{name}](../../assets/species/{fn})\n\n"
                f"> **STUB.** Portrait on file; lore not yet written. Replace this "
                f"line with disposition, government, homeworld, history, and links "
                f"to related [worlds](../worlds/), [species](../species/), and "
                f"[factions](./). Set `status: draft` once you begin, `canon` when "
                f"established.\n")
        res = write_entry("factions", slug, fm, body)
        counts[res] = counts.get(res, 0) + 1

    # gallery index (always regenerated)
    idx = ["---", "name: Factions Index", "category: design",
           "tags: [index, gallery, faction]", "status: canon",
           "generated: true", "related: []", "---", "",
           f"# Factions — {len(names)} on file", ""]
    if names:
        idx += ["The named powers of the galaxy not already covered as a single "
                "[species](../species/). Portraits in `../assets/species/`.", ""]
        for name, slug, fn in names:
            idx.append(f"- [{name}]({slug}.md)")
    else:
        idx += ["Every portrait in `../assets/species/` turned out to map 1:1 to a "
                "**[species](../species/)** entry (the higher-government peoples "
                "carry grand names like *Morvaine Dynasty*), so they live there with "
                "their art. This folder is reserved for future factions that are a "
                "*polity of multiple species* rather than a people — e.g. an "
                "alliance, a corporation, a pirate coalition. Add them by hand.", ""]
    with open(os.path.join(CODEX, "factions", "_index.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(idx) + "\n")
    counts["index"] = 1
    return counts


def main():
    if not os.path.exists(REFERENCE):
        print(f"reference not found: {REFERENCE}", file=sys.stderr)
        sys.exit(1)
    with open(REFERENCE, encoding="utf-8") as f:
        text = f.read()
    total = {}
    species_counts, species_slugs = run_species(text)
    steps = [("reference tables", lambda: run_bullets(text)),
             ("species", lambda: species_counts),
             ("fauna", lambda: run_fauna(text)),
             ("factions", lambda: run_factions(exclude=species_slugs))]
    for label, fn in steps:
        c = fn()
        print(f"{label}: " + ", ".join(f"{k}={v}" for k, v in sorted(c.items())))
        for k, v in c.items():
            total[k] = total.get(k, 0) + v
    print("TOTAL: " + ", ".join(f"{k}={v}" for k, v in sorted(total.items())))
    print("(wrote=created/updated generated, kept=hand-authored left alone)")


if __name__ == "__main__":
    main()
