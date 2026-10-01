/*
 * fatgl - GLSL shaders and programs: glslang (relaxed Vulkan rules: loose
 * uniforms become a default uniform block per stage) compiles at link time
 * to SPIR-V, fatmap runs it. Uniform locations, samplers, uniform blocks and
 * vertex inputs come from the SPIR-V (names, decorations).
 */
#include "fgl.h"
#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#include <stdio.h>

/* ---- objects (shaders and programs share names) ---- */
static fgl_shader* fgl_shader_get(fgl_ctx* c, GLuint name)
{
    for (int i = 0; i < c->nshaders; i++)
        if (c->shaders[i].used && c->shaders[i].name == name) return &c->shaders[i];
    return NULL;
}

fgl_program* fgl_program_get(fgl_ctx* c, GLuint name)
{
    for (int i = 0; i < c->nprogs; i++)
        if (c->progs[i].used && c->progs[i].name == name) return &c->progs[i];
    return NULL;
}

static GLuint fgl_new_name(fgl_ctx* c)
{
    if (!c->next_sp_name) c->next_sp_name = 1;
    return c->next_sp_name++;
}

GLuint APIENTRY glCreateShader(GLenum type)
{
    fgl_ctx* c = fgl_cur();
    if (!c) return 0;
    if (type != GL_VERTEX_SHADER && type != GL_FRAGMENT_SHADER && type != GL_GEOMETRY_SHADER) {
        fgl_error(GL_INVALID_ENUM);
        return 0;
    }
    int slot = -1;
    for (int i = 0; i < c->nshaders && slot < 0; i++)
        if (!c->shaders[i].used) slot = i;
    if (slot < 0) {
        fgl_shader* n = (fgl_shader*)realloc(c->shaders, ((size_t)c->nshaders + 16) * sizeof(fgl_shader));
        if (!n) return 0;
        memset(n + c->nshaders, 0, 16 * sizeof(fgl_shader));
        c->shaders = n, slot = c->nshaders, c->nshaders += 16;
    }
    fgl_shader* s = &c->shaders[slot];
    memset(s, 0, sizeof(*s));
    s->used = 1, s->type = type, s->name = fgl_new_name(c);
    return s->name;
}

static void fgl_shader_free(fgl_shader* s)
{
    free(s->src);
    free(s->log);
    memset(s, 0, sizeof(*s));
}

static int fgl_shader_attached(fgl_ctx* c, GLuint name)
{
    for (int i = 0; i < c->nprogs; i++)
        for (int k = 0; c->progs[i].used && k < c->progs[i].nshaders; k++)
            if (c->progs[i].shaders[k] == name) return 1;
    return 0;
}

void APIENTRY glDeleteShader(GLuint name)
{
    FGL_CTX_OR_RETURN(c);
    fgl_shader* s = name ? fgl_shader_get(c, name) : NULL;
    if (!s) return;
    if (fgl_shader_attached(c, name)) s->delete_pending = 1;
    else fgl_shader_free(s);
}

GLboolean APIENTRY glIsShader(GLuint name)
{
    fgl_ctx* c = fgl_cur();
    return c && name && fgl_shader_get(c, name) ? GL_TRUE : GL_FALSE;
}

void APIENTRY glShaderSource(GLuint name, GLsizei count, const GLchar* const* str, const GLint* len)
{
    FGL_CTX_OR_RETURN(c);
    fgl_shader* s = fgl_shader_get(c, name);
    if (!s) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    size_t total = 1;
    for (GLsizei i = 0; i < count; i++) total += len && len[i] >= 0 ? (size_t)len[i] : strlen(str[i]);
    char* src = (char*)malloc(total);
    if (!src) {
        fgl_error(GL_OUT_OF_MEMORY);
        return;
    }
    size_t o = 0;
    for (GLsizei i = 0; i < count; i++) {
        size_t n = len && len[i] >= 0 ? (size_t)len[i] : strlen(str[i]);
        memcpy(src + o, str[i], n);
        o += n;
    }
    src[o] = 0;
    free(s->src);
    s->src = src;
}

/* ---- glslang ---- */
static int g_glslang_init;

static glslang_stage_t fgl_stage(GLenum type) { return type == GL_VERTEX_SHADER ? GLSLANG_STAGE_VERTEX : GLSLANG_STAGE_FRAGMENT; }

static glslang_shader_t* fgl_glslang_shader(GLenum type, const char* src, int ntexcoords, char** log)
{
    if (!g_glslang_init) g_glslang_init = glslang_initialize_process();
    glslang_input_t in;
    memset(&in, 0, sizeof(in));
    in.language                = GLSLANG_SOURCE_GLSL;
    in.stage                   = fgl_stage(type);
    in.client                  = GLSLANG_CLIENT_VULKAN; /* relaxed rules: GL GLSL compiled as Vulkan GLSL */
    in.client_version          = GLSLANG_TARGET_VULKAN_1_0;
    in.target_language         = GLSLANG_TARGET_SPV;
    in.target_language_version = GLSLANG_TARGET_SPV_1_0;
    int   builtins = 0; /* (the fgl_Builtins block shows up in the reflection) */
    char* up       = fgl_glsl_upgrade(src, type, ntexcoords, &builtins); /* legacy GLSL -> 3.30 core */
    in.code                    = up ? up : src;
    in.default_version         = 110;
    in.default_profile         = GLSLANG_NO_PROFILE;
    in.messages                = GLSLANG_MSG_DEFAULT_BIT;
    in.resource                = glslang_default_resource();
    glslang_shader_t* sh       = glslang_shader_create(&in);
    if (!sh) {
        free(up);
        return NULL;
    }
    glslang_shader_set_options(sh, GLSLANG_SHADER_AUTO_MAP_BINDINGS | GLSLANG_SHADER_AUTO_MAP_LOCATIONS |
                                       GLSLANG_SHADER_VULKAN_RULES_RELAXED | GLSLANG_SHADER_BINDINGS_PER_RESOURCE_TYPE);
    glslang_shader_set_default_uniform_block_name(sh, "gl_DefaultUniformBlock");
    glslang_shader_set_default_uniform_block_set_and_binding(sh, 0, type == GL_VERTEX_SHADER ? FGL_DEF_VS : FGL_DEF_FS);
    int ok = glslang_shader_preprocess(sh, &in) && glslang_shader_parse(sh, &in);
    free(up); /* glslang reads the source (it keeps the pointer) until parsing is done */
    if (log) {
        const char* l = glslang_shader_get_info_log(sh);
        *log          = l && *l ? _strdup(l) : NULL;
    }
    if (!ok) {
        glslang_shader_delete(sh);
        return NULL;
    }
    return sh;
}

