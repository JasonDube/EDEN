#!/usr/bin/env python3
"""Texture cropper -- squares out of pictures, straight onto the ships shelf.

    python3 tools/texture_cropper.py [image.png] [more.png ...]

Drag a square on the picture (the selection stays square on its own), type a
name, hit Enter or Save. The crop lands in

    examples/terrain_editor/textures/building/ships/

as <name>.png -- resized to 64 or 128 if one of those buttons is lit, native
size if "native" is lit. Names auto-number themselves if taken (plate, then
plate_2, plate_3...), so mashing Enter over and over with one good name is a
workflow, not an accident. Open with no arguments and a file picker appears;
multiple images queue up, N goes to the next one.

Keys:  Enter=save   N=next image   1/2/0=size 64/128/native   Esc=quit
"""
import os
import sys
import tkinter as tk
from tkinter import filedialog
from PIL import Image, ImageTk

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "examples", "terrain_editor", "textures", "building", "ships")

MAX_VIEW = 900          # viewport cap; big sheets scale down to fit


class Cropper:
    def __init__(self, paths):
        self.paths = paths
        self.idx = -1
        self.saved = 0

        self.tkroot = tk.Tk()
        self.tkroot.title("texture cropper -- drag a square, name it, Enter")

        top = tk.Frame(self.tkroot)
        top.pack(fill="x", padx=6, pady=4)
        tk.Label(top, text="name:").pack(side="left")
        self.name = tk.Entry(top, width=24)
        self.name.pack(side="left", padx=4)
        self.name.insert(0, "plate")
        self.size = tk.IntVar(value=64)
        for label, val in (("64", 64), ("128", 128), ("native", 0)):
            tk.Radiobutton(top, text=label, variable=self.size, value=val).pack(side="left")
        tk.Button(top, text="Save (Enter)", command=self.save).pack(side="left", padx=8)
        tk.Button(top, text="Next image (N)", command=self.next_image).pack(side="left")
        self.status = tk.Label(self.tkroot, anchor="w", fg="#996")
        self.status.pack(fill="x", padx=6)

        self.canvas = tk.Canvas(self.tkroot, cursor="crosshair", bg="#222")
        self.canvas.pack(padx=6, pady=6)
        self.canvas.bind("<ButtonPress-1>", self.press)
        self.canvas.bind("<B1-Motion>", self.drag)
        self.tkroot.bind("<Return>", lambda e: self.save())
        self.tkroot.bind("n", lambda e: self.next_image())
        self.tkroot.bind("1", lambda e: self.size.set(64))
        self.tkroot.bind("2", lambda e: self.size.set(128))
        self.tkroot.bind("0", lambda e: self.size.set(0))
        self.tkroot.bind("<Escape>", lambda e: self.tkroot.destroy())

        self.sel = None          # (x0, y0, side) in VIEW pixels
        self.rect_id = None
        self.next_image()
        self.tkroot.mainloop()

    # ---- images ------------------------------------------------------------
    def next_image(self):
        self.idx += 1
        if self.idx >= len(self.paths):
            more = filedialog.askopenfilenames(
                title="pick pictures to crop",
                filetypes=[("images", "*.png *.jpg *.jpeg *.webp *.bmp")])
            if not more:
                if self.idx == 0:
                    self.tkroot.destroy()
                    return
                self.idx -= 1
                self.note(f"no more images -- {self.saved} saved so far")
                return
            self.paths.extend(more)
        path = self.paths[self.idx]
        self.img = Image.open(path).convert("RGBA")
        self.scale = min(1.0, MAX_VIEW / max(self.img.width, self.img.height))
        vw = int(self.img.width * self.scale)
        vh = int(self.img.height * self.scale)
        self.view = ImageTk.PhotoImage(self.img.resize((vw, vh), Image.NEAREST))
        self.canvas.config(width=vw, height=vh)
        self.canvas.delete("all")
        self.canvas.create_image(0, 0, anchor="nw", image=self.view)
        self.rect_id = None
        self.sel = None
        self.note(f"{os.path.basename(path)}  ({self.img.width}x{self.img.height})"
                  f" -- drag a square")

    # ---- selection (always square) ----------------------------------------
    def press(self, e):
        self.sel = (e.x, e.y, 0)
        self.redraw_rect()

    def drag(self, e):
        if not self.sel:
            return
        x0, y0, _ = self.sel
        side = max(abs(e.x - x0), abs(e.y - y0))
        # grow toward the drag direction
        self.sel = (x0 if e.x >= x0 else x0 - side,
                    y0 if e.y >= y0 else y0 - side,
                    side)
        self.redraw_rect()

    def redraw_rect(self):
        if self.rect_id:
            self.canvas.delete(self.rect_id)
        x, y, side = self.sel
        self.rect_id = self.canvas.create_rectangle(
            x, y, x + side, y + side, outline="#ff5", width=2)
        px = int(side / self.scale) if self.scale else side
        self.note(f"selection {px}x{px} px -- name it and press Enter")

    # ---- saving ------------------------------------------------------------
    def save(self):
        if not self.sel or self.sel[2] < 4:
            self.note("drag a square first")
            return
        base = self.name.get().strip() or "plate"
        x, y, side = self.sel
        # back to ORIGINAL pixel coordinates
        ox = int(x / self.scale)
        oy = int(y / self.scale)
        oside = int(side / self.scale)
        ox = max(0, min(ox, self.img.width - 1))
        oy = max(0, min(oy, self.img.height - 1))
        oside = max(1, min(oside, self.img.width - ox, self.img.height - oy))
        crop = self.img.crop((ox, oy, ox + oside, oy + oside))
        target = self.size.get()
        if target:
            crop = crop.resize((target, target), Image.LANCZOS)

        os.makedirs(OUT_DIR, exist_ok=True)
        name, n = base, 1
        while os.path.exists(os.path.join(OUT_DIR, name + ".png")):
            n += 1
            name = f"{base}_{n}"
        out = os.path.join(OUT_DIR, name + ".png")
        crop.save(out)
        self.saved += 1
        self.note(f"saved {name}.png ({crop.width}x{crop.height}) -- {self.saved} total")

    def note(self, msg):
        self.status.config(text=msg)


if __name__ == "__main__":
    Cropper(list(sys.argv[1:]))
