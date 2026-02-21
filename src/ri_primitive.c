/**
 * ri_primitive.c - Primitive creation functions
 *
 * Handles all geometric primitives (Sphere, Cylinder, Polygon, Patch, etc.),
 * retained geometry, and surface shaders.
 */

#include "ri_internal.h"
#include "teapot_data.h"
#include "rh_sl_slo.h"
#include "rh_sl_vm.h"
#include "rh_sl_codegen.h"
#include "rh_sl_parse.h"
#include "rh_sl_sema.h"
#include "xpt.h"
#include <stdio.h>
#include <string.h>

/* --- SL Shader Program Cache --- */

#define SL_CACHE_MAX 64

static struct {
    char name[64];
    RhSLProgram* prog;
} sl_cache[SL_CACHE_MAX];
static int sl_cache_count = 0;

static RhSLProgram* sl_cache_find(const char* name) {
    for (int i = 0; i < sl_cache_count; i++) {
        if (strcmp(sl_cache[i].name, name) == 0)
            return sl_cache[i].prog;
    }
    return NULL;
}

static void sl_cache_add(const char* name, RhSLProgram* prog) {
    if (sl_cache_count >= SL_CACHE_MAX) return;
    size_t len = strlen(name);
    if (len >= 64) len = 63;
    memcpy(sl_cache[sl_cache_count].name, name, len);
    sl_cache[sl_cache_count].name[len] = '\0';
    sl_cache[sl_cache_count].prog = prog;
    sl_cache_count++;
}

void ri_sl_cache_clear(void) {
    for (int i = 0; i < sl_cache_count; i++) {
        rh_sl_program_free(sl_cache[i].prog);
    }
    sl_cache_count = 0;
}

/* Try to read file into malloc'd buffer; returns NULL if not found. */
static char* sl_read_file(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t nread = fread(buf, 1, (size_t)len, f);
    buf[nread] = '\0';
    fclose(f);
    return buf;
}

/* Compile a .sl source string to an RhSLProgram. Returns NULL on error. */
static RhSLProgram* sl_compile_source(const char* source, const char* filename) {
    RhSLParser parser;
    rh_sl_parse_init(&parser, source);
    RhSLNode* ast = rh_sl_parse(&parser);
    if (!ast || parser.num_errors > 0) {
        for (int i = 0; i < parser.num_errors; i++)
            xpt_error("ri.primitive", "%s: %s", filename, parser.errors[i]);
        if (ast) rh_sl_node_free(ast);
        return NULL;
    }

    RhSLSema sema;
    rh_sl_sema_init(&sema);
    if (rh_sl_sema_analyze(&sema, ast) != 0) {
        for (int i = 0; i < sema.num_errors; i++)
            xpt_error("ri.primitive", "%s: %s", filename, sema.errors[i]);
        rh_sl_node_free(ast);
        return NULL;
    }

    RhSLCodegenErrors errs;
    RhSLProgram* prog = rh_sl_codegen(ast, &errs);
    if (!prog) {
        for (int i = 0; i < errs.num_errors; i++)
            xpt_error("ri.primitive", "%s: %s", filename, errs.errors[i]);
    }
    rh_sl_node_free(ast);
    return prog;
}

/* Try to load a shader from a single directory prefix.
   dir should be "" for current directory or "path/" with trailing slash.
   Returns cached or newly loaded program, or NULL if not found. */
RhSLProgram* sl_try_load_from_dir(const char* name, const char* dir) {
    RhSLProgram* prog;
    char path[512];

    /* Try .slo first */
    int n = snprintf(path, sizeof(path), "%s%s.slo", dir, name);
    if (n >= 0 && (size_t)n < sizeof(path)) {
        prog = rh_sl_slo_read(path);
        if (prog) {
            xpt_debug("ri.primitive", "shader: loaded '%s' from '%s'", name, path);
            sl_cache_add(name, prog);
            return prog;
        }
    }

    /* Try .sl (compile on-the-fly) */
    n = snprintf(path, sizeof(path), "%s%s.sl", dir, name);
    if (n >= 0 && (size_t)n < sizeof(path)) {
        char* source = sl_read_file(path);
        if (source) {
            xpt_debug("ri.primitive", "shader: compiling '%s' from '%s'", name, path);
            prog = sl_compile_source(source, path);
            free(source);
            if (prog) {
                xpt_debug("ri.primitive", "shader: loaded '%s' from '%s'", name, path);
                sl_cache_add(name, prog);
                return prog;
            }
        }
    }

    return NULL;
}

/* Parse the shader_searchpath from context and get directory elements
   (excluding BUILTIN). Each element is converted to a directory prefix:
   "." -> "", "shaders" -> "shaders/", etc.
   Returns count of dirs written. */
static int sl_get_search_dirs(const char** dirs, char dir_bufs[][256], int max_dirs) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return 0;

    const char* path = ctx->shader_searchpath;
    int count = 0;

    while (*path && count < max_dirs) {
        /* Find the end of this element (colon or end of string) */
        const char* end = path;
        while (*end && *end != ':') end++;
        size_t len = (size_t)(end - path);

        if (len > 0 && !(len == 7 && memcmp(path, "BUILTIN", 7) == 0)) {
            /* Convert "." to "" (current dir), otherwise append '/' */
            if (len == 1 && path[0] == '.') {
                dir_bufs[count][0] = '\0';
            } else {
                if (len >= 255) len = 254;
                memcpy(dir_bufs[count], path, len);
                /* Append trailing slash if not present */
                if (dir_bufs[count][len - 1] != '/') {
                    dir_bufs[count][len] = '/';
                    dir_bufs[count][len + 1] = '\0';
                } else {
                    dir_bufs[count][len] = '\0';
                }
            }
            dirs[count] = dir_bufs[count];
            count++;
        }

        path = (*end == ':') ? end + 1 : end;
    }
    return count;
}

/* Load or compile a shader program by name, searching context search path
   directories (excluding BUILTIN). Returns NULL on failure. */