void APIENTRY glCompileShader(GLuint name)
{
    FGL_CTX_OR_RETURN(c);
    fgl_shader* s = fgl_shader_get(c, name);
    if (!s) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    free(s->log);
    s->log      = NULL;
    s->compiled = 0;
    if (s->type == GL_GEOMETRY_SHADER) {
        s->log = _strdup("fatgl: geometry shaders are not supported yet\n");
        return;
    }
    glslang_shader_t* sh = s->src ? fgl_glslang_shader(s->type, s->src, 8, &s->log) : NULL;
    s->compiled          = sh != NULL;
    if (!sh) fgl_log("shader %u (%s) does not compile:\n%s--- source ---\n%s\n--- end ---\n", name,
                     s->type == GL_VERTEX_SHADER ? "vertex" : "fragment", s->log ? s->log : "", s->src ? s->src : "");
    if (sh) glslang_shader_delete(sh);
}

void APIENTRY glGetShaderiv(GLuint name, GLenum p, GLint* v)
{
    FGL_CTX_OR_RETURN(c);
    fgl_shader* s = fgl_shader_get(c, name);
    if (!s) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    switch (p) {
    case GL_SHADER_TYPE: *v = (GLint)s->type; break;
    case GL_COMPILE_STATUS: *v = s->compiled; break;
    case GL_DELETE_STATUS: *v = s->delete_pending; break;
    case GL_INFO_LOG_LENGTH: *v = s->log ? (GLint)strlen(s->log) + 1 : 0; break;
    case GL_SHADER_SOURCE_LENGTH: *v = s->src ? (GLint)strlen(s->src) + 1 : 0; break;
    default: fgl_error(GL_INVALID_ENUM); break;
    }
}

static void fgl_copy_log(const char* log, GLsizei max, GLsizei* len, GLchar* out)
{
    size_t n = log ? strlen(log) : 0;
    if (max <= 0) {
        if (len) *len = 0;
        return;
    }
    if (n > (size_t)max - 1) n = (size_t)max - 1;
    if (n) memcpy(out, log, n);
    out[n] = 0;
    if (len) *len = (GLsizei)n;
}

void APIENTRY glGetShaderInfoLog(GLuint name, GLsizei max, GLsizei* len, GLchar* out)
{
    FGL_CTX_OR_RETURN(c);
    fgl_shader* s = fgl_shader_get(c, name);
    if (!s) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    fgl_copy_log(s->log, max, len, out);
}

void APIENTRY glGetShaderSource(GLuint name, GLsizei max, GLsizei* len, GLchar* out)
{
    FGL_CTX_OR_RETURN(c);
    fgl_shader* s = fgl_shader_get(c, name);
    if (s) fgl_copy_log(s->src, max, len, out);
}

/* ---- programs ---- */
GLuint APIENTRY glCreateProgram(void)
{
    fgl_ctx* c = fgl_cur();
    if (!c) return 0;
    int slot = -1;
    for (int i = 0; i < c->nprogs && slot < 0; i++)
        if (!c->progs[i].used) slot = i;
    if (slot < 0) {
        fgl_program* n = (fgl_program*)realloc(c->progs, ((size_t)c->nprogs + 16) * sizeof(fgl_program));
        if (!n) return 0;
        memset(n + c->nprogs, 0, 16 * sizeof(fgl_program));
        c->progs = n, slot = c->nprogs, c->nprogs += 16;
    }
    fgl_program* p = &c->progs[slot];
    memset(p, 0, sizeof(*p));
    p->used = 1, p->name = fgl_new_name(c);
    return p->name;
}

static void fgl_program_reset(fgl_program* p) /* the link results */
{
    fm3d_spirv_destroy(p->sp);
    p->sp = NULL;
    free(p->def[0]), free(p->def[1]);
    p->def[0] = p->def[1] = NULL;
    p->defsize[0] = p->defsize[1] = 0;
    free(p->u);
    p->u = NULL, p->nu = 0, p->nblocks = 0, p->nin = 0, p->linked = 0;
    free(p->log);
    p->log = NULL;
}

void APIENTRY glDeleteProgram(GLuint name)
{
    FGL_CTX_OR_RETURN(c);
    fgl_program* p = name ? fgl_program_get(c, name) : NULL;
    if (!p) return;
    if (c->program == name) {
        p->delete_pending = 1;
        return;
    }
    fgl_flush(c);
    GLuint sh[8];
    int    n = p->nshaders;
    memcpy(sh, p->shaders, sizeof(sh));
    fgl_program_reset(p);
    memset(p, 0, sizeof(*p));
    for (int i = 0; i < n; i++) { /* shaders waiting for their deletion */
        fgl_shader* s = fgl_shader_get(c, sh[i]);
        if (s && s->delete_pending && !fgl_shader_attached(c, sh[i])) fgl_shader_free(s);
    }
}

GLboolean APIENTRY glIsProgram(GLuint name)
{
    fgl_ctx* c = fgl_cur();
    return c && name && fgl_program_get(c, name) ? GL_TRUE : GL_FALSE;
}

void APIENTRY glAttachShader(GLuint prog, GLuint shader)
{
    FGL_CTX_OR_RETURN(c);
    fgl_program* p = fgl_program_get(c, prog);
    if (!p || !fgl_shader_get(c, shader) || p->nshaders == 8) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    for (int i = 0; i < p->nshaders; i++)
        if (p->shaders[i] == shader) {
            fgl_error(GL_INVALID_OPERATION);
            return;
        }
    p->shaders[p->nshaders++] = shader;
}

