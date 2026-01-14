#include "ri.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

// Draw a single quad on the XZ plane at height y
// Vertices ordered clockwise when viewed from above for correct normal (+Y)
static void draw_floor_quad(float x, float z, float size, float y) {
    RtPoint verts[4] = {
        {x,        y, z},
        {x,        y, z + size},
        {x + size, y, z + size},
        {x + size, y, z}
    };
    RiPolygon(4, "P", verts, RI_NULL);
}

int main(int argc, char** argv) {
    // Parse command line arguments
    int verbose = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        }
    }

    RiBegin(RI_NULL);

    // Enable statistics output if verbose flag is set
    if (verbose) {
        RtInt stats_on = 1;
        RiOption("statistics", "endofframe", &stats_on, RI_NULL);
    }

    RiDisplay("output.png", "file", "rgba", RI_NULL);
    RiFormat(800, 600, 1.0f);
    RiPixelSamples(4, 4);
    RiShadingRate(2.0);
    RiProjection("perspective", RI_NULL);

    // Setup Camera - looking at teapot from above and front
    RiTransformBegin();
        RiTranslate(0.0f, 2.0f, 8.0f);
        RiRotate(-15.0f, 1.0f, 0.0f, 0.0f);  // Tilt down slightly

        RiWorldBegin();

            // --- Light Sources ---
            // Key light from upper right
            RiTransformBegin();
                RtPoint light1_pos = {8.0f, 10.0f, 8.0f};
                RtFloat intensity1 = 1.2f;
                RiLightSource("pointlight", "from", light1_pos, "intensity", &intensity1, RI_NULL);
            RiTransformEnd();

            // Fill light from left
            RiTransformBegin();
                RtPoint light2_pos = {-6.0f, 5.0f, 4.0f};
                RtFloat intensity2 = 0.5f;
                RiLightSource("pointlight", "from", light2_pos, "intensity", &intensity2, RI_NULL);
            RiTransformEnd();

            // --- Checkerboard Floor ---
            {
                int board_size = 8;
                float square_size = 1.0f;
                float floor_y = 0.0f;
                float offset = -(board_size * square_size) / 2.0f;

                for (int i = 0; i < board_size; i++) {
                    for (int j = 0; j < board_size; j++) {
                        RiTransformBegin();
                            // Alternate colors: green and red
                            if ((i + j) % 2 == 0) {
                                RiColor((RtColor){0.1f, 0.7f, 0.1f});  // Green
                            } else {
                                RiColor((RtColor){0.8f, 0.1f, 0.1f});  // Red
                            }

                            float x = offset + i * square_size;
                            float z = offset + j * square_size;
                            draw_floor_quad(x, z, square_size, floor_y);
                        RiTransformEnd();
                    }
                }
            }

            // --- Teapot ---
            RiTransformBegin();
                // Position teapot on the floor, centered
                RiTranslate(0.0f, 0.0f, 0.0f);
                RiRotate(-90.0f, 1.0f, 0.0f, 0.0f);  // Stand upright (teapot data is Z-up)
                RiScale(0.4f, 0.4f, 0.4f);          // Scale down to fit nicely on floor

                // Copper/orange color for teapot
                RiColor((RtColor){0.9f, 0.5f, 0.2f});
                RiGeometry("teapot", RI_NULL);
            RiTransformEnd();

            // --- Diagnostic Shader Demo: Spheres ---
            // Sphere with randomgrid shader (each grid gets a unique color)
            RiTransformBegin();
                RiTranslate(-2.5f, 1.0f, -2.0f);
                RiSurface("randomgrid", RI_NULL);
                RiSphere(0.8f, -0.8f, 0.8f, 360.0f, RI_NULL);
            RiTransformEnd();

            // Sphere with random shader (each micropolygon gets a unique color)
            RiTransformBegin();
                RiTranslate(2.5f, 1.0f, -2.0f);
                RiSurface("random", RI_NULL);
                RiSphere(0.8f, -0.8f, 0.8f, 360.0f, RI_NULL);
            RiTransformEnd();

        RiWorldEnd();
    RiTransformEnd();

    RiEnd();

    printf("Teapot on checkerboard rendered to output.png\n");
    return 0;
}
