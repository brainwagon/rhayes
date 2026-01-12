#include "rh_math.h"
#include <math.h>

// --- Vector Operations ---

RhVec3 rh_vec3_create(RhFloat x, RhFloat y, RhFloat z) {
    RhVec3 v = {x, y, z};
    return v;
}

RhVec3 rh_vec3_add(RhVec3 a, RhVec3 b) {
    RhVec3 v = {a.x + b.x, a.y + b.y, a.z + b.z};
    return v;
}

RhVec3 rh_vec3_sub(RhVec3 a, RhVec3 b) {
    RhVec3 v = {a.x - b.x, a.y - b.y, a.z - b.z};
    return v;
}

RhVec3 rh_vec3_mul(RhVec3 v, RhFloat s) {
    RhVec3 r = {v.x * s, v.y * s, v.z * s};
    return r;
}

RhVec3 rh_vec3_div(RhVec3 v, RhFloat s) {
    if (s == 0.0f) return v; // Safety
    RhFloat inv = 1.0f / s;
    RhVec3 r = {v.x * inv, v.y * inv, v.z * inv};
    return r;
}

RhFloat rh_vec3_dot(RhVec3 a, RhVec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

RhVec3 rh_vec3_cross(RhVec3 a, RhVec3 b) {
    RhVec3 r;
    r.x = a.y * b.z - a.z * b.y;
    r.y = a.z * b.x - a.x * b.z;
    r.z = a.x * b.y - a.y * b.x;
    return r;
}

RhFloat rh_vec3_length_sq(RhVec3 v) {
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

RhFloat rh_vec3_length(RhVec3 v) {
    return sqrtf(rh_vec3_length_sq(v));
}

RhVec3 rh_vec3_normalize(RhVec3 v) {
    RhFloat len = rh_vec3_length(v);
    if (len > RH_EPSILON) {
        return rh_vec3_div(v, len);
    }
    return v;
}

// --- Matrix Operations ---

RhMat4 rh_mat4_identity(void) {
    RhMat4 m = {0};
    m.m[0][0] = 1.0f; m.m[1][1] = 1.0f; m.m[2][2] = 1.0f; m.m[3][3] = 1.0f;
    return m;
}

RhMat4 rh_mat4_translate(RhFloat x, RhFloat y, RhFloat z) {
    RhMat4 m = rh_mat4_identity();
    m.m[0][3] = x;
    m.m[1][3] = y;
    m.m[2][3] = z;
    return m;
}

RhMat4 rh_mat4_scale(RhFloat x, RhFloat y, RhFloat z) {
    RhMat4 m = {0};
    m.m[0][0] = x;
    m.m[1][1] = y;
    m.m[2][2] = z;
    m.m[3][3] = 1.0f;
    return m;
}

RhMat4 rh_mat4_mul(RhMat4 a, RhMat4 b) {
    RhMat4 r = {0};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            r.m[i][j] = 0.0f;
            for (int k = 0; k < 4; k++) {
                r.m[i][j] += a.m[i][k] * b.m[k][j];
            }
        }
    }
    return r;
}

RhVec4 rh_mat4_mul_vec4(RhMat4 m, RhVec4 v) {
    RhVec4 r;
    r.x = m.m[0][0]*v.x + m.m[0][1]*v.y + m.m[0][2]*v.z + m.m[0][3]*v.w;
    r.y = m.m[1][0]*v.x + m.m[1][1]*v.y + m.m[1][2]*v.z + m.m[1][3]*v.w;
    r.z = m.m[2][0]*v.x + m.m[2][1]*v.y + m.m[2][2]*v.z + m.m[2][3]*v.w;
    r.w = m.m[3][0]*v.x + m.m[3][1]*v.y + m.m[3][2]*v.z + m.m[3][3]*v.w;
    return r;
}

RhVec3 rh_mat4_mul_point(RhMat4 m, RhVec3 p) {
    RhVec4 v = {p.x, p.y, p.z, 1.0f};
    RhVec4 r = rh_mat4_mul_vec4(m, v);
    // Perspective divide would go here if w != 1, but for affine transforms it's usually 1
    // We will assume W=1 for basic transforms for now, but explicit division is safer
    if (fabs(r.w) > RH_EPSILON && fabs(r.w - 1.0f) > RH_EPSILON) {
        RhFloat invW = 1.0f / r.w;
        return rh_vec3_create(r.x * invW, r.y * invW, r.z * invW);
    }
    return rh_vec3_create(r.x, r.y, r.z);
}

RhVec3 rh_mat4_mul_dir(RhMat4 m, RhVec3 d) {
    RhVec4 v = {d.x, d.y, d.z, 0.0f};
    RhVec4 r = rh_mat4_mul_vec4(m, v);
    return rh_vec3_create(r.x, r.y, r.z);
}

RhMat4 rh_mat4_transpose(RhMat4 m) {
    RhMat4 r;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            r.m[i][j] = m.m[j][i];
        }
    }
    return r;
}

RhMat4 rh_mat4_inverse(RhMat4 m) {
    // Gauss-Jordan elimination or Cofactor expansion.
    // For 4x4, implementation is a bit long but standard.
    // Using a standard implementation approach.
    
    float temp[4][8];
    RhMat4 r;
    
    // Initialize augmented matrix [M | I]
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            temp[i][j] = m.m[i][j];
            temp[i][j+4] = (i == j) ? 1.0f : 0.0f;
        }
    }

    // Forward elimination
    for (int i = 0; i < 4; i++) {
        // Find pivot
        int pivot = i;
        float max_val = fabs(temp[i][i]);
        for (int k = i + 1; k < 4; k++) {
            if (fabs(temp[k][i]) > max_val) {
                max_val = fabs(temp[k][i]);
                pivot = k;
            }
        }

        // Swap rows if needed
        if (pivot != i) {
            for (int j = 0; j < 8; j++) {
                float swap = temp[i][j];
                temp[i][j] = temp[pivot][j];
                temp[pivot][j] = swap;
            }
        }

        // Check singular
        if (fabs(temp[i][i]) < RH_EPSILON) {
            return rh_mat4_identity(); // Fail, return identity
        }

        // Scale row to make pivot 1
        float div = temp[i][i];
        for (int j = 0; j < 8; j++) {
            temp[i][j] /= div;
        }

        // Eliminate other rows
        for (int k = 0; k < 4; k++) {
            if (k != i) {
                float mul = temp[k][i];
                for (int j = 0; j < 8; j++) {
                    temp[k][j] -= mul * temp[i][j];
                }
            }
        }
    }

    // Extract inverse
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            r.m[i][j] = temp[i][j+4];
        }
    }
    
    return r;
}

// --- Utilities ---

RhFloat rh_min(RhFloat a, RhFloat b) { return a < b ? a : b; }
RhFloat rh_max(RhFloat a, RhFloat b) { return a > b ? a : b; }
RhFloat rh_clamp(RhFloat v, RhFloat min, RhFloat max) {
    return (v < min) ? min : (v > max) ? max : v;
}
RhFloat rh_lerp(RhFloat a, RhFloat b, RhFloat t) {
    return a + t * (b - a);
}
