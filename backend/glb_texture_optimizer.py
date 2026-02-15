#!/usr/bin/env python3
"""
GLB Texture Optimizer — Downscale and convert textures to DDS (BC3/DXT5).

Reads a GLB file, extracts textures, downscales them, converts to DDS with
BC3 compression, and writes an optimized GLB. Also optionally exports
standalone DDS files.

Usage:
    python glb_texture_optimizer.py model.glb                     # 1024x1024 default
    python glb_texture_optimizer.py model.glb --size 512          # 512x512
    python glb_texture_optimizer.py model.glb --export-dds        # also save .dds files
    python glb_texture_optimizer.py *.glb --size 512 --export-dds # batch mode
    python glb_texture_optimizer.py input_dir/ --recursive        # process all GLBs in dir

Output: <name>_opt.glb (and optionally <name>_basecolor.dds etc.)
"""

import struct
import json
import sys
import io
import argparse
from pathlib import Path
from PIL import Image, DdsImagePlugin


def read_glb(filepath):
    """Parse a GLB file into gltf JSON + binary buffer."""
    with open(filepath, "rb") as f:
        magic, version, length = struct.unpack("<III", f.read(12))
        if magic != 0x46546C67:  # 'glTF'
            raise ValueError(f"Not a valid GLB file: {filepath}")

        # JSON chunk
        chunk_len, chunk_type = struct.unpack("<II", f.read(8))
        json_data = json.loads(f.read(chunk_len))

        # Binary chunk
        bin_data = b""
        remaining = length - 12 - 8 - chunk_len
        if remaining > 8:
            chunk_len2, chunk_type2 = struct.unpack("<II", f.read(8))
            bin_data = f.read(chunk_len2)

    return json_data, bin_data


def write_glb(filepath, gltf, bin_data):
    """Write gltf JSON + binary buffer as a GLB file."""
    json_bytes = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    # Pad JSON to 4-byte alignment
    while len(json_bytes) % 4 != 0:
        json_bytes += b" "
    # Pad binary to 4-byte alignment
    while len(bin_data) % 4 != 0:
        bin_data += b"\x00"

    total = 12 + 8 + len(json_bytes) + 8 + len(bin_data)

    with open(filepath, "wb") as f:
        # Header
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        # JSON chunk
        f.write(struct.pack("<II", len(json_bytes), 0x4E4F534A))
        f.write(json_bytes)
        # Binary chunk
        f.write(struct.pack("<II", len(bin_data), 0x004E4942))
        f.write(bin_data)


def extract_image(gltf, bin_data, image_index):
    """Extract a PIL Image from the GLB binary buffer."""
    img_info = gltf["images"][image_index]
    bv = gltf["bufferViews"][img_info["bufferView"]]
    offset = bv.get("byteOffset", 0)
    size = bv["byteLength"]
    img_bytes = bin_data[offset : offset + size]
    return Image.open(io.BytesIO(img_bytes))


def image_to_png_bytes(img):
    """Convert PIL Image to PNG bytes."""
    buf = io.BytesIO()
    img.save(buf, format="PNG", optimize=True)
    return buf.getvalue()


def image_to_dds_bytes(img, is_normal=False):
    """Convert PIL Image to DDS BC3 bytes."""
    if img.mode != "RGBA":
        img = img.convert("RGBA")
    buf = io.BytesIO()
    # BC3 (DXT5) for everything — good quality with alpha
    img.save(buf, format="DDS", pixel_format="BC3")
    return buf.getvalue()