RhSLProgram* sl_load_program(const char* name) {
    RhSLProgram* prog = sl_cache_find(name);
    if (prog) return prog;

    const char* dirs[16];
    char dir_bufs[16][256];
    int ndirs = sl_get_search_dirs(dirs, dir_bufs, 16);

    for (int i = 0; i < ndirs; i++) {
        prog = sl_try_load_from_dir(name, dirs[i]);
        if (prog) return prog;
    }

    return NULL;
}

/* --- Builtin Shader Lookup Tables --- */

typedef struct {
    const char* name;
    RhShaderFunc func;
} RhBuiltinShader;

static const RhBuiltinShader builtin_surface_shaders[] = {
    {"plastic",     rh_shader_surface_plastic},
    {"matte",       rh_shader_surface_matte},
    {"constant",    rh_shader_surface_constant},
    {"metal",       rh_shader_surface_metal},
    {"shinymetal",  rh_shader_surface_shinymetal},
    {"randomgrid",  rh_shader_surface_randomgrid},
    {"random",      rh_shader_surface_random},
    {NULL, NULL}
};

/* Check builtin surface shader table. Returns func or NULL. */
static RhShaderFunc builtin_surface_lookup(const char* name) {
    for (int i = 0; builtin_surface_shaders[i].name; i++) {
        if (strcmp(name, builtin_surface_shaders[i].name) == 0)
            return builtin_surface_shaders[i].func;
    }
    return NULL;
}

/* Known builtin atmosphere shaders */
static bool builtin_atmosphere_known(const char* name) {
    return strcmp(name, "depthcue") == 0 ||
           strcmp(name, "fog") == 0;
}

void ri_iterate_searchpath(RiSearchPathCallback cb, void* user) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    const char* path = ctx->shader_searchpath;
    char dir_buf[256];

    while (*path) {
        const char* end = path;
        while (*end && *end != ':') end++;
        size_t len = (size_t)(end - path);

        if (len > 0) {
            if (len == 7 && memcmp(path, "BUILTIN", 7) == 0) {
                if (cb(true, NULL, user)) return;
            } else {
                /* Convert "." to "", otherwise append '/' */
                if (len == 1 && path[0] == '.') {
                    dir_buf[0] = '\0';
                } else {
                    if (len >= 255) len = 254;
                    memcpy(dir_buf, path, len);
                    if (dir_buf[len - 1] != '/') {
                        dir_buf[len] = '/';
                        dir_buf[len + 1] = '\0';
                    } else {
                        dir_buf[len] = '\0';
                    }
                }
                if (cb(false, dir_buf, user)) return;
            }
        }

        path = (*end == ':') ? end + 1 : end;
    }
}

void ri_iterate_texture_searchpath(RiSearchPathCallback cb, void* user) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    const char* path = ctx->texture_searchpath;
    char dir_buf[256];

    while (*path) {
        const char* end = path;
        while (*end && *end != ':') end++;
        size_t len = (size_t)(end - path);

        if (len > 0) {
            /* Convert "." to "", otherwise append '/' */
            if (len == 1 && path[0] == '.') {
                dir_buf[0] = '\0';
            } else {
                if (len >= 255) len = 254;
                memcpy(dir_buf, path, len);
                if (dir_buf[len - 1] != '/') {
                    dir_buf[len] = '/';
                    dir_buf[len + 1] = '\0';
                } else {
                    dir_buf[len] = '\0';
                }
            }
            if (cb(false, dir_buf, user)) return;
        }

        path = (*end == ':') ? end + 1 : end;
    }
}

typedef struct {
    const char* filename;
    RhTextureFormat format;
    RhTexture* result;
} TextureSearch;

