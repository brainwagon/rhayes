#!/usr/bin/env python3
"""Debug shadow map matrices and test point lookups."""

import struct
import sys
import numpy as np

def read_shadowmap(filename):
    with open(filename, 'rb') as f:
        magic, version = struct.unpack('II', f.read(8))
        width, height = struct.unpack('ii', f.read(8))
        near_clip, far_clip = struct.unpack('ff', f.read(8))

        # Read world_to_light_ndc matrix (4x4, row-major)
        ndc_matrix = np.array(struct.unpack('16f', f.read(64))).reshape(4, 4)

        # Read world_to_light_camera matrix (4x4, row-major)
        cam_matrix = np.array(struct.unpack('16f', f.read(64))).reshape(4, 4)

        # Read depths
        depths = np.array(struct.unpack(f'{width*height}f', f.read(width*height*4))).reshape(height, width)

    return {
        'width': width,
        'height': height,
        'near_clip': near_clip,
        'far_clip': far_clip,
        'world_to_light_ndc': ndc_matrix,
        'world_to_light_camera': cam_matrix,
        'depths': depths
    }

def transform_point(matrix, point):
    """Transform a 3D point by a 4x4 matrix."""
    p = np.array([point[0], point[1], point[2], 1.0])
    result = matrix @ p
    if abs(result[3]) > 1e-6:
        return result[:3] / result[3]
    return result[:3]

def lookup_shadow(sm, world_pos, bias=0.05):
    """Perform a shadow lookup for a world position."""
    # Transform to light NDC space
    light_ndc = transform_point(sm['world_to_light_ndc'], world_pos)

    # Transform to light camera space
    light_cam = transform_point(sm['world_to_light_camera'], world_pos)

    print(f"  World pos: {world_pos}")
    print(f"  Light NDC: ({light_ndc[0]:.4f}, {light_ndc[1]:.4f}, {light_ndc[2]:.4f})")
    print(f"  Light cam: ({light_cam[0]:.4f}, {light_cam[1]:.4f}, {light_cam[2]:.4f})")

    # Check frustum
    if light_ndc[0] < -1 or light_ndc[0] > 1 or light_ndc[1] < -1 or light_ndc[1] > 1:
        print(f"  -> Outside frustum (NDC out of range)")
        return 0.0

    if light_cam[2] < sm['near_clip']:
        print(f"  -> Outside frustum (behind near clip {sm['near_clip']})")
        return 0.0

    # Convert NDC to UV (Y is flipped: NDC y=+1 is top, raster y=0 is top)
    u = (light_ndc[0] + 1.0) * 0.5
    v = (1.0 - light_ndc[1]) * 0.5

    # Sample shadow map
    px = int(u * sm['width'])
    py = int(v * sm['height'])
    px = max(0, min(px, sm['width'] - 1))
    py = max(0, min(py, sm['height'] - 1))

    shadow_z = sm['depths'][py, px]
    surface_z = light_cam[2]

    print(f"  UV: ({u:.4f}, {v:.4f}) -> pixel ({px}, {py})")
    print(f"  Shadow map z: {shadow_z:.4f}")
    print(f"  Surface z:    {surface_z:.4f}")
    print(f"  Bias:         {bias:.4f}")
    print(f"  Comparison:   surface_z ({surface_z:.4f}) > shadow_z + bias ({shadow_z + bias:.4f})?")

    if surface_z > shadow_z + bias:
        print(f"  -> IN SHADOW")
        return 1.0
    else:
        print(f"  -> LIT")
        return 0.0

def main():
    if len(sys.argv) < 2:
        print("Usage: debug_shadowmap.py <shadowmap.shd>")
        sys.exit(1)

    sm = read_shadowmap(sys.argv[1])

    print(f"Shadow map: {sm['width']}x{sm['height']}")
    print(f"Near/far clip: {sm['near_clip']:.4f} / {sm['far_clip']:.4f}")
    print()

    print("world_to_light_ndc matrix:")
    print(sm['world_to_light_ndc'])
    print()

    print("world_to_light_camera matrix:")
    print(sm['world_to_light_camera'])
    print()

    # Compute camera position from inverse of view matrix
    try:
        cam_inv = np.linalg.inv(sm['world_to_light_camera'])
        cam_pos = cam_inv @ np.array([0, 0, 0, 1])
        print(f"Camera position (from inverse): ({cam_pos[0]:.4f}, {cam_pos[1]:.4f}, {cam_pos[2]:.4f})")
    except:
        print("Could not invert camera matrix")
    print()

    # Depth statistics
    valid_depths = sm['depths'][sm['depths'] < 1e20]
    if len(valid_depths) > 0:
        print(f"Depth range: {valid_depths.min():.4f} to {valid_depths.max():.4f}")
    print()

    # Test some world positions
    print("=" * 60)
    print("Testing shadow lookups:")
    print("=" * 60)

    # Origin (should be on the floor at y=-1.5)
    print("\n1. Origin (0, 0, 0) - should be above floor:")
    lookup_shadow(sm, [0, 0, 0])

    # Floor center
    print("\n2. Floor center (0, -1.5, 0):")
    lookup_shadow(sm, [0, -1.5, 0])

    # Far corner of floor (should be in shadow map but far)
    print("\n3. Far floor corner (2, -1.5, 2):")
    lookup_shadow(sm, [2, -1.5, 2])

    # Near corner of floor
    print("\n4. Near floor corner (-2, -1.5, -2):")
    lookup_shadow(sm, [-2, -1.5, -2])

    # Top of teapot (approximately)
    print("\n5. Teapot top (0, 0.5, 0) - should be lit:")
    lookup_shadow(sm, [0, 0.5, 0])

    # Under the teapot (should be in shadow)
    print("\n6. Under teapot (0, -1.5, 0.5) - should be shadowed:")
    lookup_shadow(sm, [0, -1.5, 0.5])

if __name__ == '__main__':
    main()
