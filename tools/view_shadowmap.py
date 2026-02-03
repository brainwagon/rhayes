#!/usr/bin/env python3
"""
View a shadow map (.shd) file as a grayscale image.
Near values are white, far values are dark.

Usage: python view_shadowmap.py <shadowmap.shd> [output.png]
"""

import struct
import sys
import numpy as np

def read_shadowmap(filename):
    """Read a .shd shadow map file and return metadata + depth array."""
    with open(filename, 'rb') as f:
        magic = struct.unpack('I', f.read(4))[0]
        version = struct.unpack('I', f.read(4))[0]
        width = struct.unpack('i', f.read(4))[0]
        height = struct.unpack('i', f.read(4))[0]
        near = struct.unpack('f', f.read(4))[0]
        far = struct.unpack('f', f.read(4))[0]

        # Skip matrices (NDC matrix + camera matrix for version 2)
        if version == 1:
            f.read(64)  # 4x4 matrix
        else:
            f.read(128)  # Two 4x4 matrices

        depths = np.array(struct.unpack(f'{width*height}f', f.read(width*height*4)))
        depths = depths.reshape((height, width))

    return {
        'version': version,
        'width': width,
        'height': height,
        'near': near,
        'far': far,
        'depths': depths
    }

def visualize_shadowmap(shadowmap, output_file=None):
    """Convert depth values to grayscale image (near=white, far=dark)."""
    depths = shadowmap['depths']

    # Find actual min/max (excluding very far values)
    valid_mask = depths < 1e10
    if not np.any(valid_mask):
        print("No geometry in shadow map!")
        return None

    valid_depths = depths[valid_mask]
    z_min = np.min(valid_depths)
    z_max = np.max(valid_depths)

    print(f"Shadow map: {shadowmap['width']}x{shadowmap['height']}")
    print(f"Depth range: {z_min:.2f} to {z_max:.2f} world units")
    print(f"Pixels with geometry: {np.sum(valid_mask)}")

    # Normalize: near (z_min) -> 1.0 (white), far (z_max) -> 0.0 (dark)
    # Background (very far) -> 0.0 (black)
    normalized = np.zeros_like(depths)
    if z_max > z_min:
        normalized[valid_mask] = 1.0 - (depths[valid_mask] - z_min) / (z_max - z_min)
    else:
        normalized[valid_mask] = 1.0

    # Convert to 8-bit grayscale
    img_array = (normalized * 255).astype(np.uint8)

    # Try to use PIL for saving/display
    try:
        from PIL import Image
        img = Image.fromarray(img_array, mode='L')

        if output_file:
            img.save(output_file)
            print(f"Saved to: {output_file}")
        else:
            # Try to display
            try:
                img.show()
            except:
                # If display fails, save to temp file
                output_file = '/tmp/shadowmap_view.png'
                img.save(output_file)
                print(f"Saved to: {output_file}")

        return img
    except ImportError:
        print("PIL not available. Install with: pip install Pillow")
        if output_file and output_file.endswith('.pgm'):
            # Write raw PGM as fallback
            with open(output_file, 'wb') as f:
                f.write(f'P5\n{shadowmap["width"]} {shadowmap["height"]}\n255\n'.encode())
                f.write(img_array.tobytes())
            print(f"Saved PGM to: {output_file}")
        return None

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    shd_file = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else None

    shadowmap = read_shadowmap(shd_file)
    visualize_shadowmap(shadowmap, output_file)