static bool texture_search_cb(bool is_builtin, const char* dir, void* user) {
    (void)is_builtin;
    TextureSearch* s = (TextureSearch*)user;
    char path[512];
    if (dir && dir[0]) {
        snprintf(path, sizeof(path), "%s%s", dir, s->filename);
    } else {
        strncpy(path, s->filename, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    }
    
    s->result = rh_texture_load(path, s->format);
    return (s->result != NULL);
}

RhTexture* ri_texture_load(const char* filename, RhTextureFormat format) {
    if (!filename || !filename[0]) return NULL;
    
    TextureSearch s = {filename, format, NULL};
    ri_iterate_texture_searchpath(texture_search_cb, &s);
    return s.result;
}

typedef struct {
    const char* filename;
    RhShadowMap* result;
} ShadowSearch;

static bool shadow_search_cb(bool is_builtin, const char* dir, void* user) {
    (void)is_builtin;
    ShadowSearch* s = (ShadowSearch*)user;
    char path[512];
    if (dir && dir[0]) {
        snprintf(path, sizeof(path), "%s%s", dir, s->filename);
    } else {
        strncpy(path, s->filename, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    }
    
    s->result = rh_shadowmap_read(path);
    return (s->result != NULL);
}

RhShadowMap* ri_shadowmap_read(const char* filename) {
    if (!filename || !filename[0]) return NULL;
    
    ShadowSearch s = {filename, NULL};
    ri_iterate_texture_searchpath(shadow_search_cb, &s);
    return s.result;
}

// --- Standard Basis Matrices ---

RtMatrix RiBezierBasis = {
    {-1,  3, -3,  1},
    { 3, -6,  3,  0},
    {-3,  3,  0,  0},
    { 1,  0,  0,  0}
};

RtMatrix RiBSplineBasis = {
    {-1.0f/6,  3.0f/6, -3.0f/6,  1.0f/6},
    { 3.0f/6, -6.0f/6,  3.0f/6,  0.0f/6},
    {-3.0f/6,  0.0f/6,  3.0f/6,  0.0f/6},
    { 1.0f/6,  4.0f/6,  1.0f/6,  0.0f/6}
};

RtMatrix RiCatmullRomBasis = {
    {-0.5f,  1.5f, -1.5f,  0.5f},
    { 1.0f, -2.5f,  2.0f, -0.5f},
    {-0.5f,  0.0f,  0.5f,  0.0f},
    { 0.0f,  1.0f,  0.0f,  0.0f}
};

RtMatrix RiHermiteBasis = {
    { 2, -2,  1,  1},
    {-3,  3, -2, -1},
    { 0,  0,  1,  0},
    { 1,  0,  0,  0}
};

RtMatrix RiPowerBasis = {
    { 1,  0,  0,  0},
    { 0,  1,  0,  0},
    { 0,  0,  1,  0},
    { 0,  0,  0,  1}
};

// --- Forward declaration ---

static void ri_add_to_buckets(const RhPrimitive* p, const RhMat4* transform, const RhColor* color);

// --- Internal Helpers ---

void ri_add_geometry(RhPrimitive* p) {
    RiContextData* ctx = ri_get_ctx();
    if (ctx->current_obj) {
        RhObject* obj = ctx->current_obj;
        if (obj->count >= obj->capacity) {
            obj->capacity = obj->capacity == 0 ? 4 : obj->capacity * 2;
            obj->items = (RhObjectItem*)realloc(obj->items, obj->capacity * sizeof(RhObjectItem));
        }
        RhObjectItem* item = &obj->items[obj->count++];
        item->prim = *p;
        if (p->type == RH_PRIM_POLYGON) {
            item->prim.data.polygon.vertices = (RhVec3*)malloc(p->data.polygon.count * sizeof(RhVec3));
            memcpy(item->prim.data.polygon.vertices, p->data.polygon.vertices, p->data.polygon.count * sizeof(RhVec3));
            // Deep copy st coords if present
            if (p->data.polygon.st) {
                item->prim.data.polygon.st = (RhFloat*)malloc(p->data.polygon.count * 2 * sizeof(RhFloat));
                memcpy(item->prim.data.polygon.st, p->data.polygon.st, p->data.polygon.count * 2 * sizeof(RhFloat));
            } else {
                item->prim.data.polygon.st = NULL;
            }
            // Deep copy vertex normals if present
            if (p->data.polygon.normals) {
                item->prim.data.polygon.normals = (RhVec3*)malloc(p->data.polygon.count * sizeof(RhVec3));
                memcpy(item->prim.data.polygon.normals, p->data.polygon.normals, p->data.polygon.count * sizeof(RhVec3));
            } else {
                item->prim.data.polygon.normals = NULL;
            }
        }
        item->transform = ri_curr()->transform;
    } else {
        ri_add_to_buckets(p, &ri_curr()->transform, &ri_curr()->color);
    }
}

// --- Primvar Parsing Helper ---
// Parse token/value arrays and attach primvars to a primitive
// Returns 1 on success, 0 on failure

static int ri_parse_primvars(RhPrimitive* prim, RtToken* tokens, RtPointer* values, int count) {
    if (!prim || count <= 0) return 1;  // Nothing to parse is success

    // Count how many user-defined primvars we have
    int num_primvars = 0;
    for (int i = 0; i < count; i++) {
        if (!tokens[i]) continue;

        // Skip standard geometric variables that are handled elsewhere
        // P, N, Cs, Os, s, t, st are typically handled by the primitive itself
        if (strcmp(tokens[i], "P") == 0 ||
            strcmp(tokens[i], "N") == 0 ||
            strcmp(tokens[i], "Np") == 0 ||
            strcmp(tokens[i], "s") == 0 ||
            strcmp(tokens[i], "t") == 0 ||
            strcmp(tokens[i], "st") == 0) {
            continue;
        }

        const RiDeclaration* decl = ri_lookup_declaration(tokens[i]);
        if (decl) {
            num_primvars++;
        }
    }

    if (num_primvars == 0) return 1;  // No primvars to add

    // Allocate primvar array
    prim->primvars = (RhPrimVar*)malloc(num_primvars * sizeof(RhPrimVar));
    if (!prim->primvars) return 0;
    prim->num_primvars = 0;

    // Parse each token/value pair
    for (int i = 0; i < count; i++) {
        if (!tokens[i]) continue;

        // Skip standard geometric variables
        if (strcmp(tokens[i], "P") == 0 ||
            strcmp(tokens[i], "N") == 0 ||
            strcmp(tokens[i], "Np") == 0 ||
            strcmp(tokens[i], "s") == 0 ||
            strcmp(tokens[i], "t") == 0 ||
            strcmp(tokens[i], "st") == 0) {
            continue;
        }

        const RiDeclaration* decl = ri_lookup_declaration(tokens[i]);
        if (!decl) continue;

        RhPrimVar* pv = &prim->primvars[prim->num_primvars];
        strncpy(pv->name, decl->name, sizeof(pv->name) - 1);
        pv->name[sizeof(pv->name) - 1] = '\0';
        pv->sclass = decl->sclass;
        pv->type = decl->type;

        // For now, we support UNIFORM/CONSTANT storage class (single value)
        // VARYING/VERTEX would need interpolation support
        pv->count = decl->array_size;

        // Calculate data size and copy
        size_t component_count = rh_type_component_count(decl->type);
        size_t element_size;
        if (decl->type == RI_TYPE_STRING) {
            element_size = sizeof(char*);
        } else if (decl->type == RI_TYPE_INTEGER) {
            element_size = sizeof(int);
        } else {
            element_size = sizeof(float) * component_count;
        }
        size_t total_size = element_size * decl->array_size;

        pv->data = malloc(total_size);
        if (pv->data) {
            if (decl->type == RI_TYPE_STRING) {
                // Copy string pointers and duplicate strings
                char** dst_strings = (char**)pv->data;
                char** src_strings = (char**)values[i];
                for (int j = 0; j < decl->array_size; j++) {
                    if (src_strings && src_strings[j]) {
                        dst_strings[j] = strdup(src_strings[j]);
                    } else if (values[i]) {
                        // Single string passed directly
                        dst_strings[j] = strdup((char*)values[i]);
                    } else {
                        dst_strings[j] = NULL;
                    }
                }
            } else {
                memcpy(pv->data, values[i], total_size);
            }
            prim->num_primvars++;
        }
    }

    return 1;
}

// --- Bucket Functions (needed by ri_add_geometry) ---

static void ri_add_to_buckets(const RhPrimitive* p, const RhMat4* transform, const RhColor* color) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx || !ctx->world_active) return;

    RhRenderItem* item = ri_render_item_create(p, transform, color);

    // Handle memory limit - primitive was dropped
    if (!item) return;

    // Track primitive statistics
    if (p->type >= 0 && p->type < 9) {
        ctx->stats.primitives_by_type[p->type]++;
    }

    // Calculate Screen Bounds
    RhBounds3 obj_bounds = rh_prim_bound(p);
    if (item->displace_bound > 0.0f) {
        float db = ri_displace_bound_in_obj(item->displace_bound,
                                            item->displace_coordsys,
                                            &item->transform);
        obj_bounds.min.x -= db; obj_bounds.min.y -= db; obj_bounds.min.z -= db;
        obj_bounds.max.x += db; obj_bounds.max.y += db; obj_bounds.max.z += db;
    }
    RhMat4 mvp = rh_mat4_mul(ctx->projection, rh_mat4_mul(ctx->view_matrix, item->transform));

    RhVec3 corners[8];
    corners[0] = rh_vec3_create(obj_bounds.min.x, obj_bounds.min.y, obj_bounds.min.z);
    corners[1] = rh_vec3_create(obj_bounds.max.x, obj_bounds.min.y, obj_bounds.min.z);
    corners[2] = rh_vec3_create(obj_bounds.min.x, obj_bounds.max.y, obj_bounds.min.z);
    corners[3] = rh_vec3_create(obj_bounds.max.x, obj_bounds.max.y, obj_bounds.min.z);
    corners[4] = rh_vec3_create(obj_bounds.min.x, obj_bounds.min.y, obj_bounds.max.z);
    corners[5] = rh_vec3_create(obj_bounds.max.x, obj_bounds.min.y, obj_bounds.max.z);
    corners[6] = rh_vec3_create(obj_bounds.min.x, obj_bounds.max.y, obj_bounds.max.z);
    corners[7] = rh_vec3_create(obj_bounds.max.x, obj_bounds.max.y, obj_bounds.max.z);

    float min_x = 1e30f, max_x = -1e30f;
    float min_y = 1e30f, max_y = -1e30f;
    float min_depth = 1e30f, max_depth = -1e30f;

    // Project bounding box corners at t0
    for (int i = 0; i < 8; i++) {
        RhVec3 p_ndc = rh_mat4_mul_point(mvp, corners[i]);
        float rx = (p_ndc.x + 1.0f) * 0.5f * ctx->ss_xres;
        float ry = (1.0f - (p_ndc.y + 1.0f) * 0.5f) * ctx->ss_yres;
        if (rx < min_x) min_x = rx;
        if (rx > max_x) max_x = rx;
        if (ry < min_y) min_y = ry;
        if (ry > max_y) max_y = ry;
        // Track depth bounds for front-to-back sorting and Hi-Z culling
        if (p_ndc.z < min_depth) min_depth = p_ndc.z;
        if (p_ndc.z > max_depth) max_depth = p_ndc.z;
    }

    // For motion blur: also project at t1 and use union of bounds
    if (item->has_motion) {
        RhMat4 mvp_t1 = rh_mat4_mul(ctx->projection, rh_mat4_mul(ctx->view_matrix, item->transform_t1));
        for (int i = 0; i < 8; i++) {
            RhVec3 p_ndc = rh_mat4_mul_point(mvp_t1, corners[i]);
            float rx = (p_ndc.x + 1.0f) * 0.5f * ctx->ss_xres;
            float ry = (1.0f - (p_ndc.y + 1.0f) * 0.5f) * ctx->ss_yres;
            if (rx < min_x) min_x = rx;
            if (rx > max_x) max_x = rx;
            if (ry < min_y) min_y = ry;
            if (ry > max_y) max_y = ry;
            // Track depth bounds across motion range
            if (p_ndc.z < min_depth) min_depth = p_ndc.z;
            if (p_ndc.z > max_depth) max_depth = p_ndc.z;
        }
    }

    // Store depth bounds for front-to-back sorting and Hi-Z culling
    item->min_depth = min_depth;
    item->max_depth = max_depth;

    // Near plane culling: check if ALL corners are behind near plane
    // Left-handed: camera looks down +Z, in front when z >= near_clip
    RhMat4 mv = rh_mat4_mul(ctx->view_matrix, item->transform);
    bool all_behind = true;
    for (int i = 0; i < 8; i++) {
        RhVec3 cam_corner = rh_mat4_mul_point(mv, corners[i]);
        if (cam_corner.z >= ctx->near_clip) {
            all_behind = false;
            break;
        }
    }
    if (all_behind) {
        ctx->all_items[item->all_items_idx] = NULL;
        ri_render_item_destroy(item);
        return;
    }

    int b_min_x = (int)floorf(min_x / ctx->bucket_size);
    int b_max_x = (int)floorf(max_x / ctx->bucket_size);
    int b_min_y = (int)floorf(min_y / ctx->bucket_size);
    int b_max_y = (int)floorf(max_y / ctx->bucket_size);

    // Early-out for completely off-screen items (check before clamping)
    if (b_max_x < 0 || b_max_y < 0 ||
        b_min_x >= ctx->num_buckets_x || b_min_y >= ctx->num_buckets_y) {
        ctx->all_items[item->all_items_idx] = NULL;  // Mark as freed
        ri_render_item_destroy(item);
        return;
    }

    // Clamp to bucket grid for partially visible items
    if (b_min_x < 0) b_min_x = 0;
    if (b_max_x >= ctx->num_buckets_x) b_max_x = ctx->num_buckets_x - 1;
    if (b_min_y < 0) b_min_y = 0;
    if (b_max_y >= ctx->num_buckets_y) b_max_y = ctx->num_buckets_y - 1;

    // Compute last bucket index (scanline order: y * num_buckets_x + x)
    item->last_bucket_idx = b_max_y * ctx->num_buckets_x + b_max_x;

    for (int y = b_min_y; y <= b_max_y; y++) {
        for (int x = b_min_x; x <= b_max_x; x++) {
            RhBucket* b = &ctx->buckets[y * ctx->num_buckets_x + x];
            if (b->item_count >= b->item_capacity) {
                b->item_capacity = b->item_capacity == 0 ? 4 : b->item_capacity * 2;
                b->items = (RhRenderItem**)realloc(b->items, b->item_capacity * sizeof(RhRenderItem*));
            }
            b->items[b->item_count++] = item;
        }
    }
}