void APIENTRY glDetachShader(GLuint prog, GLuint shader)
{
    FGL_CTX_OR_RETURN(c);
    fgl_program* p = fgl_program_get(c, prog);
    if (!p) return;
    for (int i = 0; i < p->nshaders; i++)
        if (p->shaders[i] == shader) {
            p->shaders[i] = p->shaders[--p->nshaders];
            fgl_shader* s = fgl_shader_get(c, shader);
            if (s && s->delete_pending && !fgl_shader_attached(c, shader)) fgl_shader_free(s);
            return;
        }
}

void APIENTRY glBindAttribLocation(GLuint prog, GLuint index, const GLchar* name)
{
    FGL_CTX_OR_RETURN(c);
    fgl_program* p = fgl_program_get(c, prog);
    if (!p || index >= FGL_ATTRIBS) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    for (int i = 0; i < p->nbind; i++)
        if (!strcmp(p->bind_attrib[i].name, name)) {
            p->bind_attrib[i].index = (int)index;
            return;
        }
    if (p->nbind < FGL_ATTRIBS) {
        snprintf(p->bind_attrib[p->nbind].name, sizeof(p->bind_attrib[0].name), "%s", name);
        p->bind_attrib[p->nbind++].index = (int)index;
    }
}

void APIENTRY glBindFragDataLocation(GLuint prog, GLuint color, const GLchar* name) { (void)prog, (void)color, (void)name; }
GLint APIENTRY glGetFragDataLocation(GLuint prog, const GLchar* name)
{
    (void)prog, (void)name;
    return 0; /* one color output */
}

/* ---- SPIR-V reflection ---- */
#define SP_OpName           5
#define SP_OpMemberName     6
#define SP_OpTypeBool       20
#define SP_OpTypeInt        21
#define SP_OpTypeFloat      22
#define SP_OpTypeVector     23
#define SP_OpTypeMatrix     24
#define SP_OpTypeImage      25
#define SP_OpTypeSampledImage 27
#define SP_OpTypeArray      28
#define SP_OpTypeStruct     30
#define SP_OpTypePointer    32
#define SP_OpConstant       43
#define SP_OpVariable       59
#define SP_OpDecorate       71
#define SP_OpMemberDecorate 72

typedef struct sp_mod {
    const uint32_t* w;
    size_t          n;
    uint32_t        bound;
    uint32_t*       at; /* id -> word offset of its defining instruction (types, constants, variables) */
} sp_mod;

static const uint32_t* sp_def(const sp_mod* m, uint32_t id) { return id < m->bound && m->at[id] ? m->w + m->at[id] : NULL; }

static const char* sp_name(const sp_mod* m, uint32_t id, int member) /* OpName / OpMemberName */
{
    for (size_t p = 5; p < m->n;) {
        uint32_t wc = m->w[p] >> 16, op = m->w[p] & 0xffff;
        if (!wc) break;
        if (op == SP_OpName && member < 0 && m->w[p + 1] == id) return (const char*)(m->w + p + 2);
        if (op == SP_OpMemberName && member >= 0 && m->w[p + 1] == id && (int)m->w[p + 2] == member) return (const char*)(m->w + p + 3);
        p += wc;
    }
    return "";
}

static int sp_deco(const sp_mod* m, uint32_t id, int member, uint32_t deco) /* decoration value, -1 if none */
{
    for (size_t p = 5; p < m->n;) {
        uint32_t wc = m->w[p] >> 16, op = m->w[p] & 0xffff;
        if (!wc) break;
        if (op == SP_OpDecorate && member < 0 && m->w[p + 1] == id && m->w[p + 2] == deco) return wc > 3 ? (int)m->w[p + 3] : 1;
        if (op == SP_OpMemberDecorate && member >= 0 && m->w[p + 1] == id && (int)m->w[p + 2] == member && m->w[p + 3] == deco)
            return wc > 4 ? (int)m->w[p + 4] : 1;
        p += wc;
    }
    return -1;
}

static int sp_init(sp_mod* m, const uint32_t* w, size_t n)
{
    m->w = w, m->n = n, m->bound = w[3];
    m->at = (uint32_t*)calloc(m->bound, sizeof(uint32_t));
    if (!m->at) return 0;
    for (size_t p = 5; p < n;) {
        uint32_t wc = w[p] >> 16, op = w[p] & 0xffff;
        if (!wc) break;
        if (op >= SP_OpTypeBool && op <= SP_OpTypePointer && w[p + 1] < m->bound) m->at[w[p + 1]] = (uint32_t)p;
        if ((op == SP_OpConstant || op == SP_OpVariable) && w[p + 2] < m->bound) m->at[w[p + 2]] = (uint32_t)p;
        p += wc;
    }
    return 1;
}