def optimize_glb(input_path, output_path, target_size=1024, export_dds=False, use_dds_in_glb=False):
    """
    Optimize a GLB file by downscaling textures.

    Args:
        input_path: Source GLB file
        output_path: Destination GLB file
        target_size: Target texture resolution (e.g., 512, 1024)
        export_dds: If True, also export standalone DDS files
        use_dds_in_glb: If True, embed DDS in GLB (not widely supported,
                        default keeps PNG in GLB for compatibility)
    """
    input_path = Path(input_path)
    output_path = Path(output_path)
    stem = input_path.stem.replace("_texture", "")

    print(f"\n{'='*60}")
    print(f"Processing: {input_path.name}")
    print(f"{'='*60}")

    gltf, bin_data = read_glb(input_path)

    orig_size = len(bin_data)
    images = gltf.get("images", [])
    if not images:
        print("  No images found, skipping.")
        return

    # Identify texture types from material references
    tex_types = {}  # image_index -> type name
    for mat in gltf.get("materials", []):
        pbr = mat.get("pbrMetallicRoughness", {})
        bc = pbr.get("baseColorTexture", {})
        if bc:
            tex = gltf["textures"][bc["index"]]
            tex_types[tex.get("source", -1)] = "basecolor"
        mr = pbr.get("metallicRoughnessTexture", {})
        if mr:
            tex = gltf["textures"][mr["index"]]
            tex_types[tex.get("source", -1)] = "metallic_roughness"
        nt = mat.get("normalTexture", {})
        if nt:
            tex = gltf["textures"][nt["index"]]
            tex_types[tex.get("source", -1)] = "normal"
        et = mat.get("emissiveTexture", {})
        if et:
            tex = gltf["textures"][et["index"]]
            tex_types[tex.get("source", -1)] = "emissive"
        ot = mat.get("occlusionTexture", {})
        if ot:
            tex = gltf["textures"][ot["index"]]
            tex_types[tex.get("source", -1)] = "occlusion"

    # Rebuild binary buffer with optimized textures
    new_bin = bytearray()

    # First, copy all non-image buffer views
    image_bv_indices = set()
    for img in images:
        if "bufferView" in img:
            image_bv_indices.add(img["bufferView"])

    # Map old bufferView index -> new offset and size
    bv_remap = {}

    # Copy non-image buffer views first
    for bv_idx, bv in enumerate(gltf["bufferViews"]):
        if bv_idx in image_bv_indices:
            continue
        old_offset = bv.get("byteOffset", 0)
        old_size = bv["byteLength"]
        # Align to 4 bytes
        while len(new_bin) % 4 != 0:
            new_bin += b"\x00"
        new_offset = len(new_bin)
        new_bin += bin_data[old_offset : old_offset + old_size]
        bv_remap[bv_idx] = (new_offset, old_size)

    # Now process images
    for img_idx, img_info in enumerate(images):
        bv_idx = img_info.get("bufferView")
        if bv_idx is None:
            continue

        pil_img = extract_image(gltf, bin_data, img_idx)
        tex_type = tex_types.get(img_idx, img_info.get("name", f"texture_{img_idx}"))
        orig_w, orig_h = pil_img.size

        # Downscale if larger than target
        if orig_w > target_size or orig_h > target_size:
            # Maintain aspect ratio
            ratio = min(target_size / orig_w, target_size / orig_h)
            new_w = max(4, int(orig_w * ratio))
            new_h = max(4, int(orig_h * ratio))
            # Ensure dimensions are multiples of 4 (required for DDS BC compression)
            new_w = (new_w + 3) // 4 * 4
            new_h = (new_h + 3) // 4 * 4
            pil_img = pil_img.resize((new_w, new_h), Image.LANCZOS)
        else:
            new_w, new_h = orig_w, orig_h

        # Export standalone DDS if requested
        if export_dds:
            is_normal = "normal" in str(tex_type).lower()
            dds_path = output_path.parent / f"{stem}_{tex_type}.dds"
            dds_bytes = image_to_dds_bytes(pil_img, is_normal=is_normal)
            with open(dds_path, "wb") as f:
                f.write(dds_bytes)
            print(f"  Exported: {dds_path.name} ({len(dds_bytes)/1024:.0f} KB)")

        # For GLB embedding, use PNG (wider engine support) or DDS
        if use_dds_in_glb:
            embed_bytes = image_to_dds_bytes(pil_img)
            img_info["mimeType"] = "image/vnd-ms.dds"
        else:
            embed_bytes = image_to_png_bytes(pil_img)
            img_info["mimeType"] = "image/png"

        # Write to new binary buffer
        while len(new_bin) % 4 != 0:
            new_bin += b"\x00"
        new_offset = len(new_bin)
        new_bin += embed_bytes
        bv_remap[bv_idx] = (new_offset, len(embed_bytes))

        bv_orig = gltf["bufferViews"][bv_idx]
        orig_kb = bv_orig["byteLength"] / 1024
        new_kb = len(embed_bytes) / 1024
        print(f"  [{tex_type}] {orig_w}x{orig_h} -> {new_w}x{new_h}  "
              f"{orig_kb:.0f} KB -> {new_kb:.0f} KB  ({new_kb/orig_kb*100:.0f}%)")

    # Update buffer views with new offsets/sizes
    for bv_idx, (new_offset, new_size) in bv_remap.items():
        gltf["bufferViews"][bv_idx]["byteOffset"] = new_offset
        gltf["bufferViews"][bv_idx]["byteLength"] = new_size

    # Update buffer size
    if gltf.get("buffers"):
        gltf["buffers"][0]["byteLength"] = len(new_bin)

    # Write optimized GLB
    write_glb(output_path, gltf, bytes(new_bin))

    new_file_size = output_path.stat().st_size
    orig_file_size = input_path.stat().st_size
    print(f"\n  Result: {orig_file_size/1024/1024:.1f} MB -> {new_file_size/1024/1024:.1f} MB  "
          f"({new_file_size/orig_file_size*100:.0f}%)")
    print(f"  Saved:  {output_path}")