// --- Retained Geometry ---

RtObjectHandle RiObjectBegin(void) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return RI_NULL;
    RhObject* obj = (RhObject*)calloc(1, sizeof(RhObject));
    obj->inv_transform = rh_mat4_inverse(ri_curr()->transform);
    ctx->current_obj = obj;
    return (RtObjectHandle)obj;
}

void RiObjectEnd(void) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx || !ctx->current_obj) return;

    RhObject* obj = ctx->current_obj;
    if (ctx->objects_count >= ctx->objects_capacity) {
        ctx->objects_capacity = ctx->objects_capacity == 0 ? 4 : ctx->objects_capacity * 2;
        ctx->objects = (RhObject**)realloc(ctx->objects, ctx->objects_capacity * sizeof(RhObject*));
    }
    ctx->objects[ctx->objects_count++] = obj;
    ctx->current_obj = NULL;
}

void RiObjectInstance(RtObjectHandle handle) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx || !handle || !ctx->world_active) return;

    RhObject* obj = (RhObject*)handle;
    RhMat4 instance_transform = ri_curr()->transform;
    RhColor instance_color = ri_curr()->color;

    // Instance CTM * inv(BeginCTM) * item_local_CTM
    RhMat4 base_transform = rh_mat4_mul(instance_transform, obj->inv_transform);

    for (int i = 0; i < obj->count; i++) {
        RhObjectItem* item = &obj->items[i];
        RhMat4 final_transform = rh_mat4_mul(base_transform, item->transform);
        ri_add_to_buckets(&item->prim, &final_transform, &instance_color);
    }
}