/* GL type, size in std140 bytes (scalars / vectors / matrices by stride) */
static GLenum sp_gltype(const sp_mod* m, uint32_t t, int mstride, int* bytes)
{
    const uint32_t* d = sp_def(m, t);
    if (!d) return 0;
    uint32_t op = d[0] & 0xffff;
    if (op == SP_OpTypeFloat) return *bytes = 4, GL_FLOAT;
    if (op == SP_OpTypeBool) return *bytes = 4, GL_BOOL;
    if (op == SP_OpTypeInt) return *bytes = 4, d[3] ? GL_INT : GL_UNSIGNED_INT;
    if (op == SP_OpTypeVector) {
        int    eb;
        GLenum e = sp_gltype(m, d[2], 0, &eb);
        int    n = (int)d[3];
        *bytes   = 4 * n;
        if (e == GL_FLOAT) return GL_FLOAT_VEC2 + (GLenum)(n - 2);
        if (e == GL_INT) return GL_INT_VEC2 + (GLenum)(n - 2);
        if (e == GL_UNSIGNED_INT) return GL_UNSIGNED_INT_VEC2 + (GLenum)(n - 2);
        return GL_BOOL_VEC2 + (GLenum)(n - 2);
    }
    if (op == SP_OpTypeMatrix) {
        const uint32_t* col  = sp_def(m, d[2]);
        int             cols = (int)d[3], rows = col ? (int)col[3] : 4;
        *bytes               = cols * (mstride > 0 ? mstride : 16);
        static const GLenum mt[3][3] = { { GL_FLOAT_MAT2, GL_FLOAT_MAT2x3, GL_FLOAT_MAT2x4 },
                                         { GL_FLOAT_MAT3x2, GL_FLOAT_MAT3, GL_FLOAT_MAT3x4 },
                                         { GL_FLOAT_MAT4x2, GL_FLOAT_MAT4x3, GL_FLOAT_MAT4 } };
        return mt[cols - 2][rows - 2];
    }
    if (op == SP_OpTypeSampledImage) { /* the GL sampler type of the image's dim / depth / arrayed */
        const uint32_t* im = sp_def(m, d[2]);
        *bytes             = 0;
        if (!im) return GL_SAMPLER_2D;
        uint32_t dim = im[3], shadow = im[4] == 1, arr = im[5];
        switch (dim) {
        case 0: return arr ? (shadow ? GL_SAMPLER_1D_ARRAY_SHADOW : GL_SAMPLER_1D_ARRAY) : (shadow ? GL_SAMPLER_1D_SHADOW : GL_SAMPLER_1D);
        case 2: return GL_SAMPLER_3D;
        case 3: return arr ? GL_SAMPLER_CUBE_MAP_ARRAY : (shadow ? GL_SAMPLER_CUBE_SHADOW : GL_SAMPLER_CUBE);
        case 4: return shadow ? GL_SAMPLER_2D_RECT_SHADOW : GL_SAMPLER_2D_RECT;
        case 5: return GL_SAMPLER_BUFFER;
        default: return arr ? (shadow ? GL_SAMPLER_2D_ARRAY_SHADOW : GL_SAMPLER_2D_ARRAY) : (shadow ? GL_SAMPLER_2D_SHADOW : GL_SAMPLER_2D);
        }
    }
    return 0;
}

static int fgl_uniform_add(fgl_program* p, const char* name, GLenum type, int count, int stage, int off, int astride, int mstride)
{
    for (int i = 0; i < p->nu; i++)
        if (!strcmp(p->u[i].name, name)) {
            p->u[i].off[stage] = off;
            return i;
        }
    fgl_uniform* n = (fgl_uniform*)realloc(p->u, ((size_t)p->nu + 1) * sizeof(fgl_uniform));
    if (!n) return -1;
    p->u        = n;
    fgl_uniform* u = &p->u[p->nu];
    memset(u, 0, sizeof(*u));
    snprintf(u->name, sizeof(u->name), "%s", name);
    u->type = type, u->count = count, u->off[0] = u->off[1] = -1, u->off[stage] = off;
    u->astride = astride, u->mstride = mstride, u->sampler_binding = -1;
    return p->nu++;
}

/* the members of a (default block) struct type, recursively: "s.m", "a[2].m" */
static int sp_struct_size(const sp_mod* m, uint32_t st);
static void sp_members(const sp_mod* m, fgl_program* p, int stage, uint32_t st, int base, const char* prefix)
{
    const uint32_t* d = sp_def(m, st);
    if (!d || (d[0] & 0xffff) != SP_OpTypeStruct) return;
    int nm = (int)(d[0] >> 16) - 2;
    for (int k = 0; k < nm; k++) {
        uint32_t        mt  = d[2 + k];
        int             off = base + sp_deco(m, st, k, 35 /* Offset */);
        int             ms  = sp_deco(m, st, k, 7 /* MatrixStride */);
        char            name[96];
        const uint32_t* md = sp_def(m, mt);
        snprintf(name, sizeof(name), "%s%s", prefix, sp_name(m, st, k));
        int count = 1, astride = 0;
        if (md && (md[0] & 0xffff) == SP_OpTypeArray) {
            const uint32_t* len = sp_def(m, md[3]);
            count               = len ? (int)len[3] : 1;
            astride             = sp_deco(m, mt, -1, 6 /* ArrayStride */);
            mt                  = md[2];
            md                  = sp_def(m, mt);
        }
        if (md && (md[0] & 0xffff) == SP_OpTypeStruct) { /* struct members: one entry per element and member */
            for (int e = 0; e < count; e++) {
                char pre[96];
                if (count > 1) snprintf(pre, sizeof(pre), "%s[%d].", name, e);
                else snprintf(pre, sizeof(pre), "%s.", name);
                sp_members(m, p, stage, mt, off + e * astride, pre);
            }
            continue;
        }
        int    bytes;
        GLenum t = sp_gltype(m, mt, ms, &bytes);
        if (t) fgl_uniform_add(p, name, t, count, stage, off, astride, ms > 0 ? ms : 16);
    }
}

static int sp_struct_size(const sp_mod* m, uint32_t st)
{
    const uint32_t* d = sp_def(m, st);
    if (!d || (d[0] & 0xffff) != SP_OpTypeStruct) return 0;
    int nm = (int)(d[0] >> 16) - 2, size = 0;
    for (int k = 0; k < nm; k++) {
        int             off = sp_deco(m, st, k, 35), ms = sp_deco(m, st, k, 7), bytes = 0;
        const uint32_t* md  = sp_def(m, d[2 + k]);
        if (md && (md[0] & 0xffff) == SP_OpTypeArray) {
            const uint32_t* len = sp_def(m, md[3]);
            bytes               = (len ? (int)len[3] : 1) * sp_deco(m, d[2 + k], -1, 6);
        } else if (md && (md[0] & 0xffff) == SP_OpTypeStruct) {
            bytes = sp_struct_size(m, d[2 + k]);
        } else {
            sp_gltype(m, d[2 + k], ms, &bytes);
        }
        if (off + bytes > size) size = off + bytes;
    }
    return (size + 15) & ~15;
}

