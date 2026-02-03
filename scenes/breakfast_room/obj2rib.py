#!/usr/bin/env python3
"""Convert OBJ/MTL files to RenderMan RIB format for rhayes renderer."""

import argparse
import math
import os
import re
import sys
from dataclasses import dataclass, field
from typing import Optional


@dataclass
class Material:
    """Parsed MTL material."""
    name: str
    Kd: tuple = (0.5, 0.5, 0.5)  # diffuse color
    Ks: tuple = (0.0, 0.0, 0.0)  # specular color
    Ns: float = 1.0              # shininess
    map_Kd: Optional[str] = None # diffuse texture


@dataclass
class ObjData:
    """Parsed OBJ file data."""
    vertices: list = field(default_factory=list)     # [(x, y, z), ...]
    texcoords: list = field(default_factory=list)    # [(u, v), ...]
    normals: list = field(default_factory=list)      # [(nx, ny, nz), ...]
    faces_by_material: dict = field(default_factory=dict)  # material -> [faces]
    # Each face is [(v_idx, vt_idx, vn_idx), ...]


def parse_mtl(filename: str) -> dict:
    """Parse MTL file into dict of materials."""
    materials = {}
    current = None

    with open(filename, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue

            parts = line.split()
            cmd = parts[0]

            if cmd == 'newmtl':
                name = parts[1] if len(parts) > 1 else 'default'
                current = Material(name=name)
                materials[name] = current
            elif current is None:
                continue
            elif cmd == 'Kd' and len(parts) >= 4:
                current.Kd = (float(parts[1]), float(parts[2]), float(parts[3]))
            elif cmd == 'Ks' and len(parts) >= 4:
                current.Ks = (float(parts[1]), float(parts[2]), float(parts[3]))
            elif cmd == 'Ns' and len(parts) >= 2:
                current.Ns = float(parts[1])
            elif cmd == 'map_Kd' and len(parts) >= 2:
                # Get texture filename, convert jpg to png
                tex = parts[1]
                if tex.lower().endswith('.jpg') or tex.lower().endswith('.jpeg'):
                    tex = os.path.splitext(tex)[0] + '.png'
                current.map_Kd = tex

    return materials


def parse_obj(filename: str) -> ObjData:
    """Parse OBJ file, returning vertices, texcoords, normals, and faces by material."""
    data = ObjData()
    current_material = 'default'

    with open(filename, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue

            parts = line.split()
            cmd = parts[0]

            if cmd == 'v' and len(parts) >= 4:
                data.vertices.append((float(parts[1]), float(parts[2]), float(parts[3])))
            elif cmd == 'vt' and len(parts) >= 3:
                data.texcoords.append((float(parts[1]), float(parts[2])))
            elif cmd == 'vn' and len(parts) >= 4:
                data.normals.append((float(parts[1]), float(parts[2]), float(parts[3])))
            elif cmd == 'usemtl':
                current_material = parts[1] if len(parts) > 1 else 'default'
                if current_material not in data.faces_by_material:
                    data.faces_by_material[current_material] = []
            elif cmd == 'f':
                # Parse face: v/vt/vn or v//vn or v/vt or v
                face = []
                for vert in parts[1:]:
                    indices = vert.split('/')
                    v_idx = int(indices[0]) - 1  # OBJ is 1-indexed
                    vt_idx = int(indices[1]) - 1 if len(indices) > 1 and indices[1] else None
                    vn_idx = int(indices[2]) - 1 if len(indices) > 2 and indices[2] else None
                    face.append((v_idx, vt_idx, vn_idx))

                if current_material not in data.faces_by_material:
                    data.faces_by_material[current_material] = []
                data.faces_by_material[current_material].append(face)

    return data


def compute_bounds(vertices: list) -> tuple:
    """Compute bounding box: ((min_x, min_y, min_z), (max_x, max_y, max_z))."""
    if not vertices:
        return ((0, 0, 0), (1, 1, 1))

    min_x = min(v[0] for v in vertices)
    min_y = min(v[1] for v in vertices)
    min_z = min(v[2] for v in vertices)
    max_x = max(v[0] for v in vertices)
    max_y = max(v[1] for v in vertices)
    max_z = max(v[2] for v in vertices)

    return ((min_x, min_y, min_z), (max_x, max_y, max_z))


def map_material_to_shader(material: Material) -> tuple:
    """
    Map MTL material to rhayes shader.
    Returns (shader_name, shader_params, color).
    """
    # Calculate average specular intensity
    ks_avg = (material.Ks[0] + material.Ks[1] + material.Ks[2]) / 3.0

    if material.map_Kd:
        # Textured material
        return ('paintedplastic', {'texturename': material.map_Kd}, (1.0, 1.0, 1.0))
    elif ks_avg > 0.5:
        # Highly specular - use metal
        return ('metal', {}, material.Kd if sum(material.Kd) > 0.1 else material.Ks)
    elif ks_avg > 0:
        # Some specularity - use plastic
        return ('plastic', {}, material.Kd)
    else:
        # No specularity - use matte
        return ('matte', {}, material.Kd)


def write_rib(output_path: str, obj_data: ObjData, materials: dict,
              width: int = 800, height: int = 600, fov: float = 45.0,
              shading_rate: float = 1.0, pixel_samples: int = 2,
              center: bool = True):
    """Write RIB file from parsed OBJ data."""

    # Compute scene bounds and center
    bounds = compute_bounds(obj_data.vertices)
    min_pt, max_pt = bounds
    center_x = (min_pt[0] + max_pt[0]) / 2
    center_y = (min_pt[1] + max_pt[1]) / 2
    center_z = (min_pt[2] + max_pt[2]) / 2

    # Scene size for camera placement
    size_x = max_pt[0] - min_pt[0]
    size_y = max_pt[1] - min_pt[1]
    size_z = max_pt[2] - min_pt[2]

    output_name = os.path.splitext(os.path.basename(output_path))[0] + '.png'

    with open(output_path, 'w') as f:
        # Header
        f.write(f'# RIB file generated from OBJ by obj2rib.py\n')
        f.write(f'# Vertices: {len(obj_data.vertices)}\n')
        f.write(f'# Faces: {sum(len(faces) for faces in obj_data.faces_by_material.values())}\n')
        f.write(f'# Materials: {len(obj_data.faces_by_material)}\n\n')

        # Display settings
        f.write(f'Display "{output_name}" "file" "rgba"\n')
        f.write(f'Format {width} {height} 1\n')
        f.write(f'PixelSamples {pixel_samples} {pixel_samples}\n')
        f.write(f'ShadingRate {shading_rate}\n\n')

        # Camera setup
        f.write(f'Projection "perspective" "fov" [{fov}]\n')

        # Position camera INSIDE the room (matching preview.html)
        # In preview.html, model is centered at origin with these camera coords:
        #   position: (size_x * 0.3, -size_y * 0.2, size_z * 0.3)
        #   lookAt:   (-size_x * 0.1, -size_y * 0.1, -size_z * 0.2)
        # Convert to world coordinates by adding center
        eye_level_centered = -size_y * 0.2
        cam_x = center_x + size_x * 0.3
        cam_y = center_y + eye_level_centered
        cam_z = center_z + size_z * 0.3

        look_x = center_x - size_x * 0.1
        look_y = center_y + eye_level_centered * 0.5
        look_z = center_z - size_z * 0.2

        # Compute look-at rotation
        # Direction from camera to target
        dir_x = look_x - cam_x
        dir_y = look_y - cam_y
        dir_z = look_z - cam_z

        # Yaw: rotation around Y to align with horizontal direction
        yaw = math.degrees(math.atan2(-dir_x, -dir_z))

        # Pitch: rotation around X to tilt up/down
        horizontal = math.sqrt(dir_x * dir_x + dir_z * dir_z)
        pitch = math.degrees(math.atan2(dir_y, horizontal))

        # RenderMan transforms: written in reverse order of application
        # First rotate (to look at target), then translate (to position)
        f.write(f'Rotate {-pitch:.6f} 1 0 0\n')
        f.write(f'Rotate {yaw:.6f} 0 1 0\n')
        f.write(f'Translate {-cam_x:.6f} {-cam_y:.6f} {-cam_z:.6f}\n')
        f.write('\n')

        # World
        f.write('WorldBegin\n')

        # Lighting
        f.write('  LightSource "ambientlight" "intensity" [0.3]\n')
        f.write('  LightSource "distantlight" "intensity" [0.7] "from" [1 1 1] "to" [0 0 0]\n')
        f.write('\n')

        # Output polygons grouped by material
        for mat_name, faces in obj_data.faces_by_material.items():
            if not faces:
                continue

            # Get material properties
            material = materials.get(mat_name)
            if material is None:
                # Try without prefix
                short_name = mat_name.split(':')[-1] if ':' in mat_name else mat_name
                material = materials.get(short_name)
            if material is None:
                # Default material
                material = Material(name=mat_name, Kd=(0.5, 0.5, 0.5), Ks=(0.0, 0.0, 0.0))

            shader_name, shader_params, color = map_material_to_shader(material)

            f.write(f'  # Material: {mat_name} ({len(faces)} faces)\n')
            f.write('  AttributeBegin\n')

            # Set color
            f.write(f'    Color [{color[0]:.4f} {color[1]:.4f} {color[2]:.4f}]\n')

            # Set shader
            if shader_params:
                params_str = ' '.join(f'"{k}" "{v}"' for k, v in shader_params.items())
                f.write(f'    Surface "{shader_name}" {params_str}\n')
            else:
                f.write(f'    Surface "{shader_name}"\n')

            # Write polygons
            for face in faces:
                # Collect vertex positions, normals, and texcoords
                positions = []
                normals = []
                texcoords = []
                has_texcoords = material.map_Kd is not None
                has_normals = len(obj_data.normals) > 0

                for v_idx, vt_idx, vn_idx in face:
                    if 0 <= v_idx < len(obj_data.vertices):
                        v = obj_data.vertices[v_idx]
                        positions.extend([v[0], v[1], v[2]])

                    if has_normals and vn_idx is not None and 0 <= vn_idx < len(obj_data.normals):
                        vn = obj_data.normals[vn_idx]
                        normals.extend([vn[0], vn[1], vn[2]])

                    if has_texcoords and vt_idx is not None and 0 <= vt_idx < len(obj_data.texcoords):
                        vt = obj_data.texcoords[vt_idx]
                        texcoords.extend([vt[0], vt[1]])

                if len(positions) < 9:  # Need at least 3 vertices
                    continue

                # Format position array
                pos_str = ' '.join(f'{p:.6f}' for p in positions)

                # Build parameter string
                params = [f'"P" [{pos_str}]']

                # Add normals if available (for smooth shading)
                if has_normals and len(normals) == len(positions):
                    norm_str = ' '.join(f'{n:.6f}' for n in normals)
                    params.append(f'"N" [{norm_str}]')

                # Add texture coords if available
                if has_texcoords and len(texcoords) >= 6:
                    tex_str = ' '.join(f'{t:.6f}' for t in texcoords)
                    params.append(f'"st" [{tex_str}]')

                f.write(f'    Polygon {" ".join(params)}\n')

            f.write('  AttributeEnd\n\n')

        f.write('WorldEnd\n')


def main():
    parser = argparse.ArgumentParser(description='Convert OBJ/MTL to RenderMan RIB')
    parser.add_argument('input_obj', help='Input OBJ file')
    parser.add_argument('output_rib', help='Output RIB file')
    parser.add_argument('--width', type=int, default=800, help='Output width (default: 800)')
    parser.add_argument('--height', type=int, default=600, help='Output height (default: 600)')
    parser.add_argument('--fov', type=float, default=45.0, help='Field of view (default: 45)')
    parser.add_argument('--shading-rate', type=float, default=1.0, help='Shading rate (default: 1.0)')
    parser.add_argument('--pixel-samples', type=int, default=2, help='Pixel samples (default: 2)')
    parser.add_argument('--center', action='store_true', help='Center scene (default behavior)')
    parser.add_argument('-v', '--verbose', action='store_true', help='Verbose output')

    args = parser.parse_args()

    if not os.path.exists(args.input_obj):
        print(f'Error: Input file not found: {args.input_obj}', file=sys.stderr)
        sys.exit(1)

    # Find MTL file
    obj_dir = os.path.dirname(args.input_obj) or '.'
    mtl_path = None
    with open(args.input_obj, 'r') as f:
        for line in f:
            if line.startswith('mtllib'):
                mtl_name = line.split()[1]
                mtl_path = os.path.join(obj_dir, mtl_name)
                break

    if args.verbose:
        print(f'Parsing OBJ: {args.input_obj}')

    obj_data = parse_obj(args.input_obj)

    if args.verbose:
        print(f'  Vertices: {len(obj_data.vertices)}')
        print(f'  Texture coords: {len(obj_data.texcoords)}')
        print(f'  Normals: {len(obj_data.normals)}')
        total_faces = sum(len(f) for f in obj_data.faces_by_material.values())
        print(f'  Faces: {total_faces}')
        print(f'  Materials used: {len(obj_data.faces_by_material)}')

    materials = {}
    if mtl_path and os.path.exists(mtl_path):
        if args.verbose:
            print(f'Parsing MTL: {mtl_path}')
        materials = parse_mtl(mtl_path)
        if args.verbose:
            print(f'  Materials defined: {len(materials)}')

    if args.verbose:
        bounds = compute_bounds(obj_data.vertices)
        print(f'Scene bounds: {bounds[0]} to {bounds[1]}')
        print(f'Writing RIB: {args.output_rib}')

    write_rib(
        args.output_rib,
        obj_data,
        materials,
        width=args.width,
        height=args.height,
        fov=args.fov,
        shading_rate=args.shading_rate,
        pixel_samples=args.pixel_samples,
        center=True
    )

    if args.verbose:
        print('Done!')


if __name__ == '__main__':
    main()
