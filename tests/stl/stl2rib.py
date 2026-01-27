#!/usr/bin/env python3
"""
Convert binary STL files to RenderMan RIB format.

Usage: stl2rib.py input.stl output.rib [--scale SCALE] [--center]
"""

import struct
import sys
import argparse
import os


def read_binary_stl(filename):
    """
    Read a binary STL file and return list of triangles.
    Each triangle is (normal, v0, v1, v2) where each is a (x, y, z) tuple.
    """
    triangles = []

    with open(filename, 'rb') as f:
        # Skip 80-byte header
        f.read(80)

        # Read triangle count (uint32 little-endian)
        count_data = f.read(4)
        if len(count_data) < 4:
            raise ValueError("Invalid STL file: cannot read triangle count")
        triangle_count = struct.unpack('<I', count_data)[0]

        print(f"Reading {triangle_count:,} triangles from {filename}...")

        # Read each triangle (50 bytes each)
        for i in range(triangle_count):
            # Normal vector (3x float32)
            normal = struct.unpack('<3f', f.read(12))
            # 3 vertices (9x float32)
            v0 = struct.unpack('<3f', f.read(12))
            v1 = struct.unpack('<3f', f.read(12))
            v2 = struct.unpack('<3f', f.read(12))
            # Attribute byte count (2 bytes, usually ignored)
            f.read(2)

            triangles.append((normal, v0, v1, v2))

            if (i + 1) % 500000 == 0:
                print(f"  Read {i + 1:,} triangles...")

    return triangles


def compute_bounds(triangles):
    """Compute bounding box of all triangles."""
    min_x = min_y = min_z = float('inf')
    max_x = max_y = max_z = float('-inf')

    for _, v0, v1, v2 in triangles:
        for v in (v0, v1, v2):
            min_x = min(min_x, v[0])
            min_y = min(min_y, v[1])
            min_z = min(min_z, v[2])
            max_x = max(max_x, v[0])
            max_y = max(max_y, v[1])
            max_z = max(max_z, v[2])

    return (min_x, min_y, min_z), (max_x, max_y, max_z)


def write_rib(filename, triangles, scale=1.0, center=False):
    """Write triangles to RIB format."""

    # Compute bounds for camera setup
    bounds_min, bounds_max = compute_bounds(triangles)

    center_x = (bounds_min[0] + bounds_max[0]) / 2
    center_y = (bounds_min[1] + bounds_max[1]) / 2
    center_z = (bounds_min[2] + bounds_max[2]) / 2

    size_x = bounds_max[0] - bounds_min[0]
    size_y = bounds_max[1] - bounds_min[1]
    size_z = bounds_max[2] - bounds_min[2]
    max_size = max(size_x, size_y, size_z)

    print(f"Model bounds: ({bounds_min[0]:.2f}, {bounds_min[1]:.2f}, {bounds_min[2]:.2f}) to ({bounds_max[0]:.2f}, {bounds_max[1]:.2f}, {bounds_max[2]:.2f})")
    print(f"Model size: {size_x:.2f} x {size_y:.2f} x {size_z:.2f}")
    print(f"Model center: ({center_x:.2f}, {center_y:.2f}, {center_z:.2f})")

    # Camera distance: place camera far enough to see the whole model
    # With narrower FOV (30°), need more distance
    cam_distance = max_size * 3.5

    print(f"Writing {len(triangles):,} triangles to {filename}...")

    with open(filename, 'w') as f:
        f.write("# RIB file generated from STL\n")
        f.write(f"# Source triangles: {len(triangles):,}\n\n")

        # Display/format setup - derive PNG name from RIB filename
        rib_basename = os.path.splitext(os.path.basename(filename))[0]
        f.write(f"Display \"{rib_basename}.png\" \"file\" \"rgba\"\n")
        f.write("Format 800 600 1\n")
        f.write("PixelSamples 2 2\n")
        f.write("ShadingRate 1\n\n")

        # Camera setup - narrower FOV for more isometric feel
        f.write("Projection \"perspective\" \"fov\" [30]\n\n")

        # Camera placement (before WorldBegin): look along (1,1,1) diagonal
        # Order matters: first rotate to aim camera, then translate back
        f.write("# Camera: isometric-style diagonal view\n")
        f.write("Rotate 45 0 1 0\n")   # First: turn 45° around Y for corner view
        f.write("Rotate 35 1 0 0\n")   # Second: tilt down ~35° (arctan(1/sqrt(2)))
        f.write(f"Translate 0 0 {cam_distance:.4f}\n\n")  # Third: back away from model

        f.write("WorldBegin\n\n")

        # Lighting
        f.write("  # Ambient light\n")
        f.write("  LightSource \"ambientlight\" \"intensity\" 0.3\n\n")

        f.write("  # Key light from upper right\n")
        f.write("  LightSource \"distantlight\" \"intensity\" 0.8 \"from\" [1 1 -1] \"to\" [0 0 0]\n\n")

        f.write("  # Fill light from left\n")
        f.write("  LightSource \"distantlight\" \"intensity\" 0.3 \"from\" [-1 0.5 -1] \"to\" [0 0 0]\n\n")

        # Surface shader
        f.write("  Surface \"matte\"\n")
        f.write("  Color [0.7 0.7 0.8]\n")
        f.write("  Sides 1\n\n")

        # Model transforms: scale first, then center (need scaled center since scale applied first)
        if scale != 1.0:
            f.write(f"  Scale {scale} {scale} {scale}\n")

        if center:
            f.write(f"  Translate {-center_x * scale:.6f} {-center_y * scale:.6f} {-center_z * scale:.6f}\n")

        f.write("\n")

        # Write triangles as Polygon primitives
        f.write("  # Triangles\n")
        for i, (normal, v0, v1, v2) in enumerate(triangles):
            f.write(f"  Polygon \"P\" [{v0[0]:.6f} {v0[1]:.6f} {v0[2]:.6f} {v1[0]:.6f} {v1[1]:.6f} {v1[2]:.6f} {v2[0]:.6f} {v2[1]:.6f} {v2[2]:.6f}]\n")

            if (i + 1) % 500000 == 0:
                print(f"  Wrote {i + 1:,} triangles...")

        f.write("\nWorldEnd\n")

    print(f"Done. Output: {filename}")


def main():
    parser = argparse.ArgumentParser(description='Convert binary STL to RIB format')
    parser.add_argument('input', help='Input STL file')
    parser.add_argument('output', help='Output RIB file')
    parser.add_argument('--scale', type=float, default=1.0, help='Scale factor')
    parser.add_argument('--center', action='store_true', help='Center model at origin')

    args = parser.parse_args()

    triangles = read_binary_stl(args.input)
    write_rib(args.output, triangles, scale=args.scale, center=args.center)


if __name__ == '__main__':
    main()