static void fgl_reflect(fgl_program* p, int stage, const uint32_t* w, size_t n)
{
    sp_mod m;
    if (!sp_init(&m, w, n)) return;
    for (size_t q = 5; q < n;) {
        uint32_t wc = w[q] >> 16, op = w[q] & 0xffff;
        if (!wc) break;
        if (op == SP_OpVariable) {
            uint32_t        id = w[q + 2], sc = w[q + 3];
            const uint32_t* pt = sp_def(&m, w[q + 1]);
            uint32_t        t  = pt ? pt[3] : 0;
            int             binding = sp_deco(&m, id, -1, 33 /* Binding */);
            if (sc == 2 /* Uniform */ && (binding == FGL_DEF_VS || binding == FGL_DEF_FS)) { /* the default block */
                sp_members(&m, p, stage, t, 0, "");
                p->defsize[stage] = sp_struct_size(&m, t);
            } else if (sc == 2 && p->nblocks < 14) { /* a named uniform block */
                const char* bn = sp_name(&m, t, -1);
                int         bi = -1;
                for (int i = 0; i < p->nblocks; i++)
                    if (!strcmp(p->blocks[i].name, bn)) bi = i;
                if (bi < 0) {
                    bi = p->nblocks++;
                    memset(&p->blocks[bi], 0, sizeof(p->blocks[bi]));
                    snprintf(p->blocks[bi].name, sizeof(p->blocks[bi].name), "%s", bn);
                    p->blocks[bi].spv_binding[0] = p->blocks[bi].spv_binding[1] = -1;
                    p->blocks[bi].gl_binding = 0;
                }
                p->blocks[bi].spv_binding[stage] = binding;
                p->blocks[bi].size               = sp_struct_size(&m, t);
            } else if (sc == 0 /* UniformConstant */) { /* a sampler */
                int    sb;
                GLenum st = sp_gltype(&m, t, 0, &sb);
                int    u  = fgl_uniform_add(p, sp_name(&m, id, -1), st ? st : GL_SAMPLER_2D, 1, stage, 0, 0, 0);
                if (u >= 0) p->u[u].sampler_binding = binding;
            } else if (sc == 1 /* Input */ && stage == 0 && sp_deco(&m, id, -1, 11 /* BuiltIn */) < 0 && p->nin < FGL_ATTRIBS) {
                int loc = sp_deco(&m, id, -1, 30 /* Location */);
                snprintf(p->in[p->nin].name, sizeof(p->in[0].name), "%s", sp_name(&m, id, -1));
                p->in[p->nin++].location = loc;
                if (loc > p->max_loc) p->max_loc = loc;
            }
        }
        q += wc;
    }
    free(m.at);
}

static void fgl_log_append(fgl_program* p, const char* s)
{
    if (!s || !*s) return;
    size_t a = p->log ? strlen(p->log) : 0, b = strlen(s);
    char*  n = (char*)realloc(p->log, a + b + 1);
    if (!n) return;
    memcpy(n + a, s, b + 1);
    p->log = n;
}

/* glslang numbers the uniform blocks of each stage on its own (a vertex
 * block and a fragment block both get binding 0), fatmap binds blocks per
 * context: give every block name of the program its own binding (the same
 * name in both stages shares one; the default block keeps FGL_DEF_VS) */
static void fgl_unique_block_bindings(uint32_t* w[2], const size_t n[2])
{
    char names[FM3D_MAX_UNIFORM_BLOCKS][96];
    int  nnames = 0;
    for (int k = 0; k < 2; k++) {
        sp_mod m;
        if (!sp_init(&m, w[k], n[k])) continue;
        for (size_t q = 5; q < n[k];) {
            uint32_t wc = w[k][q] >> 16, op = w[k][q] & 0xffff;
            if (!wc) break;
            if (op == 71 /* OpDecorate */ && w[k][q + 2] == 33 /* Binding */ && wc >= 4) {
                uint32_t        var = w[k][q + 1];
                const uint32_t* vd  = sp_def(&m, var);
                if (vd && (vd[0] & 0xffff) == SP_OpVariable && vd[3] == 2 /* Uniform */ && w[k][q + 3] != FGL_DEF_VS) {
                    const uint32_t* pt = sp_def(&m, vd[1]);
                    const char*     bn = pt ? sp_name(&m, pt[3], -1) : "";
                    int             b  = -1;
                    for (int i = 0; i < nnames; i++)
                        if (!strcmp(names[i], bn)) b = i;
                    if (b < 0 && nnames < FM3D_MAX_UNIFORM_BLOCKS - 1) {
                        b = nnames++;
                        snprintf(names[b], sizeof(names[b]), "%s", bn);
                    }
                    if (b >= 0) w[k][q + 3] = (uint32_t)(b >= FGL_DEF_VS ? b + 1 : b);
                }
            }
            q += wc;
        }
        free(m.at);
    }
}

