#ifndef RH_SHADOW_H
#define RH_SHADOW_H

/**
 * rh_shadow.h - Shadow map support for percentage closer filtering (PCF)
 *
 * Shadow Map File Format (.shd) Version 2:
 * - 4 bytes: Magic number "RSHD"
 * - 4 bytes: Version (2)
 * - 4 bytes: Width (int32)
 * - 4 bytes: Height (int32)
 * - 4 bytes: Near clip (float32)
 * - 4 bytes: Far clip (float32)
 * - 64 bytes: World-to-light-NDC matrix (16 float32, row-major) - for UV lookup
 * - 64 bytes: World-to-light-camera matrix (16 float32, row-major) - for z comparison
 * - Width * Height * 4 bytes: Depth values (float32, camera-space z / world units)
 */

#include "rh_math.h"
#include <stdbool.h>

#define RH_SHADOW_MAGIC 0x44485352  /* "RSHD" */
#define RH_SHADOW_VERSION 2

// Shadow map data structure
typedef struct {
    int width;
    int height;
    float near_clip;
    float far_clip;
    RhMat4 world_to_light_ndc;     // Transforms world space to light NDC (for UV lookup)
    RhMat4 world_to_light_camera;  // Transforms world space to light camera space (for z comparison)
    float* depths;                 // Depth buffer - camera-space z in world units
} RhShadowMap;

// Shadow map file I/O
RhShadowMap* rh_shadowmap_create(int width, int height);
void rh_shadowmap_destroy(RhShadowMap* sm);
bool rh_shadowmap_write(const char* filename, const RhShadowMap* sm);
RhShadowMap* rh_shadowmap_read(const char* filename);

// PCF shadow lookup
// Returns shadow factor: 0.0 = fully lit, 1.0 = fully shadowed
float rh_shadow_pcf_lookup(
    const RhShadowMap* sm,
    RhVec3 world_pos,           // Surface point in world space
    int num_samples,            // Number of PCF samples (e.g., 16)
    float bias,                 // Depth bias to prevent self-shadowing
    float filter_size           // Filter region size in shadow map pixels
);

// Check if a world point is within the shadow map's view frustum
// Returns false if point is outside frustum (treat as unshadowed or fully shadowed)
bool rh_shadow_in_frustum(const RhShadowMap* sm, RhVec3 world_pos);

#endif // RH_SHADOW_H