// --- Primitive V Functions ---

void RiSphereV(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
               RtToken* tokens, RtPointer* values, int count) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    RhPrimitive p = rh_prim_create_sphere(radius, zmin, zmax, 0.0f, tmax);
    ri_parse_primvars(&p, tokens, values, count);
    ri_add_geometry(&p);
    rh_prim_free_data(&p);
}

void RiSphere(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax, ...) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    // Build token/value arrays from varargs
    RtToken tokens[16];
    RtPointer values[16];
    int count = 0;

    va_list ap;
    va_start(ap, tmax);
    RtToken token;
    while ((token = va_arg(ap, RtToken)) != RI_NULL && count < 16) {
        tokens[count] = token;
        values[count] = va_arg(ap, RtPointer);
        count++;
    }
    va_end(ap);

    RiSphereV(radius, zmin, zmax, tmax, tokens, values, count);
}

void RiCylinderV(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                 RtToken* tokens, RtPointer* values, int count) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    RhPrimitive p = rh_prim_create_cylinder(radius, zmin, zmax, tmax);
    ri_parse_primvars(&p, tokens, values, count);
    ri_add_geometry(&p);
    rh_prim_free_data(&p);
}

void RiCylinder(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax, ...) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    RtToken tokens[16];
    RtPointer values[16];
    int count = 0;

    va_list ap;
    va_start(ap, tmax);
    RtToken token;
    while ((token = va_arg(ap, RtToken)) != RI_NULL && count < 16) {
        tokens[count] = token;
        values[count] = va_arg(ap, RtPointer);
        count++;
    }
    va_end(ap);

    RiCylinderV(radius, zmin, zmax, tmax, tokens, values, count);
}

void RiConeV(RtFloat height, RtFloat radius, RtFloat tmax,
             RtToken* tokens, RtPointer* values, int count) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    RhPrimitive p = rh_prim_create_cone(height, radius, tmax);
    ri_parse_primvars(&p, tokens, values, count);
    ri_add_geometry(&p);
    rh_prim_free_data(&p);
}

void RiCone(RtFloat height, RtFloat radius, RtFloat tmax, ...) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    RtToken tokens[16];
    RtPointer values[16];
    int count = 0;

    va_list ap;
    va_start(ap, tmax);
    RtToken token;
    while ((token = va_arg(ap, RtToken)) != RI_NULL && count < 16) {
        tokens[count] = token;
        values[count] = va_arg(ap, RtPointer);
        count++;
    }
    va_end(ap);

    RiConeV(height, radius, tmax, tokens, values, count);
}

void RiParaboloidV(RtFloat rmax, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                   RtToken* tokens, RtPointer* values, int count) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    RhPrimitive p = rh_prim_create_paraboloid(rmax, zmin, zmax, tmax);
    ri_parse_primvars(&p, tokens, values, count);
    ri_add_geometry(&p);
    rh_prim_free_data(&p);
}

void RiParaboloid(RtFloat rmax, RtFloat zmin, RtFloat zmax, RtFloat tmax, ...) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    RtToken tokens[16];
    RtPointer values[16];
    int count = 0;

    va_list ap;
    va_start(ap, tmax);
    RtToken token;
    while ((token = va_arg(ap, RtToken)) != RI_NULL && count < 16) {
        tokens[count] = token;
        values[count] = va_arg(ap, RtPointer);
        count++;
    }
    va_end(ap);

    RiParaboloidV(rmax, zmin, zmax, tmax, tokens, values, count);
}

void RiDiskV(RtFloat height, RtFloat radius, RtFloat tmax,
             RtToken* tokens, RtPointer* values, int count) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    RhPrimitive p = rh_prim_create_disk(height, radius, tmax);
    ri_parse_primvars(&p, tokens, values, count);
    ri_add_geometry(&p);
    rh_prim_free_data(&p);
}

void RiDisk(RtFloat height, RtFloat radius, RtFloat tmax, ...) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    RtToken tokens[16];
    RtPointer values[16];
    int count = 0;

    va_list ap;
    va_start(ap, tmax);
    RtToken token;
    while ((token = va_arg(ap, RtToken)) != RI_NULL && count < 16) {
        tokens[count] = token;
        values[count] = va_arg(ap, RtPointer);
        count++;
    }
    va_end(ap);

    RiDiskV(height, radius, tmax, tokens, values, count);
}

void RiTorusV(RtFloat majorradius, RtFloat minorradius, RtFloat phimin, RtFloat phimax, RtFloat tmax,
              RtToken* tokens, RtPointer* values, int count) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    RhPrimitive p = rh_prim_create_torus(majorradius, minorradius, phimin, phimax, tmax);
    ri_parse_primvars(&p, tokens, values, count);
    ri_add_geometry(&p);
    rh_prim_free_data(&p);
}