void APIENTRY glLinkProgram(GLuint name)
{
    FGL_CTX_OR_RETURN(c);
    fgl_program* p = fgl_program_get(c, name);
    if (!p) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    fgl_flush(c);
    fgl_program_reset(p);
    fgl_shader* st[2] = { NULL, NULL };
    for (int i = 0; i < p->nshaders; i++) {
        fgl_shader* s = fgl_shader_get(c, p->shaders[i]);
        if (!s) continue;
        if (s->type == GL_GEOMETRY_SHADER) {
            fgl_log_append(p, "fatgl: geometry shaders are not supported yet\n");
            fgl_log("program %u does not link: geometry shaders are not supported yet\n", name);
            return;
        }
        st[s->type == GL_FRAGMENT_SHADER] = s;
    }
    /* one stage only: GL runs the fixed function for the other (Doom 3's
     * shadow volumes link a vertex shader alone, colors masked) */
    static fgl_shader fixed_stage[2];
    if (!st[0] && !st[1]) {
        fgl_log_append(p, "fatgl: the program has no shaders\n");
        fgl_log("program %u does not link: no shaders\n", name);
        return;
    }
    if (!st[1]) {
        int         legacy_color = st[0]->src && strstr(st[0]->src, "gl_FrontColor");
        fgl_shader* f            = &fixed_stage[1];
        memset(f, 0, sizeof(*f));
        f->type = GL_FRAGMENT_SHADER;
        f->src  = legacy_color ? (char*)"void main() { gl_FragColor = gl_Color; }\n"
                               : (char*)"#version 330 core\nout vec4 fgl_fixed_color;\nvoid main() { fgl_fixed_color = vec4(1.0); }\n";
        st[1]   = f;
    }
    if (!st[0]) {
        fgl_shader* v = &fixed_stage[0];
        memset(v, 0, sizeof(*v));
        v->type = GL_VERTEX_SHADER;
        v->src  = (char*)"void main() { gl_Position = ftransform(); gl_FrontColor = gl_Color; gl_TexCoord[0] = gl_MultiTexCoord0; }\n";
        st[0]   = v;
    }
    glslang_shader_t*  sh[2] = { NULL, NULL };
    glslang_program_t* gp    = NULL;
    uint32_t*          words[2] = { NULL, NULL };
    size_t             nw[2]    = { 0, 0 };
    int                ok       = 1;
    int                ntc      = 0; /* legacy gl_TexCoord[]: one size for both stages */
    for (int k = 0; k < 2; k++) {
        int n = st[k]->src ? fgl_glsl_texcoords(st[k]->src) : 0;
        ntc   = n > ntc ? n : ntc;
    }
    for (int k = 0; k < 2 && ok; k++) {
        char* log = NULL;
        sh[k]     = st[k]->src ? fgl_glslang_shader(st[k]->type, st[k]->src, ntc, &log) : NULL;
        if (log) fgl_log_append(p, log);
        free(log);
        ok = sh[k] != NULL;
    }
    if (ok) {
        gp = glslang_program_create();
        glslang_program_add_shader(gp, sh[0]);
        glslang_program_add_shader(gp, sh[1]);
        ok = glslang_program_link(gp, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT) && glslang_program_map_io(gp);
        fgl_log_append(p, glslang_program_get_info_log(gp));
    }
    for (int k = 0; k < 2 && ok; k++) {
        glslang_program_SPIRV_generate(gp, fgl_stage(st[k]->type));
        nw[k]    = glslang_program_SPIRV_get_size(gp);
        words[k] = (uint32_t*)malloc(nw[k] * sizeof(uint32_t));
        if (!words[k]) ok = 0;
        else glslang_program_SPIRV_get(gp, words[k]);
    }
    if (ok) fgl_unique_block_bindings(words, nw);
    if (ok) {
        p->max_loc = -1;
        fgl_reflect(p, 0, words[0], nw[0]);
        fgl_reflect(p, 1, words[1], nw[1]);
        /* vertex inputs read a canonical stream: location L at L * 16 bytes */
        fm3d_vertex_attrib at[FGL_ATTRIBS];
        int                na = 0;
        for (int i = 0; i < p->nin; i++)
            if (p->in[i].location >= 0 && p->in[i].location < FGL_ATTRIBS) at[na].location = p->in[i].location, at[na].components = 4,
                                                                                at[na].offset = 16 * p->in[i].location, na++;
        char err[256];
        p->sp = fm3d_spirv_create(words[0], nw[0], words[1], nw[1], at, na, err, sizeof(err));
        if (!p->sp) {
            fgl_log_append(p, "fatgl: ");
            fgl_log_append(p, err);
            fgl_log_append(p, "\n");
            ok = 0;
        }
    }
    if (ok) {
        p->prog = fm3d_spirv_program(p->sp);
        for (int k = 0; k < 2; k++)
            if (p->defsize[k]) p->def[k] = (uint8_t*)calloc(1, (size_t)p->defsize[k]);
        p->linked = 1;
    }
    if (!p->linked) {
        fgl_log("program %u does not link:\n%s\n", name, p->log ? p->log : "");
        fgl_debug(c, GL_DEBUG_SOURCE_SHADER_COMPILER, GL_DEBUG_TYPE_ERROR, GL_DEBUG_SEVERITY_HIGH, p->log ? p->log : "link failed");
    }
    for (int k = 0; k < 2; k++) {
        free(words[k]);
        if (sh[k]) glslang_shader_delete(sh[k]);
    }
    if (gp) glslang_program_delete(gp);
}

void APIENTRY glUseProgram(GLuint name)
{
    FGL_CTX_OR_RETURN(c);
    fgl_program* p = name ? fgl_program_get(c, name) : NULL;
    if (name && (!p || !p->linked)) {
        fgl_error(GL_INVALID_OPERATION);
        return;
    }
    fgl_program* old = c->program ? fgl_program_get(c, c->program) : NULL;
    c->program       = name;
    if (old && old->delete_pending && old != p) glDeleteProgram(old->name);
}

void APIENTRY glValidateProgram(GLuint name) { (void)name; }

void APIENTRY glGetProgramiv(GLuint name, GLenum q, GLint* v)
{
    FGL_CTX_OR_RETURN(c);
    fgl_program* p = fgl_program_get(c, name);
    if (!p) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    switch (q) {
    case GL_LINK_STATUS: *v = p->linked; break;
    case GL_VALIDATE_STATUS: *v = p->linked; break;
    case GL_DELETE_STATUS: *v = p->delete_pending; break;
    case GL_INFO_LOG_LENGTH: *v = p->log ? (GLint)strlen(p->log) + 1 : 0; break;
    case GL_ATTACHED_SHADERS: *v = p->nshaders; break;
    case GL_ACTIVE_UNIFORMS: *v = p->nu; break;
    case GL_ACTIVE_UNIFORM_MAX_LENGTH: {
        int m = 0;
        for (int i = 0; i < p->nu; i++) m = (int)strlen(p->u[i].name) + 1 > m ? (int)strlen(p->u[i].name) + 1 : m;
        *v = m;
        break;
    }
    case GL_ACTIVE_ATTRIBUTES: *v = p->nin; break;
    case GL_ACTIVE_ATTRIBUTE_MAX_LENGTH: *v = 96; break;
    case GL_ACTIVE_UNIFORM_BLOCKS: *v = p->nblocks; break;
    default: fgl_error(GL_INVALID_ENUM); break;
    }
}

void APIENTRY glGetProgramInfoLog(GLuint name, GLsizei max, GLsizei* len, GLchar* out)
{
    FGL_CTX_OR_RETURN(c);
    fgl_program* p = fgl_program_get(c, name);
    if (!p) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    fgl_copy_log(p->log, max, len, out);
}

