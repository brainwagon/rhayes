#include "rib_output.h"
#include "ri_callbacks.h"
#include "ri.h"
#include <stdio.h>
#include <string.h>

// Draw a single quad on the XZ plane at height y
// Vertices ordered clockwise when viewed from above for correct normal (+Y)
static void draw_floor_quad(RiCallbacks* cb, float x, float z, float size, float y) {
    RtPoint verts[4] = {
        {x,        y, z},
        {x,        y, z + size},
        {x + size, y, z + size},
        {x + size, y, z}
    };
    RtToken tokens[] = {"P"};
    RtPointer values[] = {verts};
    cb->Polygon(4, tokens, values, 1);
}

int main(int argc, char** argv) {
    // Parse command line arguments
    const char* output_file = "scene.rib";
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            output_file = argv[i];
        }
    }

    // Initialize RIB output
    if (rib_output_begin(output_file, 0) != 0) {
        fprintf(stderr, "Failed to open %s for writing\n", output_file);
        return 1;
    }

    RiCallbacks* cb = rib_output_get_callbacks();

    cb->Begin(RI_NULL);

    cb->Display("output.png", "file", "rgb");
    cb->Format(800, 600, 1.0f);
    cb->PixelSamples(4.0f, 4.0f);
    cb->ShadingRate(1.0f);
    cb->Projection("perspective", NULL, NULL, 0);

    // Setup Camera - looking at teapot from above and front
    cb->TransformBegin();
        cb->Translate(0.0f, 2.0f, 8.0f);
        cb->Rotate(-15.0f, 1.0f, 0.0f, 0.0f);  // Tilt down slightly

        cb->WorldBegin();

            // --- Light Sources ---
            // Key light from upper right
            cb->TransformBegin();
            {
                RtPoint light1_pos = {8.0f, 10.0f, 8.0f};
                RtFloat intensity1 = 1.2f;
                RtToken tokens[] = {"from", "intensity"};
                RtPointer values[] = {light1_pos, &intensity1};
                cb->LightSource("pointlight", tokens, values, 2);
            }
            cb->TransformEnd();

            // Fill light from left
            cb->TransformBegin();
            {
                RtPoint light2_pos = {-6.0f, 5.0f, 4.0f};
                RtFloat intensity2 = 0.5f;
                RtToken tokens[] = {"from", "intensity"};
                RtPointer values[] = {light2_pos, &intensity2};
                cb->LightSource("pointlight", tokens, values, 2);
            }
            cb->TransformEnd();

            // --- Checkerboard Floor ---
            {
                int board_size = 8;
                float square_size = 1.0f;
                float floor_y = 0.0f;
                float offset = -(board_size * square_size) / 2.0f;

                for (int i = 0; i < board_size; i++) {
                    for (int j = 0; j < board_size; j++) {
                        cb->TransformBegin();
                            // Alternate colors: green and red
                            if ((i + j) % 2 == 0) {
                                cb->Color((RtColor){0.1f, 0.7f, 0.1f});  // Green
                            } else {
                                cb->Color((RtColor){0.8f, 0.1f, 0.1f});  // Red
                            }

                            float xpos = offset + i * square_size;
                            float zpos = offset + j * square_size;
                            draw_floor_quad(cb, xpos, zpos, square_size, floor_y);
                        cb->TransformEnd();
                    }
                }
            }

            // --- Teapot ---
            cb->TransformBegin();
                // Position teapot on the floor, centered
                cb->Translate(0.0f, 0.0f, 0.0f);
                cb->Rotate(-90.0f, 1.0f, 0.0f, 0.0f);  // Stand upright (teapot data is Z-up)
                cb->Scale(0.4f, 0.4f, 0.4f);          // Scale down to fit nicely on floor

                // Copper/orange color for teapot
                cb->Color((RtColor){0.9f, 0.5f, 0.2f});
                cb->Scale(1.0f, 1.0f, 4.0f/3.0f);
                cb->Geometry("teapot", NULL, NULL, 0);
            cb->TransformEnd();

            // --- Shader Demo: Spheres ---
            // Sphere with paintedplastic shader and texture
            cb->TransformBegin();
            {
                cb->Translate(-2.5f, 1.0f, -2.0f);
                cb->Color((RtColor){1.0f, 1.0f, 1.0f});  // White base (texture provides color)
                RtToken tokens[] = {"texturename"};
                RtPointer values[] = {"textures/uvgrid.png"};
                cb->Surface("paintedplastic", tokens, values, 1);
                cb->Sphere(0.8f, -0.8f, 0.8f, 360.0f, NULL, NULL, 0);
            }
            cb->TransformEnd();

            // Sphere with randomgrid shader (diagnostic)
            cb->TransformBegin();
                cb->Translate(2.5f, 1.0f, -2.0f);
                cb->Surface("randomgrid", NULL, NULL, 0);
                cb->Sphere(0.8f, -0.8f, 0.8f, 360.0f, NULL, NULL, 0);
            cb->TransformEnd();

        cb->WorldEnd();
    cb->TransformEnd();

    cb->End();
    rib_output_end();

    printf("Scene written to %s\n", output_file);
    return 0;
}
