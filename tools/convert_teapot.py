import sys

def parse_teapot(data):
    tokens = data.split()
    iterator = iter(tokens)
    
    try:
        num_patches = int(next(iterator))
        print(f"// Generated from teapotCGAnobottom.bpt")
        print(f"#ifndef TEAPOT_DATA_H")
        print(f"#define TEAPOT_DATA_H")
        print(f"")
        print(f"#define TEAPOT_NUM_PATCHES {num_patches}")
        print(f"")
        print(f"static float teapot_patches[{num_patches}][16][3] = {{")
        
        for i in range(num_patches):
            u_deg = int(next(iterator))
            v_deg = int(next(iterator))
            
            if u_deg != 3 or v_deg != 3:
                sys.stderr.write(f"Warning: Patch {i} has degree {u_deg}x{v_deg}, expected 3x3\n")
            
            print(f"    {{ // Patch {i}")
            for j in range(16):
                x = float(next(iterator))
                y = float(next(iterator))
                z = float(next(iterator))
                print(f"        {{{x}f, {y}f, {z}f}},")
            print(f"    }},")
            
        print(f"}};")
        print(f"")
        print(f"#endif // TEAPOT_DATA_H")
        
    except StopIteration:
        pass

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python convert_teapot.py <input_file>")
        sys.exit(1)
        
    with open(sys.argv[1], 'r') as f:
        data = f.read()
        parse_teapot(data)