def main():
    parser = argparse.ArgumentParser(
        description="Optimize GLB textures — downscale and optionally convert to DDS"
    )
    parser.add_argument("inputs", nargs="+", help="GLB files or directories to process")
    parser.add_argument("--size", type=int, default=1024,
                        help="Target texture size (default: 1024)")
    parser.add_argument("--export-dds", action="store_true",
                        help="Export standalone DDS files alongside the GLB")
    parser.add_argument("--dds-in-glb", action="store_true",
                        help="Embed DDS textures in GLB (less compatible)")
    parser.add_argument("--recursive", "-r", action="store_true",
                        help="Recursively search directories for GLB files")
    parser.add_argument("--output-dir", "-o", type=str, default=None,
                        help="Output directory (default: same as input)")
    parser.add_argument("--suffix", type=str, default="_opt",
                        help="Suffix for output files (default: _opt)")

    args = parser.parse_args()

    # Collect all GLB files
    glb_files = []
    for inp in args.inputs:
        p = Path(inp)
        if p.is_dir():
            pattern = "**/*.glb" if args.recursive else "*.glb"
            glb_files.extend(p.glob(pattern))
        elif p.is_file() and p.suffix.lower() == ".glb":
            glb_files.append(p)
        else:
            print(f"Warning: skipping {inp}")

    if not glb_files:
        print("No GLB files found.")
        sys.exit(1)

    print(f"Found {len(glb_files)} GLB file(s), target size: {args.size}x{args.size}")

    out_dir = Path(args.output_dir) if args.output_dir else None
    if out_dir:
        out_dir.mkdir(parents=True, exist_ok=True)

    for glb_path in sorted(glb_files):
        # Skip already-optimized files
        if args.suffix in glb_path.stem:
            continue
        if out_dir:
            out_path = out_dir / f"{glb_path.stem}{args.suffix}.glb"
        else:
            out_path = glb_path.parent / f"{glb_path.stem}{args.suffix}.glb"

        try:
            optimize_glb(glb_path, out_path, args.size, args.export_dds, args.dds_in_glb)
        except Exception as e:
            print(f"  ERROR processing {glb_path.name}: {e}")

    print(f"\nDone! Processed {len(glb_files)} file(s).")


if __name__ == "__main__":
    main()
