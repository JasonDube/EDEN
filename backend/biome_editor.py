#!/usr/bin/env python3
"""
EDEN Biome Editor — Pygame GUI tool for editing biome data, reference images,
species classification, and texture palettes.

Usage: python biome_editor.py
"""

import pygame
import json
import os
import sys
import subprocess
import colorsys
import math
import shutil
from pathlib import Path

# ── Paths ──
BACKEND_DIR = Path(__file__).parent
BIOME_DATA_FILE = BACKEND_DIR / "biome_editor_data.json"
BIOME_MAPPINGS_FILE = BACKEND_DIR / "biome_mappings.json"
BIOME_IMAGES_DIR = BACKEND_DIR / "biome_images"
TEXTURE_DIR = BACKEND_DIR.parent / "examples" / "terrain_editor" / "assets" / "textures"

# ── Constants ──
WIN_W, WIN_H = 1280, 650
SIDEBAR_W = 240
TAB_BAR_H = 40
STATUS_BAR_H = 28
CONTENT_X = SIDEBAR_W
CONTENT_Y = TAB_BAR_H
CONTENT_W = WIN_W - SIDEBAR_W
CONTENT_H = WIN_H - TAB_BAR_H - STATUS_BAR_H
FPS = 30

# ── Colors ──
C_BG = (30, 30, 36)
C_SIDEBAR = (38, 38, 46)
C_TAB_BG = (24, 24, 30)
C_TAB_ACTIVE = (60, 60, 80)
C_TAB_HOVER = (50, 50, 65)
C_TEXT = (220, 220, 230)
C_TEXT_DIM = (140, 140, 155)
C_TEXT_BRIGHT = (255, 255, 255)
C_ACCENT = (80, 140, 255)
C_ACCENT_DIM = (50, 90, 170)
C_INPUT_BG = (50, 50, 60)
C_INPUT_ACTIVE = (60, 65, 80)
C_BUTTON = (60, 120, 200)
C_BUTTON_HOVER = (80, 140, 230)
C_BUTTON_DANGER = (180, 50, 50)
C_SLIDER_BG = (55, 55, 65)
C_SLIDER_FILL = (80, 140, 255)
C_SELECTED = (55, 55, 75)
C_HOVER = (45, 45, 58)
C_STATUS = (22, 22, 28)
C_GREEN = (60, 180, 80)
C_ORANGE = (220, 160, 40)
C_SCROLLBAR = (70, 70, 85)
C_SCROLLBAR_THUMB = (100, 100, 120)

TAB_NAMES = ["Biomes", "Species", "Textures"]

TEMPERATURES = ["frozen", "cold", "moderate", "warm", "hot", "extreme"]
VEGETATIONS = ["none", "minimal", "sparse", "moderate", "heavy", "alien"]
LIQUID_TYPES = ["water", "gas", "toxic", "none"]

TEXTURE_FILES = {
    "grass": "01_grass.png",
    "dirt": "02_dirt.png",
    "rock": "03_rock.png",
    "sand": "04_sand.png",
}
TEXTURE_SLOTS = ["base", "slope", "high", "detail"]


# ── Data Loading ──

def load_biome_data():
    """Load or bootstrap biome editor data from planet_generator BIOMES."""
    if BIOME_DATA_FILE.exists():
        with open(BIOME_DATA_FILE) as f:
            return json.load(f)

    # Bootstrap from planet_generator
    sys.path.insert(0, str(BACKEND_DIR))
    from planet_generator import BIOMES

    data = {}
    for key, b in BIOMES.items():
        data[key] = {
            "name": b["name"],
            "description": b["description"],
            "terrain": dict(b["terrain"]),
            "water_coverage": b.get("water_coverage", 0.1),
            "temperature": b.get("temperature", "moderate"),
            "vegetation": b.get("vegetation", "moderate"),
            "liquid_type": b.get("liquid_type", "water"),
            "dominant_resources": b.get("dominant_resources", []),
            "possible_resources": b.get("possible_resources", []),
            "images": [],
            "texture_palette": {
                "base": {"texture": "grass", "h": 0.33, "s": 0.5, "b": 0.5},
                "slope": {"texture": "rock", "h": 0.0, "s": 0.2, "b": 0.4},
                "high": {"texture": "rock", "h": 0.0, "s": 0.1, "b": 0.6},
                "detail": {"texture": "dirt", "h": 0.08, "s": 0.4, "b": 0.45},
            },
        }

    save_biome_data(data)
    return data


def save_biome_data(data):
    with open(BIOME_DATA_FILE, "w") as f:
        json.dump(data, f, indent=2)


def load_biome_mappings():
    if BIOME_MAPPINGS_FILE.exists():
        with open(BIOME_MAPPINGS_FILE) as f:
            return json.load(f)
    return {"mappings": {}, "biomes": []}


def save_biome_mappings(mappings):
    with open(BIOME_MAPPINGS_FILE, "w") as f:
        json.dump(mappings, f, indent=2)


def load_species_names():
    """Load species data from species_data_manager for display."""
    try:
        sys.path.insert(0, str(BACKEND_DIR))
        from species_data_manager import SpeciesDataManager
        sdm = SpeciesDataManager.get_instance()
        species = {}
        for gov_type in sdm.government_types:
            gov_info = sdm.get_government_info(gov_type)
            for tl in range(11):
                civ_id = f"{gov_type}_{tl}"
                sp = sdm.get_species_by_identifier(civ_id)
                if sp and not sp.get("name", "Unknown").startswith("Unknown"):
                    tech_info = sdm.get_tech_level_info(tl)
                    species[civ_id] = {
                        "name": sp.get("name", civ_id),
                        "government": gov_info.get("name", gov_type),
                        "tech_level": tl,
                        "tech_name": tech_info.get("name", "Unknown"),
                        "tech_era": tech_info.get("era", ""),
                        "physical": sp.get("physical_desc", ""),
                        "culture": sp.get("culture_notes", ""),
                        "homeworld": sp.get("homeworld_type", ""),
                        "building_style": sp.get("building_style", ""),
                        "trade_goods": sp.get("trade_goods", []),
                    }
        return species
    except Exception as e:
        print(f"Warning: Could not load species data: {e}")
        return {}


# ── UI Helpers ──

class UIState:
    def __init__(self):
        self.active_tab = 0
        self.selected_biome = 0
        self.selected_species = 0
        self.species_sort = "government"  # "government" or "tech_level"
        self.sidebar_scroll = 0
        self.content_scroll = 0
        self.active_input = None  # (field_name,) or None
        self.input_text = ""
        self.input_cursor = 0
        self.status_msg = "Ready"
        self.status_timer = 0
        self.hover_item = None
        self.image_preview = None  # Surface for full preview
        self.dropdown_open = None  # field name if dropdown is open
        self.dropdown_scroll = 0
        self.context_menu = None  # (x, y) position if open, None if closed
        self.dragging_image = None  # (img_name, "move"|"resize", offset_x, offset_y, owner_data)
        self.selected_image = None  # img_name of selected image
        self.selected_note = None   # note id if selected
        self.editing_note = None    # note id if actively typing
        self.dragging_note = None   # (note_id, "move", ox, oy, owner_data)
        self.right_scroll = 0       # scroll offset for right-half image panel
        self._deferred_dropdown = None  # (rect, value, options, scroll, click_rects) drawn after content
        self.confirm_delete = None  # (biome_key, x, y) if delete confirm popup is open


def set_status(ui, msg):
    ui.status_msg = msg
    ui.status_timer = 180  # frames


def draw_text(surf, text, x, y, font, color=C_TEXT, max_w=0):
    if max_w > 0:
        # Truncate with ellipsis
        rendered = font.render(text, True, color)
        if rendered.get_width() > max_w:
            while len(text) > 3 and font.size(text + "...")[0] > max_w:
                text = text[:-1]
            text += "..."
    rendered = font.render(text, True, color)
    surf.blit(rendered, (x, y))
    return rendered.get_rect(topleft=(x, y))