void APIENTRY glGetAttachedShaders(GLuint name, GLsizei max, GLsizei* count, GLuint* out)
{
    FGL_CTX_OR_RETURN(c);
    fgl_program* p = fgl_program_get(c, name);
    int          n = 0;
    for (int i = 0; p && i < p->nshaders && i < max; i++) out[n++] = p->shaders[i];
    if (count) *count = n;
}

/* ---- attributes ---- */
GLint APIENTRY glGetAttribLocation(GLuint name, const GLchar* attr)
{
    fgl_ctx* c = fgl_cur();
    if (!c) return -1;
    fgl_program* p = fgl_program_get(c, name);
    if (!p || !p->linked) {
        fgl_error(GL_INVALID_OPERATION);
        return -1;
    }
    for (int i = 0; i < p->nin; i++)
        if (!strcmp(p->in[i].name, attr)) {
            for (int b = 0; b < p->nbind; b++) /* glBindAttribLocation wins */
                if (!strcmp(p->bind_attrib[b].name, attr)) return p->bind_attrib[b].index;
            return p->in[i].location;
        }
    return -1;
}

void APIENTRY glGetActiveAttrib(GLuint name, GLuint index, GLsizei max, GLsizei* len, GLint* size, GLenum* type, GLchar* out)
{
    FGL_CTX_OR_RETURN(c);
    fgl_program* p = fgl_program_get(c, name);
    if (!p || index >= (GLuint)p->nin) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    fgl_copy_log(p->in[index].name, max, len, out);
    if (size) *size = 1;
    if (type) *type = GL_FLOAT_VEC4;
}

/* ---- uniforms: location = index * 1024 + array element ---- */
GLint APIENTRY glGetUniformLocation(GLuint name, const GLchar* uname)
{
    fgl_ctx* c = fgl_cur();
    if (!c) return -1;
    fgl_program* p = fgl_program_get(c, name);
    if (!p || !p->linked) {
        fgl_error(GL_INVALID_OPERATION);
        return -1;
    }
    char        base[96];
    int         elem = 0;
    const char* br   = strrchr(uname, '[');
    snprintf(base, sizeof(base), "%s", uname);
    if (br && br[strlen(br) - 1] == ']' && !strchr(br, '.')) { /* "a[3]" */
        base[br - uname] = 0;
        elem             = atoi(br + 1);
    }
    for (int i = 0; i < p->nu; i++)
        if (!strcmp(p->u[i].name, base) && elem < p->u[i].count) return i * 1024 + elem;
    return -1;
}

void APIENTRY glGetActiveUniform(GLuint name, GLuint index, GLsizei max, GLsizei* len, GLint* size, GLenum* type, GLchar* out)
{
    FGL_CTX_OR_RETURN(c);
    fgl_program* p = fgl_program_get(c, name);
    if (!p || index >= (GLuint)p->nu) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    fgl_copy_log(p->u[index].name, max, len, out);
    if (size) *size = p->u[index].count;
    if (type) *type = p->u[index].type;
}

static fgl_uniform* fgl_uni(fgl_ctx* c, GLint loc, int* elem)
{
    fgl_program* p = c->program ? fgl_program_get(c, c->program) : NULL;
    if (!p || loc < 0 || loc / 1024 >= p->nu) {
        if (loc != -1) fgl_error(GL_INVALID_OPERATION); /* -1 is silently ignored */
        return NULL;
    }
    *elem = loc % 1024;
    return &p->u[loc / 1024];
}

/* n elements of `comps` 32 bit components each, from src (floats or ints) */
static void fgl_uniform_write(fgl_ctx* c, GLint loc, GLsizei n, int comps, const void* src)
{
    int          elem;
    fgl_uniform* u = fgl_uni(c, loc, &elem);
    if (!u) return;
    fgl_program* p = fgl_program_get(c, c->program);
    if (fgl_is_sampler(u->type)) { /* the texture unit */
        u->unit = *(const GLint*)src;
        return;
    }
    for (int k = 0; k < 2; k++) {
        if (u->off[k] < 0 || !p->def[k]) continue;
        for (GLsizei e = 0; e < n && elem + e < u->count; e++) {
            int at = u->off[k] + (elem + e) * u->astride;
            if (at + comps * 4 > p->defsize[k]) continue;
            memcpy(p->def[k] + at, (const uint32_t*)src + (size_t)e * (size_t)comps, (size_t)comps * 4);
        }
    }
}

static void fgl_uniform_matrix(fgl_ctx* c, GLint loc, GLsizei n, int cols, int rows, GLboolean transpose, const GLfloat* v)
{
    int          elem;
    fgl_uniform* u = fgl_uni(c, loc, &elem);
    if (!u) return;
    fgl_program* p = fgl_program_get(c, c->program);
    for (int k = 0; k < 2; k++) {
        if (u->off[k] < 0 || !p->def[k]) continue;
        for (GLsizei e = 0; e < n && elem + e < u->count; e++) {
            const GLfloat* m = v + (size_t)e * (size_t)(cols * rows);
            for (int cc = 0; cc < cols; cc++)
                for (int rr = 0; rr < rows; rr++) {
                    int at = u->off[k] + (elem + e) * u->astride + cc * u->mstride + rr * 4;
                    if (at + 4 > p->defsize[k]) continue;
                    float f = transpose ? m[rr * cols + cc] : m[cc * rows + rr];
                    memcpy(p->def[k] + at, &f, 4);
                }
        }
    }
}

#define FGL_UF(nm, comps, T, ...)                                                                                       \
    void APIENTRY nm(GLint loc, __VA_ARGS__)                                                                            \
    {                                                                                                                   \
        FGL_CTX_OR_RETURN(c);                                                                                           \
        T v[4] = { FGL_ARGS };                                                                                          \
        fgl_uniform_write(c, loc, 1, comps, v);                                                                         \
    }