void RiTorus(RtFloat majorradius, RtFloat minorradius, RtFloat phimin, RtFloat phimax, RtFloat tmax, ...) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    RtToken tokens[16];
    RtPointer values[16];
    int count = 0;

    va_list ap;
    va_start(ap, tmax);
    RtToken token;
    while ((token = va_arg(ap, RtToken)) != RI_NULL && count < 16) {
        tokens[count] = token;
        values[count] = va_arg(ap, RtPointer);
        count++;
    }
    va_end(ap);

    RiTorusV(majorradius, minorradius, phimin, phimax, tmax, tokens, values, count);
}

void RiHyperboloidV(RtPoint point1, RtPoint point2, RtFloat tmax,
                    RtToken* tokens, RtPointer* values, int count) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;
    RhVec3 p1 = rh_vec3_create(point1[0], point1[1], point1[2]);
    RhVec3 p2 = rh_vec3_create(point2[0], point2[1], point2[2]);
    RhPrimitive p = rh_prim_create_hyperboloid(p1, p2, tmax);
    ri_parse_primvars(&p, tokens, values, count);
    ri_add_geometry(&p);
    rh_prim_free_data(&p);
}

void RiHyperboloid(RtPoint point1, RtPoint point2, RtFloat tmax, ...) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    RtToken tokens[16];
    RtPointer values[16];
    int count = 0;

    va_list ap;
    va_start(ap, tmax);
    RtToken token;
    while ((token = va_arg(ap, RtToken)) != RI_NULL && count < 16) {
        tokens[count] = token;
        values[count] = va_arg(ap, RtPointer);
        count++;
    }
    va_end(ap);

    RiHyperboloidV(point1, point2, tmax, tokens, values, count);
}

void RiPolygonV(RtInt nvertices, RtToken* tokens, RtPointer* values, int count) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    // Find the "P", "N", and "st" parameters
    RhVec3* points = NULL;
    RhFloat* st_data = NULL;
    RhVec3* normals = NULL;

    for (int i = 0; i < count; i++) {
        if (tokens[i] && strcmp(tokens[i], "P") == 0) {
            RtFloat* p = (RtFloat*)values[i];
            points = (RhVec3*)malloc(nvertices * sizeof(RhVec3));
            for (int j = 0; j < nvertices; j++) {
                points[j] = rh_vec3_create(p[j*3], p[j*3+1], p[j*3+2]);
            }
        } else if (tokens[i] && strcmp(tokens[i], "st") == 0) {
            RtFloat* st = (RtFloat*)values[i];
            st_data = (RhFloat*)malloc(nvertices * 2 * sizeof(RhFloat));
            memcpy(st_data, st, nvertices * 2 * sizeof(RhFloat));
        } else if (tokens[i] && strcmp(tokens[i], "N") == 0) {
            RtFloat* n = (RtFloat*)values[i];
            normals = (RhVec3*)malloc(nvertices * sizeof(RhVec3));
            for (int j = 0; j < nvertices; j++) {
                normals[j] = rh_vec3_create(n[j*3], n[j*3+1], n[j*3+2]);
            }
        }
    }

    if (!points) {
        free(st_data);
        free(normals);
        return;
    }

    RhPrimitive prim = rh_prim_create_polygon(nvertices, points);
    prim.data.polygon.st = st_data;      // Attach st coords (may be NULL)
    prim.data.polygon.normals = normals; // Attach vertex normals (may be NULL)
    ri_parse_primvars(&prim, tokens, values, count);
    ri_add_geometry(&prim);
    rh_prim_free_data(&prim);
    free(points);
}

void RiPolygon(RtInt nvertices, ...) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    RtToken tokens[16];
    RtPointer values[16];
    int count = 0;

    va_list ap;
    va_start(ap, nvertices);
    RtToken token;
    while ((token = va_arg(ap, RtToken)) != RI_NULL && count < 16) {
        tokens[count] = token;
        values[count] = va_arg(ap, RtPointer);
        count++;
    }
    va_end(ap);

    RiPolygonV(nvertices, tokens, values, count);
}

void RiPatchV(RtToken type, RtToken* tokens, RtPointer* values, int count) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    if (strcmp(type, "bicubic") == 0) {
        // Find the "P" parameter
        RhVec3 cpts[16];
        bool have_points = false;
        for (int i = 0; i < count; i++) {
            if (tokens[i] && strcmp(tokens[i], "P") == 0) {
                RtFloat* p = (RtFloat*)values[i];
                for (int j = 0; j < 16; j++) {
                    cpts[j] = rh_vec3_create(p[j*3], p[j*3+1], p[j*3+2]);
                }
                have_points = true;
                break;
            }
        }
        if (have_points) {
            RhMat4 u_basis = ri_curr()->u_basis;
            RhMat4 v_basis = ri_curr()->v_basis;
            RhPrimitive p = rh_prim_create_patch_bicubic(cpts, u_basis, v_basis);
            ri_parse_primvars(&p, tokens, values, count);
            ri_add_geometry(&p);
            rh_prim_free_data(&p);
        }
    } else if (strcmp(type, "bilinear") == 0) {
        // Find the "P" parameter (4 control points)
        RhVec3 cpts[4];
        bool have_points = false;
        for (int i = 0; i < count; i++) {
            if (tokens[i] && strcmp(tokens[i], "P") == 0) {
                RtFloat* p = (RtFloat*)values[i];
                for (int j = 0; j < 4; j++) {
                    cpts[j] = rh_vec3_create(p[j*3], p[j*3+1], p[j*3+2]);
                }
                have_points = true;
                break;
            }
        }
        if (have_points) {
            RhPrimitive p = rh_prim_create_patch_bilinear(cpts);
            ri_parse_primvars(&p, tokens, values, count);
            ri_add_geometry(&p);
            rh_prim_free_data(&p);
        }
    }
}

