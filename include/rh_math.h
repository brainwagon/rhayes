#ifndef RH_MATH_H
#define RH_MATH_H

#include "rh_config.h"
#include <math.h>

// --- Structures ---

typedef struct {
    RhFloat x, y;
} RhVec2;

typedef struct {
    RhFloat x, y, z;
} RhVec3;

typedef struct {
    RhFloat x, y, z, w;
} RhVec4;

typedef struct {
    RhFloat m[4][4]; // Row-major
} RhMat4;

typedef struct {
    RhVec3 min;
    RhVec3 max;
} RhBounds3;

// --- Vector Operations ---

RhVec3 rh_vec3_create(RhFloat x, RhFloat y, RhFloat z);
RhVec3 rh_vec3_add(RhVec3 a, RhVec3 b);
RhVec3 rh_vec3_sub(RhVec3 a, RhVec3 b);
RhVec3 rh_vec3_mul(RhVec3 v, RhFloat s);
RhVec3 rh_vec3_div(RhVec3 v, RhFloat s);
RhFloat rh_vec3_dot(RhVec3 a, RhVec3 b);
RhVec3 rh_vec3_cross(RhVec3 a, RhVec3 b);
RhFloat rh_vec3_length_sq(RhVec3 v);
RhFloat rh_vec3_length(RhVec3 v);
RhVec3 rh_vec3_normalize(RhVec3 v);

// --- Matrix Operations ---

RhMat4 rh_mat4_identity(void);
RhMat4 rh_mat4_translate(RhFloat x, RhFloat y, RhFloat z);
RhMat4 rh_mat4_scale(RhFloat x, RhFloat y, RhFloat z);
RhMat4 rh_mat4_mul(RhMat4 a, RhMat4 b);
RhVec4 rh_mat4_mul_vec4(RhMat4 m, RhVec4 v);
RhVec3 rh_mat4_mul_point(RhMat4 m, RhVec3 p); // Assumes w=1
RhVec3 rh_mat4_mul_dir(RhMat4 m, RhVec3 d);   // Assumes w=0

RhMat4 rh_mat4_transpose(RhMat4 m);
RhMat4 rh_mat4_inverse(RhMat4 m);

// --- Utilities ---

RhFloat rh_min(RhFloat a, RhFloat b);
RhFloat rh_max(RhFloat a, RhFloat b);
RhFloat rh_clamp(RhFloat v, RhFloat min, RhFloat max);
RhFloat rh_lerp(RhFloat a, RhFloat b, RhFloat t);

#endif // RH_MATH_H
