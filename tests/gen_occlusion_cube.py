#!/usr/bin/env python3
import sys

def main():
    print('# Occlusion Culling Test: 20x20x20 Cube of Bricks')
    print('Display "occlusion_cube.png" "file" "rgba"')
    print('Format 640 480 1')
    print('PixelSamples 1 1') # Keep samples low for speed
    print('Projection "perspective" "fov" [40]')
    print('Option "statistics" "endofframe" [2]') # Enable stats to see culling
    
    # Setup camera view
    print('Translate 0 0 80') 
    print('Rotate -30 1 0 0') # Tilt down
    print('Rotate 45 0 1 0')  # Rotate side
    
    # Center the 20x20x20 cube which goes from 0 to 20
    print('Translate -10 -10 -10')

    print('WorldBegin')
    
    # Lighting (simple - not needed for constant shader but good practice)
    print('LightSource "ambientlight" "intensity" [1]')
    
    # Shader
    print('Surface "constant"')
    
    size = 20
    
    # Unit cube faces (CCW winding)
    faces = [
        # Front (Z=1)
        [0, 0, 1,  1, 0, 1,  1, 1, 1,  0, 1, 1],
        # Back (Z=0)
        [0, 0, 0,  0, 1, 0,  1, 1, 0,  1, 0, 0], 
        # Left (X=0)
        [0, 0, 0,  0, 0, 1,  0, 1, 1,  0, 1, 0],
        # Right (X=1)
        [1, 0, 0,  1, 1, 0,  1, 1, 1,  1, 0, 1],
        # Top (Y=1)
        [0, 1, 0,  0, 1, 1,  1, 1, 1,  1, 1, 0],
        # Bottom (Y=0)
        [0, 0, 0,  1, 0, 0,  1, 0, 1,  0, 0, 1]
    ]
    
    for x in range(size):
        for y in range(size):
            for z in range(size):
                r = x / float(size - 1)
                g = y / float(size - 1)
                b = z / float(size - 1)
                
                print(f'AttributeBegin')
                print(f'Color [{r:.3f} {g:.3f} {b:.3f}]')
                print(f'Translate {x} {y} {z}')
                
                for face in faces:
                    pts = " ".join(str(v) for v in face)
                    print(f'Polygon "P" [{pts}]')
                
                print(f'AttributeEnd')

    print('WorldEnd')

if __name__ == "__main__":
    main()