void RiPatch(RtToken type, ...) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    RtToken tokens[16];
    RtPointer values[16];
    int count = 0;

    va_list ap;
    va_start(ap, type);
    RtToken token;
    while ((token = va_arg(ap, RtToken)) != RI_NULL && count < 16) {
        tokens[count] = token;
        values[count] = va_arg(ap, RtPointer);
        count++;
    }
    va_end(ap);

    RiPatchV(type, tokens, values, count);
}

void RiGeometryV(RtToken type, RtToken* tokens, RtPointer* values, int count) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    if (strcmp(type, "teapot") == 0) {
        // Get the current basis and step values for teapot patches
        RhMat4 u_basis = ri_curr()->u_basis;
        RhMat4 v_basis = ri_curr()->v_basis;

        // Iterate through all teapot patches
        for (int i = 0; i < TEAPOT_NUM_PATCHES; i++) {
            // Get the 16 control points for this patch
            RhVec3 cpts[16];
            for (int j = 0; j < 16; j++) {
                cpts[j].x = teapot_patches[i][j][0];
                cpts[j].y = teapot_patches[i][j][1];
                cpts[j].z = teapot_patches[i][j][2];
            }

            // Create and add the bicubic patch
            RhPrimitive p = rh_prim_create_patch_bicubic(cpts, u_basis, v_basis);
            ri_parse_primvars(&p, tokens, values, count);
            ri_add_geometry(&p);
            rh_prim_free_data(&p);
        }
    }
}

void RiGeometry(RtToken type, ...) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    RtToken tokens[16];
    RtPointer values[16];
    int count = 0;

    va_list ap;
    va_start(ap, type);
    RtToken token;
    while ((token = va_arg(ap, RtToken)) != RI_NULL && count < 16) {
        tokens[count] = token;
        values[count] = va_arg(ap, RtPointer);
        count++;
    }
    va_end(ap);

    RiGeometryV(type, tokens, values, count);
}

// --- Surface Shader ---

/* Set a C builtin surface shader. Handles paintedplastic specially for params. */
static bool ri_set_builtin_surface(RtToken name, RhShaderFunc func,
                                   RtToken* tokens, RtPointer* values, int count) {
    if (strcmp(name, "paintedplastic") == 0) {
        ri_curr()->current_surface_shader = rh_shader_surface_paintedplastic;
        RhPaintedPlasticParams* params = (RhPaintedPlasticParams*)malloc(sizeof(RhPaintedPlasticParams));
        if (params) {
            params->Ka = 1.0f;
            params->Kd = 0.5f;
            params->Ks = 0.5f;
            params->roughness = 0.1f;
            params->specular_color = (RhColor){1.0f, 1.0f, 1.0f};
            params->texturename[0] = '\0';
            params->texture = NULL;

            for (int i = 0; i < count; i++) {
                RtToken token = tokens[i];
                if (!token) break;
                if (strcmp(token, "texturename") == 0) {
                    RtToken texname = (RtToken)values[i];
                    if (texname) {
                        size_t tlen = strlen(texname);
                        if (tlen >= sizeof(params->texturename))
                            tlen = sizeof(params->texturename) - 1;
                        memcpy(params->texturename, texname, tlen);
                        params->texturename[tlen] = '\0';
                        params->texture = ri_texture_load(texname, RH_TEX_RGB);
                        if (!params->texture) {
                            xpt_warn("ri.primitive", "Failed to load texture '%s'", texname);
                        }
                    }
                } else if (strcmp(token, "Ka") == 0) {
                    params->Ka = *(RtFloat*)values[i];
                } else if (strcmp(token, "Kd") == 0) {
                    params->Kd = *(RtFloat*)values[i];
                } else if (strcmp(token, "Ks") == 0) {
                    params->Ks = *(RtFloat*)values[i];
                } else if (strcmp(token, "roughness") == 0) {
                    params->roughness = *(RtFloat*)values[i];
                } else if (strcmp(token, "specularcolor") == 0) {
                    RtColor* col = (RtColor*)values[i];
                    params->specular_color.r = (*col)[0];
                    params->specular_color.g = (*col)[1];
                    params->specular_color.b = (*col)[2];
                }
            }
            ri_curr()->current_shader_params = params;
        } else {
            ri_curr()->current_shader_params = NULL;
        }
        return true;
    }

    if (func) {
        ri_curr()->current_surface_shader = func;
        ri_curr()->current_shader_params = NULL;
        return true;
    }
    return false;
}

/* Try to set an SL surface shader from a loaded program. */
static bool ri_set_sl_surface(RhSLProgram* prog, RtToken* tokens,
                              RtPointer* values, int count) {
    RhSLShader* shader = rh_sl_shader_create(prog);
    if (!shader) return false;

    for (int i = 0; i < count; i++) {
        if (!tokens[i]) break;
        float* fval = (float*)values[i];
        if (rh_sl_shader_set_param(shader, tokens[i], fval, 1) == 0)
            continue;
        rh_sl_shader_set_string_param(shader, tokens[i],
                                      (const char*)values[i]);
    }
    ri_curr()->current_surface_shader = rh_sl_vm_shader_exec;
    ri_curr()->current_shader_params = shader;
    return true;
}

/* Callback state for RiSurfaceV search path iteration */
typedef struct {
    RtToken name;
    RtToken* tokens;
    RtPointer* values;
    int count;
    bool found;
} SurfaceSearchCtx;

static bool surface_search_cb(bool is_builtin, const char* dir, void* user) {
    SurfaceSearchCtx* s = (SurfaceSearchCtx*)user;

    if (is_builtin) {
        /* Check paintedplastic specially */
        if (strcmp(s->name, "paintedplastic") == 0) {
            s->found = ri_set_builtin_surface(s->name, NULL,
                                              s->tokens, s->values, s->count);
            if (s->found)
                xpt_debug("ri.primitive", "shader: loaded builtin surface '%s'", s->name);
            return s->found;
        }
        RhShaderFunc func = builtin_surface_lookup(s->name);
        if (func) {
            xpt_debug("ri.primitive", "shader: loaded builtin surface '%s'", s->name);
            ri_curr()->current_surface_shader = func;
            ri_curr()->current_shader_params = NULL;
            s->found = true;
            return true;
        }
    } else {
        RhSLProgram* prog = sl_cache_find(s->name);
        if (!prog) prog = sl_try_load_from_dir(s->name, dir);
        if (prog) {
            s->found = ri_set_sl_surface(prog, s->tokens, s->values, s->count);
            return s->found;
        }
    }
    return false;
}

