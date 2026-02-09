#!/usr/bin/env python3
"""Generate a purple night sky cubemap in horizontal cross format."""

import random
import math

try:
    from PIL import Image, ImageDraw, ImageFilter
except ImportError:
    print("PIL not found. Install with: pip install Pillow")
    exit(1)

# Configuration
FACE_SIZE = 512
CANVAS_WIDTH = FACE_SIZE * 4   # 2048
CANVAS_HEIGHT = FACE_SIZE * 3  # 1536

# Colors (RGB)
DARK_PURPLE = (15, 5, 25)
MID_PURPLE = (45, 20, 70)
LIGHT_PURPLE = (80, 40, 120)
DEEP_BLACK = (5, 2, 10)

def lerp_color(c1, c2, t):
    """Linear interpolate between two colors."""
    return tuple(int(c1[i] + (c2[i] - c1[i]) * t) for i in range(3))

def generate_gradient_face(size, top_color, bottom_color, variation=0.1):
    """Generate a face with vertical gradient and slight noise."""
    img = Image.new('RGB', (size, size))
    pixels = img.load()

    for y in range(size):
        t = y / size
        base_color = lerp_color(top_color, bottom_color, t)
        for x in range(size):
            # Add slight variation
            noise = random.uniform(-variation, variation)
            color = tuple(max(0, min(255, int(c * (1 + noise)))) for c in base_color)
            pixels[x, y] = color

    return img

def add_stars(img, density=0.001, bright_density=0.0002):
    """Add twinkling stars to an image."""
    draw = ImageDraw.Draw(img)
    width, height = img.size

    # Regular small stars
    num_stars = int(width * height * density)
    for _ in range(num_stars):
        x = random.randint(0, width - 1)
        y = random.randint(0, height - 1)

        # Vary star brightness
        brightness = random.randint(150, 255)
        # Slight color tint (white to light blue/purple)
        r = brightness
        g = brightness - random.randint(0, 30)
        b = brightness

        draw.point((x, y), fill=(r, g, b))

    # Brighter/larger stars
    num_bright = int(width * height * bright_density)
    for _ in range(num_bright):
        x = random.randint(1, width - 2)
        y = random.randint(1, height - 2)

        brightness = random.randint(200, 255)
        color = (brightness, brightness - random.randint(0, 20), brightness)

        # Draw a small cross or dot pattern for bright stars
        draw.point((x, y), fill=(255, 255, 255))
        if random.random() > 0.5:
            # Some stars have a slight glow
            dim = (brightness // 2, brightness // 2 - 10, brightness // 2)
            draw.point((x-1, y), fill=dim)
            draw.point((x+1, y), fill=dim)
            draw.point((x, y-1), fill=dim)
            draw.point((x, y+1), fill=dim)

    return img

def create_face(face_name):
    """Create a single cubemap face with appropriate gradient direction."""

    if face_name == '+Y':  # Top - looking up, darker in center
        img = generate_gradient_face(FACE_SIZE, DEEP_BLACK, MID_PURPLE, 0.15)
    elif face_name == '-Y':  # Bottom - looking down, purple horizon glow
        img = generate_gradient_face(FACE_SIZE, LIGHT_PURPLE, DARK_PURPLE, 0.1)
    elif face_name == '+Z':  # Front
        img = generate_gradient_face(FACE_SIZE, DARK_PURPLE, LIGHT_PURPLE, 0.1)
    elif face_name == '-Z':  # Back
        img = generate_gradient_face(FACE_SIZE, DARK_PURPLE, MID_PURPLE, 0.1)
    elif face_name == '+X':  # Right
        img = generate_gradient_face(FACE_SIZE, DARK_PURPLE, LIGHT_PURPLE, 0.1)
    elif face_name == '-X':  # Left
        img = generate_gradient_face(FACE_SIZE, DARK_PURPLE, LIGHT_PURPLE, 0.1)
    else:
        img = Image.new('RGB', (FACE_SIZE, FACE_SIZE), DARK_PURPLE)

    # Add stars (fewer on bottom face, more on top)
    if face_name == '+Y':
        add_stars(img, density=0.002, bright_density=0.0004)
    elif face_name == '-Y':
        add_stars(img, density=0.0003, bright_density=0.0001)
    else:
        add_stars(img, density=0.0012, bright_density=0.00025)

    return img

def main():
    # Create the canvas
    canvas = Image.new('RGB', (CANVAS_WIDTH, CANVAS_HEIGHT), (0, 0, 0))

    # Face positions in horizontal cross layout:
    #        [+Y]
    # [-X][+Z][+X][-Z]
    #        [-Y]

    face_positions = {
        '+Y': (1, 0),  # Top center
        '-X': (0, 1),  # Middle left
        '+Z': (1, 1),  # Middle center-left (front)
        '+X': (2, 1),  # Middle center-right
        '-Z': (3, 1),  # Middle right (back)
        '-Y': (1, 2),  # Bottom center
    }

    print("Generating night sky cubemap...")

    for face_name, (col, row) in face_positions.items():
        print(f"  Creating face {face_name}...")
        face_img = create_face(face_name)

        x = col * FACE_SIZE
        y = row * FACE_SIZE
        canvas.paste(face_img, (x, y))

    # Output path
    output_path = "/home/jasondube/Desktop/EDEN/build/examples/terrain_editor/sky_box/night_sky_purple.png"

    canvas.save(output_path, 'PNG')
    print(f"Saved to: {output_path}")
    print(f"Image size: {CANVAS_WIDTH}x{CANVAS_HEIGHT}")

if __name__ == "__main__":
    random.seed(42)  # For reproducible stars
    main()
