/**
 * rh_shadow.c - Shadow map support for percentage closer filtering (PCF)
 *
 * Implements the shadow algorithm from "Rendering Antialiased Shadows with
 * Depth Maps" by Reeves, Salesin, and Cook.
 *
 * Shadow maps store camera-space z (world units) for intuitive bias values.
 */

#include "rh_shadow.h"
#include "xpt.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

// Simple random number generator for jittered sampling
static unsigned int pcf_seed = 12345;

static float pcf_random(void) {
    pcf_seed = pcf_seed * 1103515245 + 12345;
    return (float)(pcf_seed & 0x7FFFFFFF) / (float)0x7FFFFFFF;
}

// --- Shadow Map Creation/Destruction ---

RhShadowMap* rh_shadowmap_create(int width, int height) {
    RhShadowMap* sm = (RhShadowMap*)malloc(sizeof(RhShadowMap));
    if (!sm) return NULL;

    sm->width = width;
    sm->height = height;
    sm->near_clip = 0.1f;
    sm->far_clip = 1000.0f;
    sm->world_to_light_ndc = rh_mat4_identity();
    sm->world_to_light_camera = rh_mat4_identity();

    sm->depths = (float*)malloc(width * height * sizeof(float));
    if (!sm->depths) {
        free(sm);
        return NULL;
    }

    // Initialize depths to very far (large positive z in camera space)
    for (int i = 0; i < width * height; i++) {
        sm->depths[i] = 1e30f;
    }

    return sm;
}

void rh_shadowmap_destroy(RhShadowMap* sm) {
    if (sm) {
        free(sm->depths);
        free(sm);
    }
}

// --- Shadow Map File I/O ---

bool rh_shadowmap_write(const char* filename, const RhShadowMap* sm) {
    if (!filename || !sm || !sm->depths) return false;

    FILE* f = fopen(filename, "wb");
    if (!f) {
        xpt_error("rh.shadow", "Cannot open shadow map file for writing: %s", filename);
        return false;
    }

    // Write header
    unsigned int magic = RH_SHADOW_MAGIC;
    unsigned int version = RH_SHADOW_VERSION;

    fwrite(&magic, sizeof(unsigned int), 1, f);
    fwrite(&version, sizeof(unsigned int), 1, f);
    fwrite(&sm->width, sizeof(int), 1, f);
    fwrite(&sm->height, sizeof(int), 1, f);
    fwrite(&sm->near_clip, sizeof(float), 1, f);
    fwrite(&sm->far_clip, sizeof(float), 1, f);

    // Write world-to-light-NDC matrix (for UV lookup)
    for (int i = 0; i < 4; i++) {
        fwrite(sm->world_to_light_ndc.m[i], sizeof(float), 4, f);
    }

    // Write world-to-light-camera matrix (for z comparison)
    for (int i = 0; i < 4; i++) {
        fwrite(sm->world_to_light_camera.m[i], sizeof(float), 4, f);
    }

    // Write depth buffer (camera-space z values)
    fwrite(sm->depths, sizeof(float), sm->width * sm->height, f);

    fclose(f);
    return true;
}

RhShadowMap* rh_shadowmap_read(const char* filename) {
    if (!filename) return NULL;

    FILE* f = fopen(filename, "rb");
    if (!f) {
        xpt_error("rh.shadow", "Cannot open shadow map file for reading: %s", filename);
        return NULL;
    }

    // Read and verify header
    unsigned int magic, version;
    int width, height;
    float near_clip, far_clip;

    if (fread(&magic, sizeof(unsigned int), 1, f) != 1 ||
        fread(&version, sizeof(unsigned int), 1, f) != 1 ||
        fread(&width, sizeof(int), 1, f) != 1 ||
        fread(&height, sizeof(int), 1, f) != 1 ||
        fread(&near_clip, sizeof(float), 1, f) != 1 ||
        fread(&far_clip, sizeof(float), 1, f) != 1) {
        xpt_error("rh.shadow", "Failed to read shadow map header: %s", filename);
        fclose(f);
        return NULL;
    }

    if (magic != RH_SHADOW_MAGIC) {
        xpt_error("rh.shadow", "Invalid shadow map magic number: %s", filename);
        fclose(f);
        return NULL;
    }

    if (version != RH_SHADOW_VERSION) {
        xpt_error("rh.shadow", "Unsupported shadow map version %u (expected %u): %s",
                  version, RH_SHADOW_VERSION, filename);
        fclose(f);
        return NULL;
    }

    // Create shadow map
    RhShadowMap* sm = rh_shadowmap_create(width, height);
    if (!sm) {
        fclose(f);
        return NULL;
    }

    sm->near_clip = near_clip;
    sm->far_clip = far_clip;

    // Read world-to-light-NDC matrix
    for (int i = 0; i < 4; i++) {
        if (fread(sm->world_to_light_ndc.m[i], sizeof(float), 4, f) != 4) {
            xpt_error("rh.shadow", "Failed to read shadow map NDC matrix: %s", filename);
            rh_shadowmap_destroy(sm);
            fclose(f);
            return NULL;
        }
    }

    // Read world-to-light-camera matrix
    for (int i = 0; i < 4; i++) {
        if (fread(sm->world_to_light_camera.m[i], sizeof(float), 4, f) != 4) {
            xpt_error("rh.shadow", "Failed to read shadow map camera matrix: %s", filename);
            rh_shadowmap_destroy(sm);
            fclose(f);
            return NULL;
        }
    }

    // Read depth buffer
    if (fread(sm->depths, sizeof(float), width * height, f) != (size_t)(width * height)) {
        xpt_error("rh.shadow", "Failed to read shadow map depths: %s", filename);
        rh_shadowmap_destroy(sm);
        fclose(f);
        return NULL;
    }

    fclose(f);
    return sm;
}