void RiSurfaceV(RtToken name, RtToken* tokens, RtPointer* values, int count) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    SurfaceSearchCtx sctx = {name, tokens, values, count, false};
    ri_iterate_searchpath(surface_search_cb, &sctx);

    if (!sctx.found) {
        xpt_warn("ri.primitive", "shader '%s' not found", name);
    } else {
        ri_curr()->current_shader_to_world = ri_curr()->transform;
    }
}

void RiSurface(RtToken name, ...) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    // Build token/value arrays from varargs
    RtToken tokens[16];
    RtPointer values[16];
    int count = 0;

    va_list ap;
    va_start(ap, name);
    RtToken token;
    while ((token = va_arg(ap, RtToken)) != RI_NULL && count < 16) {
        tokens[count] = token;
        values[count] = va_arg(ap, RtPointer);
        count++;
    }
    va_end(ap);

    RiSurfaceV(name, tokens, values, count);
}

// --- Atmosphere Shader ---

/* Set a C builtin atmosphere shader (depthcue or fog) with params. */
static bool ri_set_builtin_atmosphere(RtToken name, RtToken* tokens,
                                      RtPointer* values, int count) {
    if (strcmp(name, "depthcue") == 0) {
        ri_curr()->current_atmosphere_shader = rh_shader_atmosphere_depthcue;
        RhDepthcueParams* params = (RhDepthcueParams*)malloc(sizeof(RhDepthcueParams));
        if (params) {
            params->mindistance = 0.0f;
            params->maxdistance = 1.0f;
            params->background = (RhColor){0.0f, 0.0f, 0.0f};

            for (int i = 0; i < count; i++) {
                RtToken token = tokens[i];
                if (!token) break;
                if (strcmp(token, "mindistance") == 0) {
                    params->mindistance = *(RtFloat*)values[i];
                } else if (strcmp(token, "maxdistance") == 0) {
                    params->maxdistance = *(RtFloat*)values[i];
                } else if (strcmp(token, "background") == 0) {
                    RtFloat* col = (RtFloat*)values[i];
                    params->background.r = col[0];
                    params->background.g = col[1];
                    params->background.b = col[2];
                }
            }
            ri_curr()->current_atmosphere_params = params;
        } else {
            ri_curr()->current_atmosphere_params = NULL;
        }
        return true;
    } else if (strcmp(name, "fog") == 0) {
        ri_curr()->current_atmosphere_shader = rh_shader_atmosphere_fog;
        RhFogParams* params = (RhFogParams*)malloc(sizeof(RhFogParams));
        if (params) {
            params->distance = 1.0f;
            params->background = (RhColor){0.0f, 0.0f, 0.0f};

            for (int i = 0; i < count; i++) {
                RtToken token = tokens[i];
                if (!token) break;
                if (strcmp(token, "distance") == 0) {
                    params->distance = *(RtFloat*)values[i];
                } else if (strcmp(token, "background") == 0) {
                    RtFloat* col = (RtFloat*)values[i];
                    params->background.r = col[0];
                    params->background.g = col[1];
                    params->background.b = col[2];
                }
            }
            ri_curr()->current_atmosphere_params = params;
        } else {
            ri_curr()->current_atmosphere_params = NULL;
        }
        return true;
    }
    return false;
}

/* Callback state for RiAtmosphereV search path iteration */
typedef struct {
    RtToken name;
    RtToken* tokens;
    RtPointer* values;
    int count;
    bool found;
} AtmosphereSearchCtx;

static bool atmosphere_search_cb(bool is_builtin, const char* dir, void* user) {
    AtmosphereSearchCtx* s = (AtmosphereSearchCtx*)user;

    if (is_builtin) {
        if (builtin_atmosphere_known(s->name)) {
            s->found = ri_set_builtin_atmosphere(s->name, s->tokens,
                                                 s->values, s->count);
            if (s->found)
                xpt_debug("ri.primitive", "shader: loaded builtin atmosphere '%s'", s->name);
            return s->found;
        }
    } else {
        RhSLProgram* prog = sl_cache_find(s->name);
        if (!prog) prog = sl_try_load_from_dir(s->name, dir);
        if (prog) {
            RhSLShader* shader = rh_sl_shader_create(prog);
            if (shader) {
                for (int i = 0; i < s->count; i++) {
                    if (!s->tokens[i]) break;
                    float* fval = (float*)s->values[i];
                    if (rh_sl_shader_set_param(shader, s->tokens[i], fval, 1) == 0)
                        continue;
                    rh_sl_shader_set_string_param(shader, s->tokens[i],
                                                  (const char*)s->values[i]);
                }
                ri_curr()->current_atmosphere_shader = rh_sl_vm_shader_exec;
                ri_curr()->current_atmosphere_params = shader;
                s->found = true;
                return true;
            }
        }
    }
    return false;
}

void RiAtmosphereV(RtToken name, RtToken* tokens, RtPointer* values, int count) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    if (!name || name[0] == '\0') {
        ri_curr()->current_atmosphere_shader = NULL;
        ri_curr()->current_atmosphere_params = NULL;
        return;
    }

    AtmosphereSearchCtx sctx = {name, tokens, values, count, false};
    ri_iterate_searchpath(atmosphere_search_cb, &sctx);

    if (!sctx.found) {
        xpt_warn("ri.primitive", "Unknown atmosphere shader '%s'", name);
    }
}

void RiAtmosphere(RtToken name, ...) {
    RiContextData* ctx = ri_get_ctx();
    if (!ctx) return;

    RtToken tokens[16];
    RtPointer values[16];
    int count = 0;

    va_list ap;
    va_start(ap, name);
    RtToken token;
    while ((token = va_arg(ap, RtToken)) != RI_NULL && count < 16) {
        tokens[count] = token;
        values[count] = va_arg(ap, RtPointer);
        count++;
    }
    va_end(ap);

    RiAtmosphereV(name, tokens, values, count);
}