#define FGL_ARGS x
FGL_UF(glUniform1f, 1, GLfloat, GLfloat x)
FGL_UF(glUniform1i, 1, GLint, GLint x)
FGL_UF(glUniform1ui, 1, GLuint, GLuint x)
#undef FGL_ARGS
#define FGL_ARGS x, y
FGL_UF(glUniform2f, 2, GLfloat, GLfloat x, GLfloat y)
FGL_UF(glUniform2i, 2, GLint, GLint x, GLint y)
FGL_UF(glUniform2ui, 2, GLuint, GLuint x, GLuint y)
#undef FGL_ARGS
#define FGL_ARGS x, y, z
FGL_UF(glUniform3f, 3, GLfloat, GLfloat x, GLfloat y, GLfloat z)
FGL_UF(glUniform3i, 3, GLint, GLint x, GLint y, GLint z)
FGL_UF(glUniform3ui, 3, GLuint, GLuint x, GLuint y, GLuint z)
#undef FGL_ARGS
#define FGL_ARGS x, y, z, w
FGL_UF(glUniform4f, 4, GLfloat, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
FGL_UF(glUniform4i, 4, GLint, GLint x, GLint y, GLint z, GLint w)
FGL_UF(glUniform4ui, 4, GLuint, GLuint x, GLuint y, GLuint z, GLuint w)
#undef FGL_ARGS

#define FGL_UV(nm, comps, T)                                                                                            \
    void APIENTRY nm(GLint loc, GLsizei n, const T* v)                                                                  \
    {                                                                                                                   \
        FGL_CTX_OR_RETURN(c);                                                                                           \
        fgl_uniform_write(c, loc, n, comps, v);                                                                         \
    }
FGL_UV(glUniform1fv, 1, GLfloat)
FGL_UV(glUniform2fv, 2, GLfloat)
FGL_UV(glUniform3fv, 3, GLfloat)
FGL_UV(glUniform4fv, 4, GLfloat)
FGL_UV(glUniform1iv, 1, GLint)
FGL_UV(glUniform2iv, 2, GLint)
FGL_UV(glUniform3iv, 3, GLint)
FGL_UV(glUniform4iv, 4, GLint)
FGL_UV(glUniform1uiv, 1, GLuint)
FGL_UV(glUniform2uiv, 2, GLuint)
FGL_UV(glUniform3uiv, 3, GLuint)
FGL_UV(glUniform4uiv, 4, GLuint)

#define FGL_UM(nm, cols, rows)                                                                                          \
    void APIENTRY nm(GLint loc, GLsizei n, GLboolean transpose, const GLfloat* v)                                       \
    {                                                                                                                   \
        FGL_CTX_OR_RETURN(c);                                                                                           \
        fgl_uniform_matrix(c, loc, n, cols, rows, transpose, v);                                                        \
    }
FGL_UM(glUniformMatrix2fv, 2, 2)
FGL_UM(glUniformMatrix3fv, 3, 3)
FGL_UM(glUniformMatrix4fv, 4, 4)
FGL_UM(glUniformMatrix2x3fv, 2, 3)
FGL_UM(glUniformMatrix3x2fv, 3, 2)
FGL_UM(glUniformMatrix2x4fv, 2, 4)
FGL_UM(glUniformMatrix4x2fv, 4, 2)
FGL_UM(glUniformMatrix3x4fv, 3, 4)
FGL_UM(glUniformMatrix4x3fv, 4, 3)

void APIENTRY glGetUniformfv(GLuint name, GLint loc, GLfloat* v)
{
    FGL_CTX_OR_RETURN(c);
    fgl_program* p = fgl_program_get(c, name);
    if (!p || loc < 0 || loc / 1024 >= p->nu) {
        fgl_error(GL_INVALID_OPERATION);
        return;
    }
    fgl_uniform* u = &p->u[loc / 1024];
    int          k = u->off[0] >= 0 ? 0 : 1, bytes;
    (void)bytes;
    if (fgl_is_sampler(u->type)) {
        v[0] = (float)u->unit;
        return;
    }
    if (u->off[k] >= 0 && p->def[k]) memcpy(v, p->def[k] + u->off[k] + (loc % 1024) * u->astride, 4);
}

/* ---- uniform blocks ---- */
GLuint APIENTRY glGetUniformBlockIndex(GLuint name, const GLchar* block)
{
    fgl_ctx* c = fgl_cur();
    if (!c) return GL_INVALID_INDEX;
    fgl_program* p = fgl_program_get(c, name);
    for (int i = 0; p && i < p->nblocks; i++)
        if (!strcmp(p->blocks[i].name, block)) return (GLuint)i;
    return GL_INVALID_INDEX;
}

void APIENTRY glUniformBlockBinding(GLuint name, GLuint index, GLuint binding)
{
    FGL_CTX_OR_RETURN(c);
    fgl_program* p = fgl_program_get(c, name);
    if (!p || index >= (GLuint)p->nblocks || binding >= FGL_UBO_BINDS) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    p->blocks[index].gl_binding = (int)binding;
}

void APIENTRY glGetActiveUniformBlockiv(GLuint name, GLuint index, GLenum q, GLint* v)
{
    FGL_CTX_OR_RETURN(c);
    fgl_program* p = fgl_program_get(c, name);
    if (!p || index >= (GLuint)p->nblocks) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    switch (q) {
    case GL_UNIFORM_BLOCK_BINDING: *v = p->blocks[index].gl_binding; break;
    case GL_UNIFORM_BLOCK_DATA_SIZE: *v = p->blocks[index].size; break;
    case GL_UNIFORM_BLOCK_NAME_LENGTH: *v = (GLint)strlen(p->blocks[index].name) + 1; break;
    case GL_UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER: *v = p->blocks[index].spv_binding[0] >= 0; break;
    case GL_UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER: *v = p->blocks[index].spv_binding[1] >= 0; break;
    default: fgl_unimplemented("glGetActiveUniformBlockiv (this query)"); break;
    }
}

void APIENTRY glGetActiveUniformBlockName(GLuint name, GLuint index, GLsizei max, GLsizei* len, GLchar* out)
{
    FGL_CTX_OR_RETURN(c);
    fgl_program* p = fgl_program_get(c, name);
    if (!p || index >= (GLuint)p->nblocks) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    fgl_copy_log(p->blocks[index].name, max, len, out);
}