// --- Frustum Check ---

bool rh_shadow_in_frustum(const RhShadowMap* sm, RhVec3 world_pos) {
    if (!sm) return false;

    // Transform world position to light NDC space
    RhVec3 light_ndc = rh_mat4_mul_point(sm->world_to_light_ndc, world_pos);

    // Check if within NDC cube [-1, 1] in x and y
    // For z, check camera space is in front of light
    const float eps = 0.001f;

    if (light_ndc.x < -1.0f - eps || light_ndc.x > 1.0f + eps) return false;
    if (light_ndc.y < -1.0f - eps || light_ndc.y > 1.0f + eps) return false;

    // Also check camera-space z is positive (in front of light)
    RhVec3 light_cam = rh_mat4_mul_point(sm->world_to_light_camera, world_pos);
    if (light_cam.z < sm->near_clip) return false;

    return true;
}

// --- PCF Shadow Lookup ---

float rh_shadow_pcf_lookup(
    const RhShadowMap* sm,
    RhVec3 world_pos,
    int num_samples,
    float bias,
    float filter_size)
{
    if (!sm || !sm->depths || num_samples <= 0) return 0.0f;

    // Transform world position to light NDC space (for UV lookup)
    RhVec3 light_ndc = rh_mat4_mul_point(sm->world_to_light_ndc, world_pos);

    // Transform world position to light camera space (for z comparison)
    RhVec3 light_cam = rh_mat4_mul_point(sm->world_to_light_camera, world_pos);

    // Check if within frustum
    if (light_ndc.x < -1.0f || light_ndc.x > 1.0f ||
        light_ndc.y < -1.0f || light_ndc.y > 1.0f ||
        light_cam.z < sm->near_clip) {
        // Outside frustum - treat as fully lit (no shadow information available)
        {
            static int frustum_reject = 0, frustum_ground = 0;
            frustum_reject++;
            if (world_pos.y < -0.3f) frustum_ground++;
            if (frustum_reject % 50000 == 0)
                xpt_debug("rh.shadow", "FRUSTUM REJECT: total=%d ground=%d", frustum_reject, frustum_ground);
        }
        return 0.0f;
    }

    // Convert NDC to shadow map UV coordinates [0, 1]
    // Note: Y is flipped because NDC y=+1 is top but raster y=0 is top
    float u = (light_ndc.x + 1.0f) * 0.5f;
    float v = (1.0f - light_ndc.y) * 0.5f;

    // Surface depth in light camera space (world units)
    float surface_z = light_cam.z;


    // Filter size in UV coordinates
    float filter_u = filter_size / (float)sm->width;
    float filter_v = filter_size / (float)sm->height;

    // Jittered sampling using a grid pattern with randomization
    int in_shadow = 0;
    int grid_size = (int)sqrtf((float)num_samples);
    if (grid_size < 1) grid_size = 1;

    float cell_u = filter_u / (float)grid_size;
    float cell_v = filter_v / (float)grid_size;

    int actual_samples = 0;

    for (int j = 0; j < grid_size; j++) {
        for (int i = 0; i < grid_size; i++) {
            // Jittered sample position within cell
            float jitter_u = pcf_random();
            float jitter_v = pcf_random();

            float sample_u = u + (i - grid_size * 0.5f + jitter_u) * cell_u;
            float sample_v = v + (j - grid_size * 0.5f + jitter_v) * cell_v;

            // Clamp to valid range
            if (sample_u < 0.0f || sample_u >= 1.0f ||
                sample_v < 0.0f || sample_v >= 1.0f) {
                continue;
            }

            // Sample shadow map (nearest neighbor)
            int px = (int)(sample_u * sm->width);
            int py = (int)(sample_v * sm->height);

            if (px < 0) px = 0;
            if (px >= sm->width) px = sm->width - 1;
            if (py < 0) py = 0;
            if (py >= sm->height) py = sm->height - 1;

            // Shadow map stores camera-space z (world units)
            float shadow_z = sm->depths[py * sm->width + px];

            // Add random bias variation to reduce banding
            // Bias is now in world units (e.g., 0.01 = 1cm)
            float sample_bias = bias * (0.5f + pcf_random());

            // Compare: if surface is farther than shadow map + bias, it's in shadow
            if (surface_z > shadow_z + sample_bias) {
                in_shadow++;
            }

            actual_samples++;
        }
    }

    // Return shadow factor (proportion of samples in shadow)
    if (actual_samples == 0) return 0.0f;
    float result = (float)in_shadow / (float)actual_samples;

    {
        static int call_count = 0, ground_total = 0, ground_shadowed = 0;
        static int all_total = 0, all_shadowed = 0;
        call_count++;
        all_total++;
        if (result > 0.01f) all_shadowed++;
        if (world_pos.y < -0.3f) {
            ground_total++;
            if (result > 0.01f) ground_shadowed++;
        }
        if (call_count % 50000 == 0) {
            xpt_debug("rh.shadow", "SHADOW STATS: total=%d shadowed=%d (%.1f%%) ground=%d g_shad=%d (%.1f%%)",
                      all_total, all_shadowed, 100.0f * all_shadowed / (all_total > 0 ? all_total : 1),
                      ground_total, ground_shadowed, 100.0f * ground_shadowed / (ground_total > 0 ? ground_total : 1));
        }
    }

    return result;
}