def draw_button(surf, rect, text, font, hover=False, color=None):
    c = color or (C_BUTTON_HOVER if hover else C_BUTTON)
    pygame.draw.rect(surf, c, rect, border_radius=4)
    tw, th = font.size(text)
    draw_text(surf, text, rect.x + (rect.w - tw) // 2, rect.y + (rect.h - th) // 2, font, C_TEXT_BRIGHT)


def draw_input_box(surf, rect, text, font, active=False, label=""):
    bg = C_INPUT_ACTIVE if active else C_INPUT_BG
    pygame.draw.rect(surf, bg, rect, border_radius=3)
    if active:
        pygame.draw.rect(surf, C_ACCENT, rect, 2, border_radius=3)
    # Draw text directly to surf (respects surf's clip rect) with manual clipping
    inner_x = rect.x + 4
    inner_y = rect.y + 3
    inner_w = rect.w - 8
    inner_h = rect.h - 6
    if inner_w > 0 and inner_h > 0:
        line_h = font.get_height() + 2
        if rect.h > line_h * 2:
            # Multi-line: word-wrap text
            words = text.split()
            lines = []
            line = ""
            for word in words:
                test = (line + " " + word).strip()
                if font.size(test)[0] > inner_w - 4:
                    if line:
                        lines.append(line)
                    line = word
                else:
                    line = test
            if line:
                lines.append(line)
            for i, ln in enumerate(lines):
                ty = inner_y + i * line_h
                if ty + line_h > rect.y + rect.h:
                    break
                surf.blit(font.render(ln, True, C_TEXT), (inner_x, ty))
        else:
            # Single-line
            tr = font.render(text, True, C_TEXT)
            surf.blit(tr, (inner_x, rect.y + (rect.h - tr.get_height()) // 2))
    if label:
        draw_text(surf, label, rect.x, rect.y - 18, font, C_TEXT_DIM)


def draw_slider(surf, rect, value, min_v, max_v, font, label="", fmt="{:.3f}"):
    visible = rect.clip(surf.get_rect())
    if visible.w <= 0 or visible.h <= 0:
        return
    if label:
        draw_text(surf, label, rect.x, rect.y - 18, font, C_TEXT_DIM)
    pygame.draw.rect(surf, C_SLIDER_BG, rect, border_radius=3)
    ratio = (value - min_v) / (max_v - min_v) if max_v != min_v else 0
    ratio = max(0, min(1, ratio))
    fill_w = int(rect.w * ratio)
    if fill_w > 0:
        fill_rect = pygame.Rect(rect.x, rect.y, fill_w, rect.h)
        pygame.draw.rect(surf, C_SLIDER_FILL, fill_rect, border_radius=3)
    # Knob
    knob_x = rect.x + fill_w
    knob_r = pygame.Rect(knob_x - 6, rect.y - 2, 12, rect.h + 4)
    pygame.draw.rect(surf, C_TEXT_BRIGHT, knob_r, border_radius=6)
    # Value text
    val_str = fmt.format(value)
    draw_text(surf, val_str, rect.right + 8, rect.y, font, C_TEXT)


def draw_dropdown(surf, rect, value, options, font, is_open=False, label="", scroll=0):
    if label:
        draw_text(surf, label, rect.x, rect.y - 18, font, C_TEXT_DIM)
    pygame.draw.rect(surf, C_INPUT_BG, rect, border_radius=3)
    draw_text(surf, str(value), rect.x + 6, rect.y + (rect.h - font.get_height()) // 2, font, C_TEXT)
    # Arrow
    ax = rect.right - 20
    ay = rect.y + rect.h // 2
    pygame.draw.polygon(surf, C_TEXT_DIM, [(ax, ay - 4), (ax + 8, ay - 4), (ax + 4, ay + 4)])
    if is_open:
        max_visible = min(len(options), 10)
        dd_h = max_visible * 26 + 2
        dd_rect = pygame.Rect(rect.x, rect.bottom + 2, rect.w, dd_h)
        # Opaque solid background
        pygame.draw.rect(surf, (38, 38, 50), dd_rect, border_radius=3)
        pygame.draw.rect(surf, C_ACCENT_DIM, dd_rect, 1, border_radius=3)
        visible_opts = options[scroll:scroll + max_visible]
        rects = []
        mx_dd, my_dd = pygame.mouse.get_pos()
        for i, opt in enumerate(visible_opts):
            oy = dd_rect.y + 1 + i * 26
            opt_rect = pygame.Rect(dd_rect.x + 1, oy, dd_rect.w - 2, 26)
            rects.append((opt_rect, opt))
            if opt == value:
                pygame.draw.rect(surf, C_SELECTED, opt_rect)
            elif opt_rect.collidepoint(mx_dd, my_dd):
                pygame.draw.rect(surf, C_HOVER, opt_rect)
            draw_text(surf, str(opt), opt_rect.x + 6, opt_rect.y + 4, font, C_TEXT, max_w=rect.w - 12)
        # Scroll indicators
        if scroll > 0:
            draw_text(surf, "^", dd_rect.right - 16, dd_rect.y + 2, font, C_TEXT_DIM)
        if scroll + max_visible < len(options):
            draw_text(surf, "v", dd_rect.right - 16, dd_rect.bottom - 16, font, C_TEXT_DIM)
        return rects, dd_rect
    return [], None


def hsb_to_rgb(h, s, b):
    r, g, bl = colorsys.hsv_to_rgb(h, s, b)
    return (int(r * 255), int(g * 255), int(bl * 255))


# ── Main App ──

class BiomeEditor:
    def __init__(self):
        pygame.init()
        self.screen = pygame.display.set_mode((WIN_W, WIN_H))
        pygame.display.set_caption("EDEN Biome Editor")
        self.clock = pygame.time.Clock()
        self.running = True

        self.font = pygame.font.SysFont("DejaVu Sans", 14)
        self.font_small = pygame.font.SysFont("DejaVu Sans", 12)
        self.font_bold = pygame.font.SysFont("DejaVu Sans", 15, bold=True)
        self.font_title = pygame.font.SysFont("DejaVu Sans", 18, bold=True)

        self.data = load_biome_data()
        self.mappings = load_biome_mappings()
        self.species = load_species_names()
        self.biome_keys = sorted(k for k in self.data.keys() if not k.startswith("_"))
        self.ui = UIState()
        self._species_keys_cache = {}
        self._rebuild_species_keys()

        # Ensure images dir exists
        BIOME_IMAGES_DIR.mkdir(exist_ok=True)

        # Cache loaded textures
        self.texture_cache = {}
        self._load_textures()

        # Image thumbnail cache
        self.thumb_cache = {}

        # Dragging state
        self.dragging_slider = None  # (field, min, max, rect)

        set_status(self.ui, f"Loaded {len(self.biome_keys)} biomes")

    def _load_textures(self):
        for name, fname in TEXTURE_FILES.items():
            path = TEXTURE_DIR / fname
            if path.exists():
                try:
                    img = pygame.image.load(str(path))
                    self.texture_cache[name] = img
                except Exception:
                    pass

    def _get_thumbnail(self, path, size=(150, 150)):
        cache_key = (path, size[0], size[1])
        if cache_key in self.thumb_cache:
            return self.thumb_cache[cache_key]
        full = BIOME_IMAGES_DIR / path
        if not full.exists():
            return None
        try:
            img = pygame.image.load(str(full))
            img = pygame.transform.smoothscale(img, size)
            self.thumb_cache[cache_key] = img
            return img
        except Exception:
            return None

    def _rebuild_species_keys(self):
        keys = list(self.species.keys())
        if self.ui.species_sort == "tech_level":
            # Sort by tech level (numeric), then by government name
            def tech_sort(k):
                parts = k.rsplit("_", 1)
                tl = int(parts[1]) if len(parts) == 2 and parts[1].isdigit() else 99
                return (tl, parts[0])
            self.species_keys = sorted(keys, key=tech_sort)
        else:
            # Sort by government name, then tech level (numeric)
            def gov_sort(k):
                parts = k.rsplit("_", 1)
                tl = int(parts[1]) if len(parts) == 2 and parts[1].isdigit() else 99
                return (parts[0], tl)
            self.species_keys = sorted(keys, key=gov_sort)
        self._species_keys_cache[self.ui.species_sort] = self.species_keys

    def _toggle_species_sort(self):
        # Remember current selection
        current_key = self.current_species_key()
        if self.ui.species_sort == "government":
            self.ui.species_sort = "tech_level"
        else:
            self.ui.species_sort = "government"
        self._rebuild_species_keys()
        # Restore selection
        if current_key and current_key in self.species_keys:
            self.ui.selected_species = self.species_keys.index(current_key)
        else:
            self.ui.selected_species = 0
        self.ui.sidebar_scroll = 0

    def current_biome_key(self):
        if 0 <= self.ui.selected_biome < len(self.biome_keys):
            return self.biome_keys[self.ui.selected_biome]
        return None

    def current_biome(self):
        key = self.current_biome_key()
        return self.data.get(key) if key else None

    def current_species_key(self):
        if 0 <= self.ui.selected_species < len(self.species_keys):
            return self.species_keys[self.ui.selected_species]
        return None

    def current_species_data(self):
        key = self.current_species_key()
        return self.species.get(key) if key else None

    def save(self):
        save_biome_data(self.data)
        set_status(self.ui, "Saved biome_editor_data.json")

    def run(self):
        while self.running:
            self.handle_events()
            self.draw()
            self.clock.tick(FPS)
        pygame.quit()

    # ── Event Handling ──

    def handle_events(self):
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                self.save()
                self.running = False
                return

            elif event.type == pygame.MOUSEBUTTONDOWN:
                self.handle_click(event)

            elif event.type == pygame.MOUSEBUTTONUP:
                if self.ui.dragging_image:
                    self.save()
                    self.ui.dragging_image = None
                if self.ui.dragging_note:
                    self.save()
                    self.ui.dragging_note = None
                self.dragging_slider = None

            elif event.type == pygame.MOUSEMOTION:
                if self.ui.dragging_image:
                    self.handle_image_drag(event)
                elif self.ui.dragging_note:
                    self.handle_note_drag(event)
                elif self.dragging_slider:
                    self.handle_slider_drag(event)

            elif event.type == pygame.MOUSEWHEEL:
                self.handle_scroll(event)

            elif event.type == pygame.DROPFILE:
                self._handle_drop_file(event.file)

            elif event.type == pygame.KEYDOWN:
                self.handle_key(event)

    def handle_click(self, event):
        mx, my = event.pos

        # Handle delete confirmation popup if open
        if self.ui.confirm_delete and event.button == 1:
            rects = getattr(self, '_confirm_delete_rects', None)
            if rects:
                del_btn, cancel_btn, popup_rect, key = rects
                if del_btn.collidepoint(mx, my):
                    self._delete_biome(key)
                    self.ui.confirm_delete = None
                    return
                if cancel_btn.collidepoint(mx, my) or not popup_rect.collidepoint(mx, my):
                    self.ui.confirm_delete = None
                    return
                return  # click inside popup but not on buttons — ignore
            self.ui.confirm_delete = None
            return

        # Right-click context menu on right half of content area
        mid_x = CONTENT_X + CONTENT_W // 2
        if event.button == 3 and self.ui.active_tab in (0, 1):
            if mx > mid_x and CONTENT_Y <= my < WIN_H - STATUS_BAR_H:
                self.ui.context_menu = (mx, my)
                return
            else:
                self.ui.context_menu = None

        # Left click closes context menu (after checking if it hit a menu item)
        if event.button == 1 and self.ui.context_menu:
            if self._handle_context_menu_click(mx, my):
                self.ui.context_menu = None
                return
            self.ui.context_menu = None

        # Close dropdown if clicking outside
        if self.ui.dropdown_open and event.button == 1:
            # Will be checked per-dropdown in tab drawing
            pass

        # Tab bar clicks
        if my < TAB_BAR_H:
            tab_w = (WIN_W - SIDEBAR_W) // len(TAB_NAMES)
            for i in range(len(TAB_NAMES)):
                tx = SIDEBAR_W + i * tab_w
                if tx <= mx < tx + tab_w:
                    self.ui.active_tab = i
                    self.ui.active_input = None
                    self.ui.dropdown_open = None
                    self.ui.content_scroll = 0
                    self.ui.right_scroll = 0
                    self.ui.sidebar_scroll = 0
                    self.ui.image_preview = None
                    return

        # Sidebar header clicks (sort button etc)
        list_top = TAB_BAR_H + 28
        if mx < SIDEBAR_W and TAB_BAR_H <= my < list_top and event.button == 1:
            self._click = (mx, my)
            return

        # Sidebar clicks (list starts 28px below tab bar for the title)
        if mx < SIDEBAR_W and list_top <= my < WIN_H - STATUS_BAR_H:
            keys = self.species_keys if self._sidebar_mode() == "species" else self.biome_keys
            idx = (my - list_top + self.ui.sidebar_scroll) // 30
            if 0 <= idx < len(keys):
                # Right-click to show delete confirmation
                if event.button == 3 and self._sidebar_mode() == "biomes":
                    self.ui.confirm_delete = (keys[idx], mx, my)
                    return
                if self._sidebar_mode() == "species":
                    self.ui.selected_species = idx
                else:
                    self.ui.selected_biome = idx
                self.ui.active_input = None
                self.ui.dropdown_open = None
                self.ui.content_scroll = 0
                self.ui.right_scroll = 0
                self.ui.image_preview = None
                return

        # Content area — delegate to active tab
        if mx >= SIDEBAR_W and TAB_BAR_H <= my < WIN_H - STATUS_BAR_H:
            if event.button == 1:
                self.handle_content_click(mx, my)

    def handle_scroll(self, event):
        mx, my = pygame.mouse.get_pos()
        mid_x = CONTENT_X + CONTENT_W // 2

        # If a dropdown is open, scroll it instead
        if self.ui.dropdown_open:
            self.ui.dropdown_scroll -= event.y
            self.ui.dropdown_scroll = max(0, self.ui.dropdown_scroll)
            return

        if mx < SIDEBAR_W:
            self.ui.sidebar_scroll -= event.y * 30
            keys = self.species_keys if self._sidebar_mode() == "species" else self.biome_keys
            list_h = WIN_H - TAB_BAR_H - STATUS_BAR_H - 28
            max_scroll = max(0, len(keys) * 30 - list_h)
            self.ui.sidebar_scroll = max(0, min(self.ui.sidebar_scroll, max_scroll))
        elif mx >= mid_x and self.ui.active_tab in (0, 1):
            # Right half scroll (image/notes panel)
            self.ui.right_scroll -= event.y * 40
            self.ui.right_scroll = max(0, self.ui.right_scroll)
        else:
            self.ui.content_scroll -= event.y * 40
            self.ui.content_scroll = max(0, self.ui.content_scroll)

    def handle_key(self, event):
        # Ctrl+S save
        if event.key == pygame.K_s and (event.mod & pygame.KMOD_CTRL):
            self.save()
            return

        # Ctrl+V paste image (Biomes or Species tab, right half)
        if event.key == pygame.K_v and (event.mod & pygame.KMOD_CTRL):
            if self.ui.active_tab in (0, 1):
                self.paste_image()
                return

        # Escape closes preview / dropdown
        if event.key == pygame.K_ESCAPE:
            if self.ui.editing_note:
                self._commit_note_edit()
                return
            if self.ui.selected_note:
                self.ui.selected_note = None
                return
            if self.ui.selected_image:
                self.ui.selected_image = None
                return
            if self.ui.dropdown_open:
                self.ui.dropdown_open = None
                return
            self.ui.active_input = None
            return

        # Text input
        if self.ui.active_input:
            field = self.ui.active_input
            if event.key == pygame.K_RETURN:
                self.commit_input(field)
                self.ui.active_input = None
                return
            elif event.key == pygame.K_TAB:
                self.commit_input(field)
                self.ui.active_input = None
                return
            elif event.key == pygame.K_BACKSPACE:
                if self.ui.input_cursor > 0:
                    self.ui.input_text = self.ui.input_text[:self.ui.input_cursor - 1] + self.ui.input_text[self.ui.input_cursor:]
                    self.ui.input_cursor -= 1
            elif event.key == pygame.K_DELETE:
                self.ui.input_text = self.ui.input_text[:self.ui.input_cursor] + self.ui.input_text[self.ui.input_cursor + 1:]
            elif event.key == pygame.K_LEFT:
                self.ui.input_cursor = max(0, self.ui.input_cursor - 1)
            elif event.key == pygame.K_RIGHT:
                self.ui.input_cursor = min(len(self.ui.input_text), self.ui.input_cursor + 1)
            elif event.key == pygame.K_HOME:
                self.ui.input_cursor = 0
            elif event.key == pygame.K_END:
                self.ui.input_cursor = len(self.ui.input_text)
            elif event.unicode and event.unicode.isprintable():
                self.ui.input_text = self.ui.input_text[:self.ui.input_cursor] + event.unicode + self.ui.input_text[self.ui.input_cursor:]
                self.ui.input_cursor += 1

    def commit_input(self, field):
        # Handle note editing
        if field and field.startswith("note."):
            self._commit_note_edit()
            return
        biome = self.current_biome()
        if not biome:
            return
        val = self.ui.input_text
        if field == "name":
            biome["name"] = val
        elif field == "description":
            biome["description"] = val
        elif field.startswith("terrain."):
            param = field.split(".")[1]
            try:
                if param in ("noiseOctaves",):
                    biome["terrain"][param] = int(val)
                else:
                    biome["terrain"][param] = float(val)
            except ValueError:
                set_status(self.ui, f"Invalid number for {param}")
                return
        elif field == "water_coverage":
            try:
                biome["water_coverage"] = max(0, min(1, float(val)))
            except ValueError:
                pass
        self.save()

    def activate_input(self, field, current_val):
        self.ui.active_input = field
        self.ui.input_text = str(current_val)
        self.ui.input_cursor = len(self.ui.input_text)
        self.ui.dropdown_open = None

    def handle_content_click(self, mx, my):
        """Route content click to the active tab handler."""
        # Store click coords for tab handlers
        self._click = (mx, my)

    def handle_slider_drag(self, event):
        if not self.dragging_slider:
            return
        field, min_v, max_v, rect = self.dragging_slider
        ratio = (event.pos[0] - rect.x) / rect.w
        ratio = max(0, min(1, ratio))
        val = min_v + ratio * (max_v - min_v)

        biome = self.current_biome()
        if not biome:
            return

        if field.startswith("terrain."):
            param = field.split(".")[1]
            if param == "noiseOctaves":
                val = int(round(val))
            biome["terrain"][param] = round(val, 4) if isinstance(val, float) else val
        elif field == "water_coverage":
            biome["water_coverage"] = round(val, 3)
        elif field.startswith("hsb."):
            parts = field.split(".")
            slot, component = parts[1], parts[2]
            biome.setdefault("texture_palette", {}).setdefault(slot, {})
            biome["texture_palette"][slot][component] = round(val, 3)

    def _get_image_owner(self):
        """Return (key, data_dict) for whichever item owns images on the current tab."""
        if self.ui.active_tab == 1:
            # Species tab
            sp_key = self.current_species_key()
            if sp_key:
                # Store species images in biome_editor_data under a "species_images" dict
                self.data.setdefault("_species_images", {})
                self.data["_species_images"].setdefault(sp_key, {"images": []})
                return sp_key, self.data["_species_images"][sp_key]
            return None, None
        else:
            # Biomes tab
            key = self.current_biome_key()
            biome = self.current_biome()
            return key, biome

    def paste_image(self):
        """Paste image from clipboard at the right-click position."""
        owner_key, owner_data = self._get_image_owner()
        if not owner_key or owner_data is None:
            return

        BIOME_IMAGES_DIR.mkdir(exist_ok=True)

        # Determine paste position from context menu location (in data space, accounting for scroll)
        paste_x, paste_y = None, None
        if self.ui.context_menu:
            paste_x = self.ui.context_menu[0]
            paste_y = self.ui.context_menu[1] + self.ui.right_scroll

        def _place_image(fname):
            """Set initial layout at mouse position."""
            if paste_x is not None and paste_y is not None:
                self._set_image_layout(owner_data, fname, paste_x, paste_y, 130, 130)

        # Try xclip first (Linux)
        try:
            result = subprocess.run(
                ["xclip", "-selection", "clipboard", "-t", "image/png", "-o"],
                capture_output=True, timeout=3
            )
            if result.returncode == 0 and len(result.stdout) > 100:
                img_count = len(owner_data.get("images", []))
                fname = f"{owner_key}_{img_count:03d}.png"
                fpath = BIOME_IMAGES_DIR / fname
                with open(fpath, "wb") as f:
                    f.write(result.stdout)
                owner_data.setdefault("images", []).append(fname)
                _place_image(fname)
                self.save()
                set_status(self.ui, f"Pasted image: {fname}")
                return
        except (FileNotFoundError, subprocess.TimeoutExpired):
            pass

        # Try PIL ImageGrab fallback
        try:
            from PIL import ImageGrab
            img = ImageGrab.grabclipboard()
            if img:
                img_count = len(owner_data.get("images", []))
                fname = f"{owner_key}_{img_count:03d}.png"
                fpath = BIOME_IMAGES_DIR / fname
                img.save(str(fpath))
                owner_data.setdefault("images", []).append(fname)
                _place_image(fname)
                self.save()
                set_status(self.ui, f"Pasted image: {fname}")
                return
        except ImportError:
            pass

        set_status(self.ui, "Paste failed — install xclip or Pillow")

    def import_images(self):
        """Open a native multi-file picker dialog (zenity) to import images."""
        owner_key, owner_data = self._get_image_owner()
        if not owner_key or owner_data is None:
            return
        try:
            result = subprocess.run(
                ["zenity", "--file-selection", "--multiple", "--separator=\n",
                 "--title=Import Images",
                 "--file-filter=Images | *.png *.jpg *.jpeg *.bmp *.gif *.webp",
                 "--file-filter=All files | *"],
                capture_output=True, text=True, timeout=120
            )
            if result.returncode == 0 and result.stdout.strip():
                files = [f for f in result.stdout.strip().split("\n") if f]
                self._import_files(files, owner_key, owner_data)
        except FileNotFoundError:
            set_status(self.ui, "zenity not found — install with: sudo apt install zenity")
        except subprocess.TimeoutExpired:
            pass
        except Exception as e:
            set_status(self.ui, f"Import failed: {e}")

    def _handle_drop_file(self, filepath):
        """Handle a file dropped onto the window."""
        if self.ui.active_tab not in (0, 1):
            return
        ext = Path(filepath).suffix.lower()
        if ext not in (".png", ".jpg", ".jpeg", ".bmp", ".gif", ".webp"):
            set_status(self.ui, f"Unsupported file type: {ext}")
            return
        owner_key, owner_data = self._get_image_owner()
        if not owner_key or owner_data is None:
            return
        self._import_files([filepath], owner_key, owner_data)

    def _import_files(self, filepaths, owner_key, owner_data):
        """Copy image files into biome_images/ and add them to the owner data."""
        BIOME_IMAGES_DIR.mkdir(exist_ok=True)
        mx, my = pygame.mouse.get_pos()
        count = 0
        for fpath in filepaths:
            src = Path(fpath)
            if not src.exists():
                continue
            img_count = len(owner_data.get("images", []))
            ext = src.suffix.lower() if src.suffix.lower() in (".png", ".jpg", ".jpeg") else ".png"
            fname = f"{owner_key}_{img_count:03d}{ext}"
            dst = BIOME_IMAGES_DIR / fname
            shutil.copy2(str(src), str(dst))
            owner_data.setdefault("images", []).append(fname)
            # Place at mouse position, offset each subsequent image
            place_x = mx + (count % 3) * 140
            place_y = my + (count // 3) * 140 + self.ui.right_scroll
            self._set_image_layout(owner_data, fname, place_x, place_y, 130, 130)
            count += 1
        if count:
            self.save()
            set_status(self.ui, f"Imported {count} image{'s' if count != 1 else ''}")

    # ── Drawing ──

    def draw(self):
        self.screen.fill(C_BG)
        self._click_rects = []  # Collect clickable areas for this frame
        self.ui._deferred_dropdown = None  # Reset each frame
        self.draw_tab_bar()
        self.draw_sidebar()
        self.draw_content()
        self.draw_status_bar()

        # Deferred dropdown overlay (drawn above content, but BEFORE click processing
        # so its click rects exist when we check them)
        self.draw_deferred_dropdown()

        # Handle queued click from this frame
        if hasattr(self, '_click'):
            self.process_click_rects(self._click)
            del self._click

        # Context menu (drawn above content)
        if self.ui.context_menu:
            self.draw_context_menu()

        # Delete biome confirmation popup
        if self.ui.confirm_delete:
            self.draw_confirm_delete()

        pygame.display.flip()

    def draw_tab_bar(self):
        pygame.draw.rect(self.screen, C_TAB_BG, (SIDEBAR_W, 0, CONTENT_W, TAB_BAR_H))
        tab_w = CONTENT_W // len(TAB_NAMES)
        mx, my = pygame.mouse.get_pos()
        for i, name in enumerate(TAB_NAMES):
            tx = SIDEBAR_W + i * tab_w
            rect = pygame.Rect(tx, 0, tab_w, TAB_BAR_H)
            if i == self.ui.active_tab:
                pygame.draw.rect(self.screen, C_TAB_ACTIVE, rect)
                pygame.draw.rect(self.screen, C_ACCENT, (tx, TAB_BAR_H - 3, tab_w, 3))
            elif rect.collidepoint(mx, my):
                pygame.draw.rect(self.screen, C_TAB_HOVER, rect)
            tw, th = self.font_bold.size(name)
            color = C_TEXT_BRIGHT if i == self.ui.active_tab else C_TEXT_DIM
            draw_text(self.screen, name, tx + (tab_w - tw) // 2, (TAB_BAR_H - th) // 2, self.font_bold, color)

    def _sidebar_mode(self):
        """Return 'species' when on species tab, 'biomes' otherwise."""
        return "species" if self.ui.active_tab == 1 else "biomes"

    def _sidebar_items(self):
        """Return (keys_list, selected_index, title, get_display_name_func)."""
        if self._sidebar_mode() == "species":
            def sp_name(key):
                sp = self.species.get(key, {})
                return sp.get("name", key)
            return self.species_keys, self.ui.selected_species, "Species", sp_name
        else:
            def biome_name(key):
                return self.data.get(key, {}).get("name", key)
            return self.biome_keys, self.ui.selected_biome, "Biomes", biome_name

    def draw_sidebar(self):
        sidebar_rect = pygame.Rect(0, TAB_BAR_H, SIDEBAR_W, WIN_H - TAB_BAR_H - STATUS_BAR_H)
        pygame.draw.rect(self.screen, C_SIDEBAR, sidebar_rect)

        keys, selected, title, name_fn = self._sidebar_items()

        # Title + count + add button
        draw_text(self.screen, title, 10, TAB_BAR_H + 6, self.font_bold, C_TEXT_BRIGHT)
        header_h = 28
        if self._sidebar_mode() == "biomes":
            # "+ Add" button at far right
            btn_r = pygame.Rect(SIDEBAR_W - 48, TAB_BAR_H + 4, 42, 20)
            mx_h, my_h = pygame.mouse.get_pos()
            hover = btn_r.collidepoint(mx_h, my_h)
            pygame.draw.rect(self.screen, C_BUTTON_HOVER if hover else C_ACCENT_DIM, btn_r, border_radius=3)
            lbl = "+ Add"
            tw = self.font_small.size(lbl)[0]
            draw_text(self.screen, lbl, btn_r.x + (btn_r.w - tw) // 2, btn_r.y + 3, self.font_small, C_TEXT_BRIGHT)
            self.add_click_rect(btn_r, self._add_biome)
            # Count to the left of the button
            count_text = f"({len(keys)})"
            draw_text(self.screen, count_text, btn_r.x - self.font_small.size(count_text)[0] - 6,
                      TAB_BAR_H + 8, self.font_small, C_TEXT_DIM)
        else:
            count_text = f"({len(keys)})"
            draw_text(self.screen, count_text, SIDEBAR_W - self.font_small.size(count_text)[0] - 10,
                      TAB_BAR_H + 8, self.font_small, C_TEXT_DIM)

        # Sort toggle for species tab
        if self._sidebar_mode() == "species":
            sort_label = "Gov" if self.ui.species_sort == "government" else "TL"
            btn_r = pygame.Rect(75, TAB_BAR_H + 4, 48, 20)
            mx_h, my_h = pygame.mouse.get_pos()
            hover = btn_r.collidepoint(mx_h, my_h)
            pygame.draw.rect(self.screen, C_BUTTON_HOVER if hover else C_ACCENT_DIM, btn_r, border_radius=3)
            tw = self.font_small.size(sort_label)[0]
            draw_text(self.screen, sort_label, btn_r.x + (btn_r.w - tw) // 2, btn_r.y + 3, self.font_small, C_TEXT_BRIGHT)
            self.add_click_rect(btn_r, self._toggle_species_sort)

        # Clip area for scrollable list
        list_y = TAB_BAR_H + header_h
        list_h = sidebar_rect.h - 28
        clip = pygame.Rect(0, list_y, SIDEBAR_W, list_h)
        self.screen.set_clip(clip)

        mx, my = pygame.mouse.get_pos()
        for i, key in enumerate(keys):
            iy = list_y + i * 30 - self.ui.sidebar_scroll
            if iy + 30 < list_y or iy > list_y + list_h:
                continue
            item_rect = pygame.Rect(0, iy, SIDEBAR_W, 30)
            if i == selected:
                pygame.draw.rect(self.screen, C_SELECTED, item_rect)
                pygame.draw.rect(self.screen, C_ACCENT, (0, iy, 3, 30))
            elif item_rect.collidepoint(mx, my):
                pygame.draw.rect(self.screen, C_HOVER, item_rect)

            name = name_fn(key)
            color = C_TEXT_BRIGHT if i == selected else C_TEXT
            draw_text(self.screen, name, 10, iy + 7, self.font, color, max_w=SIDEBAR_W - 20)

        self.screen.set_clip(None)

        # Scrollbar
        total_h = len(keys) * 30
        if total_h > list_h:
            sb_h = max(20, int(list_h * list_h / total_h))
            sb_y = list_y + int(self.ui.sidebar_scroll / total_h * list_h)
            pygame.draw.rect(self.screen, C_SCROLLBAR, (SIDEBAR_W - 6, list_y, 6, list_h))
            pygame.draw.rect(self.screen, C_SCROLLBAR_THUMB, (SIDEBAR_W - 6, sb_y, 6, sb_h), border_radius=3)

    def draw_content(self):
        content_rect = pygame.Rect(CONTENT_X, CONTENT_Y, CONTENT_W, CONTENT_H)
        pygame.draw.rect(self.screen, C_BG, content_rect)

        # Clip all content drawing to the content area
        self.screen.set_clip(content_rect)

        if self.ui.active_tab == 1:
            # Species tab uses species sidebar, not biome sidebar
            sp = self.current_species_data()
            sp_key = self.current_species_key()
            if not sp_key:
                draw_text(self.screen, "No species selected", CONTENT_X + 20, CONTENT_Y + 20, self.font, C_TEXT_DIM)
            else:
                self.draw_tab_species(sp_key, sp)
            self.screen.set_clip(None)
            return

        biome = self.current_biome()
        if not biome:
            draw_text(self.screen, "No biome selected", CONTENT_X + 20, CONTENT_Y + 20, self.font, C_TEXT_DIM)
            self.screen.set_clip(None)
            return

        if self.ui.active_tab == 0:
            self.draw_tab_fields(biome)
        elif self.ui.active_tab == 2:
            self.draw_tab_textures(biome)

        self.screen.set_clip(None)

    def draw_status_bar(self):
        sy = WIN_H - STATUS_BAR_H
        pygame.draw.rect(self.screen, C_STATUS, (0, sy, WIN_W, STATUS_BAR_H))
        if self.ui.status_timer > 0:
            self.ui.status_timer -= 1
        msg = self.ui.status_msg if self.ui.status_timer > 0 else "Ready — Ctrl+S to save"
        draw_text(self.screen, msg, 10, sy + 6, self.font_small, C_TEXT_DIM)

        # Right side info
        biome_key = self.current_biome_key()
        if biome_key:
            info = f"[{biome_key}]"
            iw = self.font_small.size(info)[0]
            draw_text(self.screen, info, WIN_W - iw - 10, sy + 6, self.font_small, C_TEXT_DIM)

    def process_click_rects(self, click_pos):
        """Process clickable rects collected during this frame's draw pass.
        Iterates in reverse so later-drawn (topmost) elements take priority."""
        mx, my = click_pos
        for rect, action, *args in reversed(self._click_rects):
            if rect.collidepoint(mx, my):
                action(*args)
                return

    def add_click_rect(self, rect, action, *args):
        self._click_rects.append((rect, action, *args))

    # ── Tab 0: Biome Fields ──

    def draw_tab_fields(self, biome):
        half_w = CONTENT_W // 2
        x0 = CONTENT_X + 15
        y = CONTENT_Y + 10 - self.ui.content_scroll
        col_w = half_w - 30  # fit widgets in left half
        mid_x = CONTENT_X + half_w

        # Vertical divider
        pygame.draw.line(self.screen, C_SLIDER_BG, (mid_x, CONTENT_Y), (mid_x, CONTENT_Y + CONTENT_H))

        # ── Left half: biome fields ──
        draw_text(self.screen, "Biome Properties", x0, y, self.font_title, C_TEXT_BRIGHT)
        y += 35

        # Name
        r = pygame.Rect(x0, y + 18, col_w, 28)
        active = self.ui.active_input == "name"
        txt = self.ui.input_text if active else biome.get("name", "")
        draw_input_box(self.screen, r, txt, self.font, active, "Name")
        self.add_click_rect(r, self.activate_input, "name", biome.get("name", ""))
        y += 55

        # Description (4-line tall box)
        r = pygame.Rect(x0, y + 18, col_w, 76)
        active = self.ui.active_input == "description"
        txt = self.ui.input_text if active else biome.get("description", "")
        draw_input_box(self.screen, r, txt, self.font, active, "Description")
        self.add_click_rect(r, self.activate_input, "description", biome.get("description", ""))
        y += 103

        # Terrain section
        draw_text(self.screen, "Terrain Parameters", x0, y, self.font_bold, C_ACCENT)
        y += 28

        terrain = biome.get("terrain", {})
        slider_w = min(col_w - 80, 280)
        sliders = [
            ("terrain.heightScale", "Height Scale", terrain.get("heightScale", 100), 5, 500, "{:.1f}"),
            ("terrain.noiseScale", "Noise Scale", terrain.get("noiseScale", 0.004), 0.0005, 0.020, "{:.4f}"),
            ("terrain.noiseOctaves", "Octaves", terrain.get("noiseOctaves", 4), 1, 8, "{:.0f}"),
            ("terrain.noisePersistence", "Persistence", terrain.get("noisePersistence", 0.45), 0.1, 0.8, "{:.3f}"),
        ]

        for field, label, val, mn, mx_v, fmt in sliders:
            r = pygame.Rect(x0, y + 18, slider_w, 18)
            draw_slider(self.screen, r, val, mn, mx_v, self.font, label, fmt)

            # Click to start dragging
            hit = r.inflate(0, 10)
            self.add_click_rect(hit, self._start_slider, field, mn, mx_v, r)

            # Also allow clicking the value to type it
            val_r = pygame.Rect(r.right + 8, r.y - 2, 65, 22)
            active = self.ui.active_input == field
            if active:
                draw_input_box(self.screen, val_r, self.ui.input_text, self.font, True)
                self.add_click_rect(val_r, lambda: None)  # absorb click
            else:
                self.add_click_rect(val_r, self.activate_input, field, fmt.format(val))
            y += 50

        # Water coverage
        wc = biome.get("water_coverage", 0.1)
        r = pygame.Rect(x0, y + 18, slider_w, 18)
        draw_slider(self.screen, r, wc, 0, 1, self.font, "Water Coverage", "{:.3f}")
        hit = r.inflate(0, 10)
        self.add_click_rect(hit, self._start_slider, "water_coverage", 0, 1, r)
        y += 55

        # Dropdowns
        draw_text(self.screen, "Climate Properties", x0, y, self.font_bold, C_ACCENT)
        y += 28

        dd_w = min(180, col_w)

        # Temperature dropdown
        r = pygame.Rect(x0, y + 18, dd_w, 28)
        is_open = self.ui.dropdown_open == "temperature"
        opts, dd_rect = draw_dropdown(self.screen, r, biome.get("temperature", "moderate"),
                                       TEMPERATURES, self.font, is_open, "Temperature", self.ui.dropdown_scroll)
        if is_open and opts:
            for opt_rect, opt_val in opts:
                self.add_click_rect(opt_rect, self._set_dropdown, "temperature", opt_val)
        self.add_click_rect(r, self._toggle_dropdown, "temperature")
        y += 55

        # Vegetation dropdown
        r = pygame.Rect(x0, y + 18, dd_w, 28)
        is_open = self.ui.dropdown_open == "vegetation"
        opts, dd_rect = draw_dropdown(self.screen, r, biome.get("vegetation", "moderate"),
                                       VEGETATIONS, self.font, is_open, "Vegetation", self.ui.dropdown_scroll)
        if is_open and opts:
            for opt_rect, opt_val in opts:
                self.add_click_rect(opt_rect, self._set_dropdown, "vegetation", opt_val)
        self.add_click_rect(r, self._toggle_dropdown, "vegetation")
        y += 55

        # Liquid type dropdown
        r = pygame.Rect(x0, y + 18, dd_w, 28)
        is_open = self.ui.dropdown_open == "liquid_type"
        opts, dd_rect = draw_dropdown(self.screen, r, biome.get("liquid_type", "water"),
                                       LIQUID_TYPES, self.font, is_open, "Liquid Type", self.ui.dropdown_scroll)
        if is_open and opts:
            for opt_rect, opt_val in opts:
                self.add_click_rect(opt_rect, self._set_dropdown, "liquid_type", opt_val)
        self.add_click_rect(r, self._toggle_dropdown, "liquid_type")
        y += 55

        # ── Right half: reference images ──
        self.draw_right_images()
        self.draw_right_notes()

    def _start_slider(self, field, mn, mx, rect):
        self.dragging_slider = (field, mn, mx, rect)
        # Immediately set value from click position
        ratio = (pygame.mouse.get_pos()[0] - rect.x) / rect.w
        ratio = max(0, min(1, ratio))
        val = mn + ratio * (mx - mn)
        biome = self.current_biome()
        if not biome:
            return
        if field.startswith("terrain."):
            param = field.split(".")[1]
            if param == "noiseOctaves":
                val = int(round(val))
            biome["terrain"][param] = round(val, 4) if isinstance(val, float) else val
        elif field == "water_coverage":
            biome["water_coverage"] = round(val, 3)
        elif field.startswith("hsb."):
            parts = field.split(".")
            slot, component = parts[1], parts[2]
            biome.setdefault("texture_palette", {}).setdefault(slot, {})
            biome["texture_palette"][slot][component] = round(val, 3)

    def _add_biome(self):
        """Add a new biome with a unique key and default params."""
        # Find next available name
        base = "new_biome"
        n = 1
        key = base
        while key in self.data:
            key = f"{base}_{n}"
            n += 1
        self.data[key] = {
            "name": key.replace("_", " ").title(),
            "description": "",
            "heightScale": 60.0,
            "noiseScale": 0.005,
            "octaves": 6,
            "persistence": 0.5,
            "water_coverage": 0.3,
            "temperature": "temperate",
            "vegetation": "moderate",
            "liquid_type": "water",
            "images": [],
            "notes": [],
            "texture_palette": {}
        }
        self.biome_keys = sorted(k for k in self.data.keys() if not k.startswith("_"))
        # Select the new biome
        idx = self.biome_keys.index(key)
        self.ui.selected_biome = idx
        self.save()
        set_status(self.ui, f"Added biome: {key}")

    def _delete_biome(self, key):
        """Delete a biome by key."""
        if key not in self.data:
            return
        name = self.data[key].get("name", key)
        del self.data[key]
        self.biome_keys = sorted(k for k in self.data.keys() if not k.startswith("_"))
        if self.ui.selected_biome >= len(self.biome_keys):
            self.ui.selected_biome = max(0, len(self.biome_keys) - 1)
        self.save()
        set_status(self.ui, f"Deleted biome: {name}")

    def draw_confirm_delete(self):
        """Draw a delete confirmation popup near the right-clicked biome."""
        key, cx, cy = self.ui.confirm_delete
        name = self.data.get(key, {}).get("name", key)
        label = f"Delete '{name}'?"
        label_w = self.font.size(label)[0]
        popup_w = max(label_w + 30, 180)
        popup_h = 62
        # Position to the right of the click, keep on screen
        px = min(cx + 4, WIN_W - popup_w - 4)
        py = min(cy - 10, WIN_H - popup_h - STATUS_BAR_H - 4)
        popup_rect = pygame.Rect(px, py, popup_w, popup_h)
        # Background
        pygame.draw.rect(self.screen, (50, 50, 60), popup_rect, border_radius=5)
        pygame.draw.rect(self.screen, C_ACCENT_DIM, popup_rect, 1, border_radius=5)
        # Label
        draw_text(self.screen, label, px + 10, py + 8, self.font, C_TEXT_BRIGHT)
        # Buttons
        btn_w = 60
        btn_h = 22
        del_btn = pygame.Rect(px + popup_w - btn_w - 10, py + popup_h - btn_h - 8, btn_w, btn_h)
        cancel_btn = pygame.Rect(del_btn.x - btn_w - 8, py + popup_h - btn_h - 8, btn_w, btn_h)
        mx_now, my_now = pygame.mouse.get_pos()
        # Cancel button
        hover_c = cancel_btn.collidepoint(mx_now, my_now)
        pygame.draw.rect(self.screen, C_BUTTON_HOVER if hover_c else C_ACCENT_DIM, cancel_btn, border_radius=3)
        tw = self.font_small.size("Cancel")[0]
        draw_text(self.screen, "Cancel", cancel_btn.x + (btn_w - tw) // 2, cancel_btn.y + 4, self.font_small, C_TEXT)
        # Delete button (red-ish)
        hover_d = del_btn.collidepoint(mx_now, my_now)
        del_color = (180, 60, 60) if hover_d else (140, 45, 45)
        pygame.draw.rect(self.screen, del_color, del_btn, border_radius=3)
        tw = self.font_small.size("Delete")[0]
        draw_text(self.screen, "Delete", del_btn.x + (btn_w - tw) // 2, del_btn.y + 4, self.font_small, C_TEXT_BRIGHT)
        # Store rects for click handling
        self._confirm_delete_rects = (del_btn, cancel_btn, popup_rect, key)

    def _toggle_dropdown(self, field):
        if self.ui.dropdown_open == field:
            self.ui.dropdown_open = None
        else:
            self.ui.dropdown_open = field
            self.ui.dropdown_scroll = 0

    def _set_dropdown(self, field, value):
        biome = self.current_biome()
        if biome:
            biome[field] = value
            self.save()
        self.ui.dropdown_open = None

    def _assign_species_biome(self, sp_key, biome_display_name):
        # Find biome key from display name
        biome_key = None
        for k in self.biome_keys:
            if self.data.get(k, {}).get("name", k) == biome_display_name:
                biome_key = k
                break
        if biome_key:
            self.mappings.setdefault("mappings", {})[sp_key] = biome_key
            biomes_list = self.mappings.setdefault("biomes", [])
            if biome_key not in biomes_list:
                biomes_list.append(biome_key)
            save_biome_mappings(self.mappings)
            set_status(self.ui, f"Assigned {sp_key} → {biome_key}")
        self.ui.dropdown_open = None

    # ── Right-half image panel (shared by Biomes + Species tabs) ──

    def _get_image_layout(self, owner_data, img_name, index):
        """Get stored position/size for an image, or default grid position."""
        layouts = owner_data.setdefault("image_layouts", {})
        if img_name in layouts:
            L = layouts[img_name]
            return L["x"], L["y"], L["w"], L["h"]
        # Default: grid position in right half
        mid_x = CONTENT_X + CONTENT_W // 2
        rx = mid_x + 15
        ry = CONTENT_Y + 40
        rw = CONTENT_W // 2 - 30
        cols = max(1, rw // 145)
        col = index % cols
        row = index // cols
        x = rx + col * 140
        y = ry + row * 140
        return x, y, 130, 130

    def _set_image_layout(self, owner_data, img_name, x, y, w, h):
        layouts = owner_data.setdefault("image_layouts", {})
        layouts[img_name] = {"x": x, "y": y, "w": max(50, w), "h": max(50, h)}

    def draw_right_images(self):
        """Draw movable/resizable images on the right half."""
        _, owner_data = self._get_image_owner()
        if owner_data is None:
            return

        mid_x = CONTENT_X + CONTENT_W // 2
        scroll = self.ui.right_scroll

        draw_text(self.screen, "Reference Images", mid_x + 15, CONTENT_Y + 10, self.font_bold, C_TEXT_DIM)
        draw_text(self.screen, "Right-click to paste", mid_x + 160, CONTENT_Y + 12, self.font_small, C_TEXT_DIM)

        images = owner_data.get("images", [])
        if not images:
            draw_text(self.screen, "No images yet", mid_x + 15, CONTENT_Y + 50 - scroll, self.font_small, C_TEXT_DIM)
            return

        RESIZE_HANDLE = 12

        for i, img_name in enumerate(images):
            ix, iy, iw, ih = self._get_image_layout(owner_data, img_name, i)
            iy -= scroll  # apply scroll
            img_rect = pygame.Rect(ix, iy, iw, ih)

            # Background
            pygame.draw.rect(self.screen, C_INPUT_BG, img_rect, border_radius=4)

            # Thumbnail (scaled to fit)
            thumb = self._get_thumbnail(img_name, (iw, ih))
            if thumb:
                self.screen.blit(thumb, (ix, iy))

            is_selected = self.ui.selected_image == img_name

            # Click to select + start drag (added first so it's lowest priority in reversed check)
            self.add_click_rect(img_rect, self._start_image_move, img_name, owner_data)

            # Selection border
            if is_selected:
                pygame.draw.rect(self.screen, C_ACCENT, img_rect, 2, border_radius=4)

                # Delete button (top-right, only on selected)
                del_rect = pygame.Rect(ix + iw - 22, iy + 2, 20, 20)
                pygame.draw.rect(self.screen, C_BUTTON_DANGER, del_rect, border_radius=3)
                draw_text(self.screen, "X", del_rect.x + 4, del_rect.y + 2, self.font_small, C_TEXT_BRIGHT)
                self.add_click_rect(del_rect, self._delete_image, img_name)

                # Resize handle (bottom-right corner, bigger hit area)
                rh_rect = pygame.Rect(ix + iw - RESIZE_HANDLE, iy + ih - RESIZE_HANDLE,
                                      RESIZE_HANDLE, RESIZE_HANDLE)
                pygame.draw.rect(self.screen, C_ACCENT, rh_rect)
                for offset in (3, 6, 9):
                    pygame.draw.line(self.screen, C_TEXT_DIM,
                                     (rh_rect.right - offset, rh_rect.bottom - 1),
                                     (rh_rect.right - 1, rh_rect.bottom - offset))
                hit_rh = rh_rect.inflate(8, 8)  # bigger hit area for easier grabbing
                self.add_click_rect(hit_rh, self._start_image_resize, img_name, owner_data)
            else:
                pygame.draw.rect(self.screen, (60, 60, 70), img_rect, 1, border_radius=4)

    def _start_image_move(self, img_name, owner_data):
        mx, my = pygame.mouse.get_pos()
        scroll = self.ui.right_scroll
        layouts = owner_data.get("image_layouts", {})
        L = layouts.get(img_name)
        if L:
            ox, oy = mx - L["x"], my - (L["y"] - scroll)
        else:
            ox, oy = 0, 0
        self.ui.selected_image = img_name
        self.ui.dragging_image = (img_name, "move", ox, oy, owner_data)

    def _start_image_resize(self, img_name, owner_data):
        mx, my = pygame.mouse.get_pos()
        self.ui.selected_image = img_name
        self.ui.dragging_image = (img_name, "resize", mx, my, owner_data)

    def handle_image_drag(self, event):
        if not self.ui.dragging_image:
            return
        img_name, mode, ox, oy, owner_data = self.ui.dragging_image
        mx, my = event.pos
        mid_x = CONTENT_X + CONTENT_W // 2

        if mode == "move":
            scroll = self.ui.right_scroll
            nx = max(mid_x + 2, mx - ox)
            ny = max(CONTENT_Y, my - oy) + scroll  # store in data space (scroll-independent)
            layouts = owner_data.get("image_layouts", {})
            L = layouts.get(img_name, {"x": nx, "y": ny, "w": 130, "h": 130})
            self._set_image_layout(owner_data, img_name, nx, ny, L["w"], L["h"])
        elif mode == "resize":
            layouts = owner_data.get("image_layouts", {})
            L = layouts.get(img_name)
            if L:
                dw = mx - ox
                dh = my - oy
                nw = max(50, L["w"] + dw)
                nh = max(50, L["h"] + dh)
                self._set_image_layout(owner_data, img_name, L["x"], L["y"], nw, nh)
                # Update drag origin so delta is incremental
                self.ui.dragging_image = (img_name, "resize", mx, my, owner_data)
                # Invalidate thumbnail cache for this size
                for key in list(self.thumb_cache.keys()):
                    if isinstance(key, tuple) and key[0] == img_name:
                        del self.thumb_cache[key]

    def _delete_image(self, img_name):
        _, owner_data = self._get_image_owner()
        if owner_data and img_name in owner_data.get("images", []):
            owner_data["images"].remove(img_name)
            # Remove layout
            owner_data.get("image_layouts", {}).pop(img_name, None)
            path = BIOME_IMAGES_DIR / img_name
            if path.exists():
                path.unlink()
            # Clear caches
            for key in list(self.thumb_cache.keys()):
                if key == img_name or (isinstance(key, tuple) and key[0] == img_name):
                    del self.thumb_cache[key]
            if self.ui.selected_image == img_name:
                self.ui.selected_image = None
            self.save()
            set_status(self.ui, f"Deleted {img_name}")

    # ── Notes ──

    def _add_note(self):
        _, owner_data = self._get_image_owner()
        if owner_data is None:
            return
        notes = owner_data.setdefault("notes", [])
        # Place at the right-click position
        cmx, cmy = self.ui.context_menu or (CONTENT_X + CONTENT_W * 3 // 4, CONTENT_Y + 60)
        note_id = f"note_{len(notes)}_{id(notes) % 10000}"
        note = {"id": note_id, "text": "New note", "x": cmx, "y": cmy, "w": 160, "h": 60}
        notes.append(note)
        self.ui.selected_note = note_id
        self.ui.editing_note = note_id
        self.ui.active_input = f"note.{note_id}"
        self.ui.input_text = "New note"
        self.ui.input_cursor = len(self.ui.input_text)
        self.save()
        set_status(self.ui, "Added note — start typing")

    def _delete_note(self, note_id):
        _, owner_data = self._get_image_owner()
        if owner_data is None:
            return
        notes = owner_data.get("notes", [])
        owner_data["notes"] = [n for n in notes if n["id"] != note_id]
        if self.ui.selected_note == note_id:
            self.ui.selected_note = None
        if self.ui.editing_note == note_id:
            self.ui.editing_note = None
            self.ui.active_input = None
        self.save()
        set_status(self.ui, "Deleted note")

    def _get_note(self, owner_data, note_id):
        for n in owner_data.get("notes", []):
            if n["id"] == note_id:
                return n
        return None

    def draw_right_notes(self):
        """Draw movable note boxes on the right half."""
        _, owner_data = self._get_image_owner()
        if owner_data is None:
            return

        scroll = self.ui.right_scroll

        for note in owner_data.get("notes", []):
            nid = note["id"]
            nx, ny, nw, nh = note["x"], note["y"] - scroll, note["w"], note["h"]
            note_rect = pygame.Rect(nx, ny, nw, nh)
            is_selected = self.ui.selected_note == nid
            is_editing = self.ui.editing_note == nid

            # White box
            pygame.draw.rect(self.screen, (245, 245, 240), note_rect, border_radius=4)
            if is_selected:
                pygame.draw.rect(self.screen, C_ACCENT, note_rect, 2, border_radius=4)
            else:
                pygame.draw.rect(self.screen, (180, 180, 175), note_rect, 1, border_radius=4)

            # Move click rect (lowest priority)
            self.add_click_rect(note_rect, self._select_note, nid, owner_data)

            # Text content
            text = self.ui.input_text if is_editing else note.get("text", "")
            # Word-wrap in the note box
            words = text.split()
            lines = []
            line = ""
            inner_w = nw - 12
            for word in words:
                test = (line + " " + word).strip()
                if self.font_small.size(test)[0] > inner_w:
                    if line:
                        lines.append(line)
                    line = word
                else:
                    line = test
            if line:
                lines.append(line)
            if not lines:
                lines = [""]

            for i, ln in enumerate(lines):
                ty = ny + 6 + i * 16
                if ty + 16 > ny + nh:
                    break
                self.screen.blit(self.font_small.render(ln, True, (30, 30, 30)), (nx + 6, ty))

            # Cursor blink when editing
            if is_editing and pygame.time.get_ticks() % 1000 < 500:
                # Simple cursor at end of last visible line
                last_line = lines[-1] if lines else ""
                cx = nx + 6 + self.font_small.size(last_line)[0]
                cy = ny + 6 + (min(len(lines), (nh - 12) // 16) - 1) * 16
                pygame.draw.line(self.screen, (30, 30, 30), (cx, cy), (cx, cy + 14))

            if is_selected:
                # Delete button
                del_rect = pygame.Rect(nx + nw - 18, ny + 2, 16, 16)
                pygame.draw.rect(self.screen, C_BUTTON_DANGER, del_rect, border_radius=3)
                draw_text(self.screen, "X", del_rect.x + 3, del_rect.y + 1, self.font_small, C_TEXT_BRIGHT)
                self.add_click_rect(del_rect, self._delete_note, nid)

                # Resize handle
                rh_rect = pygame.Rect(nx + nw - 10, ny + nh - 10, 10, 10)
                pygame.draw.rect(self.screen, (180, 180, 175), rh_rect)
                for offset in (3, 6):
                    pygame.draw.line(self.screen, (140, 140, 140),
                                     (rh_rect.right - offset, rh_rect.bottom - 1),
                                     (rh_rect.right - 1, rh_rect.bottom - offset))
                hit_rh = rh_rect.inflate(8, 8)
                self.add_click_rect(hit_rh, self._start_note_resize, nid, owner_data)

    def _select_note(self, note_id, owner_data):
        # If double-clicking same note, enter edit mode
        if self.ui.selected_note == note_id and not self.ui.editing_note:
            note = self._get_note(owner_data, note_id)
            if note:
                self.ui.editing_note = note_id
                self.ui.active_input = f"note.{note_id}"
                self.ui.input_text = note.get("text", "")
                self.ui.input_cursor = len(self.ui.input_text)
            return

        # Commit previous note edit if any
        if self.ui.editing_note:
            self._commit_note_edit(owner_data)

        self.ui.selected_note = note_id
        self.ui.selected_image = None
        self.ui.editing_note = None
        # Start drag
        mx, my = pygame.mouse.get_pos()
        note = self._get_note(owner_data, note_id)
        if note:
            scroll = self.ui.right_scroll
            self.ui.dragging_note = (note_id, "move", mx - note["x"], my - (note["y"] - scroll), owner_data)

    def _start_note_resize(self, note_id, owner_data):
        mx, my = pygame.mouse.get_pos()
        self.ui.selected_note = note_id
        self.ui.selected_image = None
        self.ui.dragging_note = (note_id, "resize", mx, my, owner_data)

    def _commit_note_edit(self, owner_data=None):
        if not self.ui.editing_note:
            return
        if owner_data is None:
            _, owner_data = self._get_image_owner()
        if owner_data:
            note = self._get_note(owner_data, self.ui.editing_note)
            if note:
                note["text"] = self.ui.input_text
                self.save()
        self.ui.editing_note = None
        self.ui.active_input = None

    def handle_note_drag(self, event):
        if not self.ui.dragging_note:
            return
        note_id, mode, ox, oy, owner_data = self.ui.dragging_note
        note = self._get_note(owner_data, note_id)
        if not note:
            return
        mx, my = event.pos
        mid_x = CONTENT_X + CONTENT_W // 2

        if mode == "move":
            scroll = self.ui.right_scroll
            note["x"] = max(mid_x + 2, mx - ox)
            note["y"] = max(CONTENT_Y, my - oy) + scroll
        elif mode == "resize":
            dw = mx - ox
            dh = my - oy
            note["w"] = max(80, note["w"] + dw)
            note["h"] = max(30, note["h"] + dh)
            self.ui.dragging_note = (note_id, "resize", mx, my, owner_data)

    # ── Deferred dropdown overlay ──

    def draw_deferred_dropdown(self):
        """Draw dropdown overlay AFTER all content, so it sits on top."""
        dd_info = self.ui._deferred_dropdown
        if not dd_info:
            return
        r, value, options, scroll, sp_key = dd_info
        max_visible = min(len(options), 10)
        dd_h = max_visible * 26 + 2
        dd_rect = pygame.Rect(r.x, r.bottom + 2, r.w, dd_h)
        # Solid opaque background
        pygame.draw.rect(self.screen, (38, 38, 50), dd_rect)
        pygame.draw.rect(self.screen, C_ACCENT_DIM, dd_rect, 1, border_radius=3)
        visible_opts = options[scroll:scroll + max_visible]
        mx_dd, my_dd = pygame.mouse.get_pos()
        for i, opt in enumerate(visible_opts):
            oy = dd_rect.y + 1 + i * 26
            opt_rect = pygame.Rect(dd_rect.x + 1, oy, dd_rect.w - 2, 26)
            if opt == value:
                pygame.draw.rect(self.screen, C_SELECTED, opt_rect)
            elif opt_rect.collidepoint(mx_dd, my_dd):
                pygame.draw.rect(self.screen, C_HOVER, opt_rect)
            draw_text(self.screen, str(opt), opt_rect.x + 6, opt_rect.y + 4, self.font, C_TEXT, max_w=r.w - 12)
            self.add_click_rect(opt_rect, self._assign_species_biome, sp_key, opt)
        # Scroll indicators
        if scroll > 0:
            draw_text(self.screen, "^", dd_rect.right - 16, dd_rect.y + 2, self.font, C_TEXT_DIM)
        if scroll + max_visible < len(options):
            draw_text(self.screen, "v", dd_rect.right - 16, dd_rect.bottom - 16, self.font, C_TEXT_DIM)
        self.ui._deferred_dropdown = None

    # ── Context menu ──

    def draw_context_menu(self):
        """Draw right-click context menu if open."""
        if not self.ui.context_menu:
            return
        cmx, cmy = self.ui.context_menu
        items = ["Paste Image", "Import Images", "Add Note"]
        if self.ui.selected_image:
            items.append("Delete Image")
        if self.ui.selected_note:
            items.append("Delete Note")
        menu_w = 160
        menu_h = len(items) * 28 + 4
        menu_rect = pygame.Rect(cmx, cmy, menu_w, menu_h)
        if menu_rect.right > WIN_W:
            menu_rect.x = WIN_W - menu_w
        if menu_rect.bottom > WIN_H - STATUS_BAR_H:
            menu_rect.y = WIN_H - STATUS_BAR_H - menu_h
        pygame.draw.rect(self.screen, (50, 50, 60), menu_rect, border_radius=4)
        pygame.draw.rect(self.screen, C_ACCENT_DIM, menu_rect, 1, border_radius=4)
        self._context_menu_items = []
        for i, label in enumerate(items):
            iy = menu_rect.y + 2 + i * 28
            item_rect = pygame.Rect(menu_rect.x, iy, menu_w, 28)
            self._context_menu_items.append((item_rect, label))
            mx_now, my_now = pygame.mouse.get_pos()
            if item_rect.collidepoint(mx_now, my_now):
                pygame.draw.rect(self.screen, C_SELECTED, item_rect, border_radius=3)
            draw_text(self.screen, label, item_rect.x + 10, iy + 6, self.font_small, C_TEXT_BRIGHT)

    def _handle_context_menu_click(self, mx, my):
        """Handle click on context menu. Returns True if handled."""
        for item_rect, label in getattr(self, '_context_menu_items', []):
            if item_rect.collidepoint(mx, my):
                if label == "Paste Image":
                    self.paste_image()
                elif label == "Import Images":
                    self.import_images()
                elif label == "Delete Image" and self.ui.selected_image:
                    self._delete_image(self.ui.selected_image)
                elif label == "Add Note":
                    self._add_note()
                elif label == "Delete Note" and self.ui.selected_note:
                    self._delete_note(self.ui.selected_note)
                return True
        return True  # close menu regardless

    # ── Tab 2: Species ──

    def draw_tab_species(self, sp_key, sp_data):
        half_w = CONTENT_W // 2
        x0 = CONTENT_X + 15
        y = CONTENT_Y + 10 - self.ui.content_scroll
        w = half_w - 30  # text wraps within left half
        mid_x = CONTENT_X + half_w

        # Vertical divider
        pygame.draw.line(self.screen, C_SLIDER_BG, (mid_x, CONTENT_Y), (mid_x, CONTENT_Y + CONTENT_H))

        # ── Left half: species info ──
        name = sp_data.get("name", sp_key) if sp_data else sp_key
        gov = sp_data.get("government", "") if sp_data else ""
        tech_name = sp_data.get("tech_name", "") if sp_data else ""

        # Title line: Name — government_tl (Tech Era)
        draw_text(self.screen, name, x0, y, self.font_title, C_TEXT_BRIGHT)
        subtitle = f"{sp_key}"
        if tech_name:
            subtitle += f"  ({tech_name})"
        y += 26
        draw_text(self.screen, subtitle, x0, y, self.font_small, C_TEXT_DIM)
        y += 22

        # Assigned biome with dropdown
        mapped_biome = self.mappings.get("mappings", {}).get(sp_key, None)
        draw_text(self.screen, "Biome:", x0, y, self.font_bold, C_TEXT_DIM)

        biome_display = self.data.get(mapped_biome, {}).get("name", mapped_biome) if mapped_biome else "(unassigned)"
        biome_color = C_GREEN if mapped_biome else C_ORANGE

        # Biome assign dropdown
        dd_field = "species_biome_assign"
        dd_val = biome_display
        r = pygame.Rect(x0 + 60, y - 2, w - 60, 24)
        is_open = self.ui.dropdown_open == dd_field
        biome_options = [self.data[k].get("name", k) for k in self.biome_keys]

        # Always draw the closed dropdown button
        pygame.draw.rect(self.screen, C_INPUT_BG, r, border_radius=3)
        draw_text(self.screen, dd_val, r.x + 6, r.y + (r.h - self.font.get_height()) // 2,
                  self.font, biome_color, max_w=r.w - 28)
        ax = r.right - 20
        ay = r.y + r.h // 2
        pygame.draw.polygon(self.screen, C_TEXT_DIM, [(ax, ay - 4), (ax + 8, ay - 4), (ax + 4, ay + 4)])

        if is_open:
            # Defer dropdown rendering to after all content is drawn
            self.ui._deferred_dropdown = (r, dd_val, biome_options, self.ui.dropdown_scroll, sp_key)

        self.add_click_rect(r, self._toggle_dropdown, dd_field)
        y += 32

        if sp_data:
            # Physical
            phys = sp_data.get("physical", "")
            if phys:
                draw_text(self.screen, "Physical", x0, y, self.font_bold, C_ACCENT)
                y += 20
                y = self._draw_wrapped(phys, x0, y, w, self.font, C_TEXT)
                y += 10

            # Culture
            culture = sp_data.get("culture", "")
            if culture:
                draw_text(self.screen, "Culture", x0, y, self.font_bold, C_ACCENT)
                y += 20
                y = self._draw_wrapped(culture, x0, y, w, self.font, C_TEXT)
                y += 10

            # Homeworld
            hw = sp_data.get("homeworld", "")
            if hw:
                draw_text(self.screen, "Homeworld", x0, y, self.font_bold, C_ACCENT)
                y += 20
                y = self._draw_wrapped(hw, x0, y, w, self.font, C_TEXT)
                y += 10

            # Building style
            bs = sp_data.get("building_style", "")
            if bs:
                draw_text(self.screen, "Building Style", x0, y, self.font_bold, C_ACCENT)
                y += 20
                y = self._draw_wrapped(bs, x0, y, w, self.font, C_TEXT)
                y += 10

            # Trade goods
            goods = sp_data.get("trade_goods", [])
            if goods:
                draw_text(self.screen, "Trade Goods", x0, y, self.font_bold, C_ACCENT)
                y += 20
                y = self._draw_wrapped(", ".join(goods), x0, y, w, self.font, C_TEXT)

        # ── Right half: reference images ──
        self.draw_right_images()
        self.draw_right_notes()

    def _draw_wrapped(self, text, x, y, max_w, font, color):
        """Simple word-wrap text drawing. Returns final y after last line."""
        words = text.split()
        line = ""
        for word in words:
            test = (line + " " + word).strip()
            if font.size(test)[0] > max_w:
                if line:
                    draw_text(self.screen, line, x, y, font, color)
                    y += 20
                line = word
            else:
                line = test
        if line:
            draw_text(self.screen, line, x, y, font, color)
            y += 20
        return y

    # ── Tab 3: Texture Palette ──

    def draw_tab_textures(self, biome):
        x0 = CONTENT_X + 20
        y = CONTENT_Y + 10 - self.ui.content_scroll
        w = CONTENT_W - 40

        draw_text(self.screen, "Texture Palette", x0, y, self.font_title, C_TEXT_BRIGHT)
        y += 35

        palette = biome.get("texture_palette", {})
        tex_names = list(TEXTURE_FILES.keys())

        for slot in TEXTURE_SLOTS:
            slot_data = palette.get(slot, {"texture": "grass", "h": 0.33, "s": 0.5, "b": 0.5})

            # Section header
            draw_text(self.screen, slot.upper(), x0, y, self.font_bold, C_ACCENT)
            y += 24

            # Texture dropdown
            r = pygame.Rect(x0, y + 18, 180, 28)
            dd_field = f"tex_{slot}"
            is_open = self.ui.dropdown_open == dd_field
            opts, dd_rect = draw_dropdown(self.screen, r, slot_data.get("texture", "grass"),
                                           tex_names, self.font, is_open, "Texture", self.ui.dropdown_scroll)
            if is_open and opts:
                for opt_rect, opt_val in opts:
                    self.add_click_rect(opt_rect, self._set_texture_slot, slot, opt_val)
            self.add_click_rect(r, self._toggle_dropdown, dd_field)

            # Texture preview
            tex_name = slot_data.get("texture", "grass")
            if tex_name in self.texture_cache:
                preview = pygame.transform.smoothscale(self.texture_cache[tex_name], (60, 60))
                self.screen.blit(preview, (x0 + 200, y))

            # HSB color preview
            h_val = slot_data.get("h", 0.33)
            s_val = slot_data.get("s", 0.5)
            b_val = slot_data.get("b", 0.5)
            rgb = hsb_to_rgb(h_val, s_val, b_val)
            preview_rect = pygame.Rect(x0 + 280, y, 60, 60)
            pygame.draw.rect(self.screen, rgb, preview_rect, border_radius=4)
            pygame.draw.rect(self.screen, C_TEXT_DIM, preview_rect, 1, border_radius=4)

            y += 55

            # H slider
            sr = pygame.Rect(x0, y + 18, 300, 14)
            draw_slider(self.screen, sr, h_val, 0, 1, self.font, "Hue", "{:.3f}")
            hit = sr.inflate(0, 10)
            self.add_click_rect(hit, self._start_slider, f"hsb.{slot}.h", 0, 1, sr)
            y += 42

            # S slider
            sr = pygame.Rect(x0, y + 18, 300, 14)
            draw_slider(self.screen, sr, s_val, 0, 1, self.font, "Saturation", "{:.3f}")
            hit = sr.inflate(0, 10)
            self.add_click_rect(hit, self._start_slider, f"hsb.{slot}.s", 0, 1, sr)
            y += 42

            # B slider
            sr = pygame.Rect(x0, y + 18, 300, 14)
            draw_slider(self.screen, sr, b_val, 0, 1, self.font, "Brightness", "{:.3f}")
            hit = sr.inflate(0, 10)
            self.add_click_rect(hit, self._start_slider, f"hsb.{slot}.b", 0, 1, sr)
            y += 50

    def _set_texture_slot(self, slot, texture_name):
        biome = self.current_biome()
        if biome:
            biome.setdefault("texture_palette", {}).setdefault(slot, {})["texture"] = texture_name
            self.save()
        self.ui.dropdown_open = None


# ── Entry Point ──

if __name__ == "__main__":
    print("Starting EDEN Biome Editor...")
    print(f"Data file: {BIOME_DATA_FILE}")
    print(f"Mappings:  {BIOME_MAPPINGS_FILE}")
    print(f"Images:    {BIOME_IMAGES_DIR}")
    editor = BiomeEditor()
    editor.run()
