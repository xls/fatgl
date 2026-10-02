/*
 * fatgl - stencil state, sync objects, queries, sampler objects, buffer
 * clears, indexed / 64 bit queries, texture queries and the remaining
 * small state of GL 1.x .. 3.3.
 */
#include "fgl.h"
#include <stdio.h>

void fgl_misc_init(fgl_ctx* c)
{
    for (int f = 0; f < 2; f++) {
        c->stencil[f].func = GL_ALWAYS, c->stencil[f].ref = 0, c->stencil[f].mask = c->stencil[f].wmask = 0xFFFFFFFFu;
        c->stencil[f].sfail = c->stencil[f].dpfail = c->stencil[f].dppass = GL_KEEP;
    }
    c->point_size = c->line_width = 1.0f;
    c->provoking_vertex = GL_LAST_VERTEX_CONVENTION;
    c->logic_op         = GL_COPY;
}

void fgl_misc_free(fgl_ctx* c)
{
    free(c->builtins);
    free(c->samplers);
    free(c->queries);
    c->samplers = NULL, c->queries = NULL, c->nsamplers = c->nqueries = 0;
}

/* ---- stencil ---- */
static fm3d_stencil_op fgl_sop(GLenum op)
{
    switch (op) {
    case GL_ZERO: return FM3D_STENCIL_ZERO;
    case GL_REPLACE: return FM3D_STENCIL_REPLACE;
    case GL_INCR: return FM3D_STENCIL_INCR;
    case GL_DECR: return FM3D_STENCIL_DECR;
    case GL_INVERT: return FM3D_STENCIL_INVERT;
    case GL_INCR_WRAP: return FM3D_STENCIL_INCR_WRAP;
    case GL_DECR_WRAP: return FM3D_STENCIL_DECR_WRAP;
    default: return FM3D_STENCIL_KEEP;
    }
}

void fgl_sync_stencil(fgl_ctx* c)
{
    fm3d_ctx* f = c->c3;
    fm3d_set_stencil_test(f, (c->enables & FGL_E_STENCIL) != 0);
    for (int i = 0; i < 2; i++) {
        fm3d_face face = i ? FM3D_FACE_BACK : FM3D_FACE_FRONT;
        GLint     ref  = c->stencil[i].ref < 0 ? 0 : (c->stencil[i].ref > 255 ? 255 : c->stencil[i].ref);
        fm3d_set_stencil_func(f, face, (fm3d_compare)(c->stencil[i].func - GL_NEVER), (uint8_t)ref, (uint8_t)c->stencil[i].mask);
        fm3d_set_stencil_op(f, face, fgl_sop(c->stencil[i].sfail), fgl_sop(c->stencil[i].dpfail), fgl_sop(c->stencil[i].dppass));
        fm3d_set_stencil_write_mask(f, face, (uint8_t)c->stencil[i].wmask);
    }
}

static int fgl_faces(GLenum face, int* f0, int* f1)
{
    *f0 = face == GL_BACK ? 1 : 0;
    *f1 = face == GL_FRONT ? 0 : 1;
    if (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) {
        fgl_error(GL_INVALID_ENUM);
        return 0;
    }
    return 1;
}

void APIENTRY glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask)
{
    FGL_CTX_OR_RETURN(c);
    int f0, f1;
    if (func < GL_NEVER || func > GL_ALWAYS) {
        fgl_error(GL_INVALID_ENUM);
        return;
    }
    if (!fgl_faces(face, &f0, &f1)) return;
    for (int i = f0; i <= f1; i++) c->stencil[i].func = func, c->stencil[i].ref = ref, c->stencil[i].mask = mask;
}
void APIENTRY glStencilFunc(GLenum func, GLint ref, GLuint mask) { glStencilFuncSeparate(GL_FRONT_AND_BACK, func, ref, mask); }

void APIENTRY glStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass)
{
    FGL_CTX_OR_RETURN(c);
    int f0, f1;
    if (!fgl_faces(face, &f0, &f1)) return;
    for (int i = f0; i <= f1; i++) c->stencil[i].sfail = sfail, c->stencil[i].dpfail = dpfail, c->stencil[i].dppass = dppass;
}
void APIENTRY glStencilOp(GLenum sfail, GLenum dpfail, GLenum dppass) { glStencilOpSeparate(GL_FRONT_AND_BACK, sfail, dpfail, dppass); }

void APIENTRY glStencilMaskSeparate(GLenum face, GLuint mask)
{
    FGL_CTX_OR_RETURN(c);
    int f0, f1;
    if (!fgl_faces(face, &f0, &f1)) return;
    for (int i = f0; i <= f1; i++) c->stencil[i].wmask = mask;
}
void APIENTRY glStencilMask(GLuint mask) { glStencilMaskSeparate(GL_FRONT_AND_BACK, mask); }

/* ---- sync objects: fatmap work completes at a flush ---- */
typedef struct fgl_sync_obj {
    uint32_t magic;
    int      signaled;
} fgl_sync_obj;
#define FGL_SYNC_MAGIC 0x53594e43u

static fgl_sync_obj* fgl_sync_of(GLsync s)
{
    fgl_sync_obj* o = (fgl_sync_obj*)s;
    return o && o->magic == FGL_SYNC_MAGIC ? o : NULL;
}

GLsync APIENTRY glFenceSync(GLenum cond, GLbitfield flags)
{
    fgl_ctx* c = fgl_cur();
    if (!c) return NULL;
    if (cond != GL_SYNC_GPU_COMMANDS_COMPLETE || flags != 0) {
        fgl_error(cond != GL_SYNC_GPU_COMMANDS_COMPLETE ? GL_INVALID_ENUM : GL_INVALID_VALUE);
        return NULL;
    }
    fgl_sync_obj* o = (fgl_sync_obj*)calloc(1, sizeof(*o));
    if (!o) {
        fgl_error(GL_OUT_OF_MEMORY);
        return NULL;
    }
    o->magic = FGL_SYNC_MAGIC;
    return (GLsync)o;
}

GLboolean APIENTRY glIsSync(GLsync s) { return fgl_sync_of(s) ? GL_TRUE : GL_FALSE; }

void APIENTRY glDeleteSync(GLsync s)
{
    fgl_sync_obj* o = fgl_sync_of(s);
    if (!o) {
        if (s) fgl_error(GL_INVALID_VALUE);
        return;
    }
    o->magic = 0;
    free(o);
}

GLenum APIENTRY glClientWaitSync(GLsync s, GLbitfield flags, GLuint64 timeout)
{
    (void)flags, (void)timeout;
    fgl_ctx*      c = fgl_cur();
    fgl_sync_obj* o = fgl_sync_of(s);
    if (!o) {
        fgl_error(GL_INVALID_VALUE);
        return GL_WAIT_FAILED;
    }
    if (o->signaled) return GL_ALREADY_SIGNALED;
    if (c) fgl_flush(c);
    o->signaled = 1;
    return GL_CONDITION_SATISFIED;
}

void APIENTRY glWaitSync(GLsync s, GLbitfield flags, GLuint64 timeout)
{
    (void)flags, (void)timeout;
    if (!fgl_sync_of(s)) fgl_error(GL_INVALID_VALUE); /* one queue: nothing to wait for */
}

void APIENTRY glGetSynciv(GLsync s, GLenum p, GLsizei n, GLsizei* len, GLint* v)
{
    fgl_ctx*      c = fgl_cur();
    fgl_sync_obj* o = fgl_sync_of(s);
    if (!o) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    if (n < 1) return;
    GLint r;
    switch (p) {
    case GL_OBJECT_TYPE: r = GL_SYNC_FENCE; break;
    case GL_SYNC_CONDITION: r = GL_SYNC_GPU_COMMANDS_COMPLETE; break;
    case GL_SYNC_FLAGS: r = 0; break;
    case GL_SYNC_STATUS:
        if (!o->signaled && c) fgl_flush(c), o->signaled = 1;
        r = GL_SIGNALED;
        break;
    default: fgl_error(GL_INVALID_ENUM); return;
    }
    v[0] = r;
    if (len) *len = 1;
}

/* ---- queries ---- */
static fgl_query* fgl_query_get(fgl_ctx* c, GLuint name, int create)
{
    for (int i = 0; i < c->nqueries; i++)
        if (c->queries[i].used && c->queries[i].name == name) return &c->queries[i];
    if (!create || !name) return NULL;
    int slot = -1;
    for (int i = 0; i < c->nqueries && slot < 0; i++)
        if (!c->queries[i].used) slot = i;
    if (slot < 0) {
        fgl_query* n = (fgl_query*)realloc(c->queries, ((size_t)c->nqueries + 16) * sizeof(fgl_query));
        if (!n) return NULL;
        memset(n + c->nqueries, 0, 16 * sizeof(fgl_query));
        c->queries = n, slot = c->nqueries, c->nqueries += 16;
    }
    fgl_query* q = &c->queries[slot];
    memset(q, 0, sizeof(*q));
    q->used = 1, q->name = name;
    return q;
}

static GLuint g_next_query = 1, g_next_sampler = 1;

void APIENTRY glGenQueries(GLsizei n, GLuint* out)
{
    FGL_CTX_OR_RETURN(c);
    for (GLsizei i = 0; i < n; i++) {
        while (fgl_query_get(c, g_next_query, 0)) g_next_query++;
        out[i] = g_next_query++;
        fgl_query_get(c, out[i], 1);
    }
}

void APIENTRY glDeleteQueries(GLsizei n, const GLuint* names)
{
    FGL_CTX_OR_RETURN(c);
    for (GLsizei i = 0; i < n; i++) {
        fgl_query* q = names[i] ? fgl_query_get(c, names[i], 0) : NULL;
        if (!q) continue;
        for (int k = 0; k < 4; k++)
            if (c->active_query[k] == names[i]) c->active_query[k] = 0;
        memset(q, 0, sizeof(*q));
    }
}

GLboolean APIENTRY glIsQuery(GLuint name)
{
    fgl_ctx* c = fgl_cur();
    fgl_query* q = c && name ? fgl_query_get(c, name, 0) : NULL;
    return q && q->target ? GL_TRUE : GL_FALSE;
}

static int fgl_query_slot(GLenum target)
{
    switch (target) {
    case GL_SAMPLES_PASSED: return 0;
    case GL_ANY_SAMPLES_PASSED: case GL_ANY_SAMPLES_PASSED_CONSERVATIVE: return 1;
    case GL_PRIMITIVES_GENERATED: case GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN: return 2;
    case GL_TIME_ELAPSED: return 3;
    default: return -1;
    }
}

static uint64_t fgl_now_ns(void)
{
    static LARGE_INTEGER f;
    LARGE_INTEGER        t;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return (uint64_t)((double)t.QuadPart * 1e9 / (double)f.QuadPart);
}

/* the counter a query target accumulates (fatmap statistics after a flush) */
static uint64_t fgl_query_counter(fgl_ctx* c, GLenum target)
{
    if (target == GL_TIME_ELAPSED || target == GL_TIMESTAMP) {
        fgl_flush(c);
        return fgl_now_ns();
    }
    fgl_flush(c);
    fm3d_stats st = fm3d_get_stats(c->c3);
    if (target == GL_PRIMITIVES_GENERATED) return st.triangles_in;
    if (target == GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN) return 0;
    return st.fragments_shaded;
}

void APIENTRY glBeginQuery(GLenum target, GLuint name)
{
    FGL_CTX_OR_RETURN(c);
    int k = fgl_query_slot(target);
    if (k < 0) {
        fgl_error(GL_INVALID_ENUM);
        return;
    }
    fgl_query* q = name ? fgl_query_get(c, name, 1) : NULL;
    if (!q || c->active_query[k] || (q->target && q->target != target)) {
        fgl_error(GL_INVALID_OPERATION);
        return;
    }
    q->target = target, q->result = 0;
    q->start  = fgl_query_counter(c, target);
    c->active_query[k] = name;
}

void APIENTRY glEndQuery(GLenum target)
{
    FGL_CTX_OR_RETURN(c);
    int k = fgl_query_slot(target);
    if (k < 0 || !c->active_query[k]) {
        fgl_error(k < 0 ? GL_INVALID_ENUM : GL_INVALID_OPERATION);
        return;
    }
    fgl_query* q = fgl_query_get(c, c->active_query[k], 0);
    c->active_query[k] = 0;
    if (!q) return;
    uint64_t now = fgl_query_counter(c, target);
    q->result    = now >= q->start ? now - q->start : 0;
    if (k == 1) q->result = q->result != 0;
}

void APIENTRY glQueryCounter(GLuint name, GLenum target)
{
    FGL_CTX_OR_RETURN(c);
    fgl_query* q = target == GL_TIMESTAMP && name ? fgl_query_get(c, name, 1) : NULL;
    if (!q) {
        fgl_error(target == GL_TIMESTAMP ? GL_INVALID_OPERATION : GL_INVALID_ENUM);
        return;
    }
    q->target = GL_TIMESTAMP;
    q->result = fgl_query_counter(c, GL_TIMESTAMP);
}

void APIENTRY glGetQueryiv(GLenum target, GLenum p, GLint* v)
{
    FGL_CTX_OR_RETURN(c);
    int k = fgl_query_slot(target);
    if (p == GL_QUERY_COUNTER_BITS) *v = target == GL_ANY_SAMPLES_PASSED || target == GL_ANY_SAMPLES_PASSED_CONSERVATIVE ? 1 : 64;
    else if (p == GL_CURRENT_QUERY) *v = k >= 0 ? (GLint)c->active_query[k] : 0;
    else fgl_error(GL_INVALID_ENUM);
}

static int fgl_query_result(fgl_ctx* c, GLuint name, GLenum p, uint64_t* r)
{
    fgl_query* q = name ? fgl_query_get(c, name, 0) : NULL;
    if (!q || !q->target) {
        fgl_error(GL_INVALID_OPERATION);
        return 0;
    }
    if (p == GL_QUERY_RESULT_AVAILABLE) *r = 1; /* results are final at glEndQuery */
    else if (p == GL_QUERY_RESULT || p == GL_QUERY_RESULT_NO_WAIT) *r = q->result;
    else {
        fgl_error(GL_INVALID_ENUM);
        return 0;
    }
    return 1;
}

void APIENTRY glGetQueryObjectui64v(GLuint name, GLenum p, GLuint64* v)
{
    FGL_CTX_OR_RETURN(c);
    uint64_t r;
    if (fgl_query_result(c, name, p, &r)) *v = r;
}
void APIENTRY glGetQueryObjecti64v(GLuint name, GLenum p, GLint64* v)
{
    FGL_CTX_OR_RETURN(c);
    uint64_t r;
    if (fgl_query_result(c, name, p, &r)) *v = (GLint64)r;
}
void APIENTRY glGetQueryObjectuiv(GLuint name, GLenum p, GLuint* v)
{
    FGL_CTX_OR_RETURN(c);
    uint64_t r;
    if (fgl_query_result(c, name, p, &r)) *v = r > 0xFFFFFFFFu ? 0xFFFFFFFFu : (GLuint)r;
}
void APIENTRY glGetQueryObjectiv(GLuint name, GLenum p, GLint* v)
{
    FGL_CTX_OR_RETURN(c);
    uint64_t r;
    if (fgl_query_result(c, name, p, &r)) *v = r > 0x7FFFFFFF ? 0x7FFFFFFF : (GLint)r;
}

/* conditional rendering: draws always run (a conservative answer) */
void APIENTRY glBeginConditionalRender(GLuint id, GLenum mode) { (void)id, (void)mode; }
void APIENTRY glEndConditionalRender(void) {}

/* ---- sampler objects ---- */
fgl_sampler* fgl_sampler_get(fgl_ctx* c, GLuint name)
{
    for (int i = 0; i < c->nsamplers; i++)
        if (c->samplers[i].used && c->samplers[i].name == name) return &c->samplers[i];
    return NULL;
}

void APIENTRY glGenSamplers(GLsizei n, GLuint* out)
{
    FGL_CTX_OR_RETURN(c);
    for (GLsizei i = 0; i < n; i++) {
        while (fgl_sampler_get(c, g_next_sampler)) g_next_sampler++;
        int slot = -1;
        for (int k = 0; k < c->nsamplers && slot < 0; k++)
            if (!c->samplers[k].used) slot = k;
        if (slot < 0) {
            fgl_sampler* s = (fgl_sampler*)realloc(c->samplers, ((size_t)c->nsamplers + 16) * sizeof(fgl_sampler));
            if (!s) {
                fgl_error(GL_OUT_OF_MEMORY);
                return;
            }
            memset(s + c->nsamplers, 0, 16 * sizeof(fgl_sampler));
            c->samplers = s, slot = c->nsamplers, c->nsamplers += 16;
        }
        fgl_sampler* s = &c->samplers[slot];
        memset(s, 0, sizeof(*s));
        s->used = 1, s->name = out[i] = g_next_sampler++;
        s->min_filter = GL_NEAREST_MIPMAP_LINEAR, s->mag_filter = GL_LINEAR;
        s->wrap_s = s->wrap_t = s->wrap_r = GL_REPEAT;
        s->min_lod = -1000, s->max_lod = 1000, s->aniso = 1;
        s->compare_mode = GL_NONE, s->compare_func = GL_LEQUAL;
    }
}

void APIENTRY glDeleteSamplers(GLsizei n, const GLuint* names)
{
    FGL_CTX_OR_RETURN(c);
    for (GLsizei i = 0; i < n; i++) {
        fgl_sampler* s = names[i] ? fgl_sampler_get(c, names[i]) : NULL;
        if (!s) continue;
        for (int u = 0; u < FGL_UNITS; u++)
            if (c->unit_sampler[u] == names[i]) c->unit_sampler[u] = 0;
        memset(s, 0, sizeof(*s));
    }
}

GLboolean APIENTRY glIsSampler(GLuint name)
{
    fgl_ctx* c = fgl_cur();
    return c && name && fgl_sampler_get(c, name) ? GL_TRUE : GL_FALSE;
}

void APIENTRY glBindSampler(GLuint unit, GLuint name)
{
    FGL_CTX_OR_RETURN(c);
    if (unit >= FGL_UNITS) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    if (name && !fgl_sampler_get(c, name)) {
        fgl_error(GL_INVALID_OPERATION);
        return;
    }
    c->unit_sampler[unit] = name;
}

static void fgl_sampler_set(GLuint name, GLenum p, const GLfloat* f, const GLint* iv)
{
    FGL_CTX_OR_RETURN(c);
    fgl_sampler* s = fgl_sampler_get(c, name);
    if (!s) {
        fgl_error(GL_INVALID_OPERATION);
        return;
    }
    GLint i = iv ? iv[0] : (GLint)f[0];
    float v = f ? f[0] : (float)iv[0];
    switch (p) {
    case GL_TEXTURE_MIN_FILTER: s->min_filter = (GLenum)i; break;
    case GL_TEXTURE_MAG_FILTER: s->mag_filter = (GLenum)i; break;
    case GL_TEXTURE_WRAP_S: s->wrap_s = (GLenum)i; break;
    case GL_TEXTURE_WRAP_T: s->wrap_t = (GLenum)i; break;
    case GL_TEXTURE_WRAP_R: s->wrap_r = (GLenum)i; break;
    case GL_TEXTURE_MIN_LOD: s->min_lod = v; break;
    case GL_TEXTURE_MAX_LOD: s->max_lod = v; break;
    case GL_TEXTURE_LOD_BIAS: s->lod_bias = v; break;
    case GL_TEXTURE_COMPARE_MODE: s->compare_mode = (GLenum)i; break;
    case GL_TEXTURE_COMPARE_FUNC: s->compare_func = (GLenum)i; break;
    case 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY */: s->aniso = v; break;
    case GL_TEXTURE_BORDER_COLOR:
        for (int k = 0; k < 4; k++) s->border[k] = f ? f[k] : (float)iv[k];
        break;
    default: fgl_error(GL_INVALID_ENUM); break;
    }
}
void APIENTRY glSamplerParameteri(GLuint s, GLenum p, GLint v) { fgl_sampler_set(s, p, NULL, &v); }
void APIENTRY glSamplerParameterf(GLuint s, GLenum p, GLfloat v) { fgl_sampler_set(s, p, &v, NULL); }
void APIENTRY glSamplerParameteriv(GLuint s, GLenum p, const GLint* v) { fgl_sampler_set(s, p, NULL, v); }
void APIENTRY glSamplerParameterfv(GLuint s, GLenum p, const GLfloat* v) { fgl_sampler_set(s, p, v, NULL); }
void APIENTRY glSamplerParameterIiv(GLuint s, GLenum p, const GLint* v) { fgl_sampler_set(s, p, NULL, v); }
void APIENTRY glSamplerParameterIuiv(GLuint s, GLenum p, const GLuint* v) { fgl_sampler_set(s, p, NULL, (const GLint*)v); }

static int fgl_sampler_query(GLuint name, GLenum p, float* out)
{
    fgl_ctx* c = fgl_cur();
    fgl_sampler* s = c ? fgl_sampler_get(c, name) : NULL;
    if (!s) {
        fgl_error(GL_INVALID_OPERATION);
        return 0;
    }
    switch (p) {
    case GL_TEXTURE_MIN_FILTER: out[0] = (float)s->min_filter; return 1;
    case GL_TEXTURE_MAG_FILTER: out[0] = (float)s->mag_filter; return 1;
    case GL_TEXTURE_WRAP_S: out[0] = (float)s->wrap_s; return 1;
    case GL_TEXTURE_WRAP_T: out[0] = (float)s->wrap_t; return 1;
    case GL_TEXTURE_WRAP_R: out[0] = (float)s->wrap_r; return 1;
    case GL_TEXTURE_MIN_LOD: out[0] = s->min_lod; return 1;
    case GL_TEXTURE_MAX_LOD: out[0] = s->max_lod; return 1;
    case GL_TEXTURE_LOD_BIAS: out[0] = s->lod_bias; return 1;
    case GL_TEXTURE_COMPARE_MODE: out[0] = (float)s->compare_mode; return 1;
    case GL_TEXTURE_COMPARE_FUNC: out[0] = (float)s->compare_func; return 1;
    case 0x84FE: out[0] = s->aniso; return 1;
    case GL_TEXTURE_BORDER_COLOR: memcpy(out, s->border, 16); return 4;
    default: fgl_error(GL_INVALID_ENUM); return 0;
    }
}
void APIENTRY glGetSamplerParameterfv(GLuint s, GLenum p, GLfloat* v)
{
    float o[4];
    int   n = fgl_sampler_query(s, p, o);
    for (int i = 0; i < n; i++) v[i] = o[i];
}
void APIENTRY glGetSamplerParameteriv(GLuint s, GLenum p, GLint* v)
{
    float o[4];
    int   n = fgl_sampler_query(s, p, o);
    for (int i = 0; i < n; i++) v[i] = (GLint)o[i];
}
void APIENTRY glGetSamplerParameterIiv(GLuint s, GLenum p, GLint* v) { glGetSamplerParameteriv(s, p, v); }
void APIENTRY glGetSamplerParameterIuiv(GLuint s, GLenum p, GLuint* v) { glGetSamplerParameteriv(s, p, (GLint*)v); }

/* ---- buffer clears (draw buffer 0, depth, stencil) ---- */
static void fgl_clear_with(fgl_ctx* c, GLbitfield mask, const float* col, float depth, int stencil)
{
    float  cc[4], cd = c->clear_depth;
    int    cs = c->clear_stencil;
    memcpy(cc, c->clear_color, sizeof(cc));
    if (col) memcpy(c->clear_color, col, sizeof(cc));
    c->clear_depth = depth, c->clear_stencil = stencil;
    glClear(mask);
    memcpy(c->clear_color, cc, sizeof(cc));
    c->clear_depth = cd, c->clear_stencil = cs;
}

void APIENTRY glClearBufferfv(GLenum buffer, GLint draw, const GLfloat* v)
{
    FGL_CTX_OR_RETURN(c);
    if (buffer == GL_COLOR) {
        if (draw == 0) fgl_clear_with(c, GL_COLOR_BUFFER_BIT, v, c->clear_depth, c->clear_stencil);
        else if (draw > 0) fgl_unimplemented("glClearBuffer (draw buffers other than 0)");
    } else if (buffer == GL_DEPTH) {
        fgl_clear_with(c, GL_DEPTH_BUFFER_BIT, NULL, v[0], c->clear_stencil);
    } else {
        fgl_error(GL_INVALID_ENUM);
    }
}
void APIENTRY glClearBufferiv(GLenum buffer, GLint draw, const GLint* v)
{
    FGL_CTX_OR_RETURN(c);
    if (buffer == GL_STENCIL) {
        fgl_clear_with(c, GL_STENCIL_BUFFER_BIT, NULL, c->clear_depth, v[0]);
    } else if (buffer == GL_COLOR) {
        float f[4] = { (float)v[0] / 255.0f, (float)v[1] / 255.0f, (float)v[2] / 255.0f, (float)v[3] / 255.0f };
        glClearBufferfv(GL_COLOR, draw, f);
    } else {
        fgl_error(GL_INVALID_ENUM);
    }
}
void APIENTRY glClearBufferuiv(GLenum buffer, GLint draw, const GLuint* v)
{
    FGL_CTX_OR_RETURN(c);
    if (buffer != GL_COLOR) {
        fgl_error(GL_INVALID_ENUM);
        return;
    }
    float f[4] = { (float)v[0] / 255.0f, (float)v[1] / 255.0f, (float)v[2] / 255.0f, (float)v[3] / 255.0f };
    glClearBufferfv(GL_COLOR, draw, f);
}
void APIENTRY glClearBufferfi(GLenum buffer, GLint draw, GLfloat depth, GLint stencil)
{
    FGL_CTX_OR_RETURN(c);
    (void)draw;
    if (buffer != GL_DEPTH_STENCIL) {
        fgl_error(GL_INVALID_ENUM);
        return;
    }
    fgl_clear_with(c, GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, NULL, depth, stencil);
}

/* ---- debug output (KHR_debug / ARB_debug_output): fatgl's errors,
 * unimplemented calls and shader errors reach the application ---- */
void fgl_debug(fgl_ctx* c, GLenum source, GLenum type, GLenum severity, const char* msg)
{
    if (!c || !c->dbg_cb || !(c->enables & FGL_E_DEBUG)) return;
    GLDEBUGPROC cb = c->dbg_cb;
    c->dbg_cb      = NULL; /* no recursion if the callback calls GL */
    cb(source, type, 0, severity, (GLsizei)strlen(msg), msg, c->dbg_user);
    c->dbg_cb = cb;
}
void APIENTRY glDebugMessageCallback(GLDEBUGPROC cb, const void* user)
{
    FGL_CTX_OR_RETURN(c);
    c->dbg_cb = cb, c->dbg_user = user;
}
void APIENTRY glDebugMessageControl(GLenum source, GLenum type, GLenum severity, GLsizei n, const GLuint* ids, GLboolean on)
{
    (void)source, (void)type, (void)severity, (void)n, (void)ids, (void)on;
}
void APIENTRY glDebugMessageInsert(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei len, const GLchar* buf)
{
    FGL_CTX_OR_RETURN(c);
    (void)id;
    char m[1024];
    size_t n = len < 0 ? strlen(buf) : (size_t)len;
    n        = n < sizeof(m) - 1 ? n : sizeof(m) - 1;
    memcpy(m, buf, n), m[n] = 0;
    fgl_debug(c, source, type, severity, m);
}
GLuint APIENTRY glGetDebugMessageLog(GLuint count, GLsizei size, GLenum* sources, GLenum* types, GLuint* ids, GLenum* severities,
                                     GLsizei* lengths, GLchar* log)
{
    (void)count, (void)size, (void)sources, (void)types, (void)ids, (void)severities, (void)lengths, (void)log;
    return 0; /* messages go to the callback (and fatgl.log); none are queued */
}
void APIENTRY glPushDebugGroup(GLenum source, GLuint id, GLsizei len, const GLchar* msg) { (void)source, (void)id, (void)len, (void)msg; }
void APIENTRY glPopDebugGroup(void) {}
void APIENTRY glObjectLabel(GLenum id, GLuint name, GLsizei len, const GLchar* label) { (void)id, (void)name, (void)len, (void)label; }
void APIENTRY glGetObjectLabel(GLenum id, GLuint name, GLsizei size, GLsizei* len, GLchar* label)
{
    (void)id, (void)name;
    if (len) *len = 0;
    if (label && size > 0) label[0] = 0;
}
void APIENTRY glObjectPtrLabel(const void* p, GLsizei len, const GLchar* label) { (void)p, (void)len, (void)label; }
void APIENTRY glGetObjectPtrLabel(const void* p, GLsizei size, GLsizei* len, GLchar* label)
{
    (void)p;
    if (len) *len = 0;
    if (label && size > 0) label[0] = 0;
}

/* ---- fog (fixed function) ---- */
static void fgl_fog(GLenum p, const float* v)
{
    FGL_CTX_OR_RETURN(c);
    switch (p) {
    case GL_FOG_MODE: c->fog_mode = (GLenum)v[0]; break;
    case GL_FOG_DENSITY: c->fog_density = v[0]; break;
    case GL_FOG_START: c->fog_start = v[0]; break;
    case GL_FOG_END: c->fog_end = v[0]; break;
    case GL_FOG_COLOR: memcpy(c->fog_color, v, 16); break;
    case GL_FOG_INDEX: case GL_FOG_COORD_SRC: break; /* depth based fog only */
    default: fgl_error(GL_INVALID_ENUM); break;
    }
}
void APIENTRY glFogf(GLenum p, GLfloat v)
{
    float f[4] = { v, 0, 0, 0 }; /* GL_FOG_COLOR needs the vector form */
    if (p == GL_FOG_COLOR) fgl_error(GL_INVALID_ENUM);
    else fgl_fog(p, f);
}
void APIENTRY glFogi(GLenum p, GLint v) { glFogf(p, (float)v); }
void APIENTRY glFogfv(GLenum p, const GLfloat* v) { fgl_fog(p, v); }
void APIENTRY glFogiv(GLenum p, const GLint* v)
{
    float f[4] = { (float)v[0], 0, 0, 0 };
    if (p == GL_FOG_COLOR)
        for (int k = 0; k < 4; k++) f[k] = (float)v[k] / 2147483647.0f;
    fgl_fog(p, f);
}

/* ---- glGet values kept here ---- */
int fgl_get_misc(fgl_ctx* c, GLenum p, double* v)
{
    switch (p) {
    case GL_STENCIL_FUNC: v[0] = c->stencil[0].func; return 1;
    case GL_STENCIL_REF: v[0] = c->stencil[0].ref; return 1;
    case GL_STENCIL_VALUE_MASK: v[0] = c->stencil[0].mask; return 1;
    case GL_STENCIL_WRITEMASK: v[0] = c->stencil[0].wmask; return 1;
    case GL_STENCIL_FAIL: v[0] = c->stencil[0].sfail; return 1;
    case GL_STENCIL_PASS_DEPTH_FAIL: v[0] = c->stencil[0].dpfail; return 1;
    case GL_STENCIL_PASS_DEPTH_PASS: v[0] = c->stencil[0].dppass; return 1;
    case GL_STENCIL_BACK_FUNC: v[0] = c->stencil[1].func; return 1;
    case GL_STENCIL_BACK_REF: v[0] = c->stencil[1].ref; return 1;
    case GL_STENCIL_BACK_VALUE_MASK: v[0] = c->stencil[1].mask; return 1;
    case GL_STENCIL_BACK_WRITEMASK: v[0] = c->stencil[1].wmask; return 1;
    case GL_STENCIL_BACK_FAIL: v[0] = c->stencil[1].sfail; return 1;
    case GL_STENCIL_BACK_PASS_DEPTH_FAIL: v[0] = c->stencil[1].dpfail; return 1;
    case GL_STENCIL_BACK_PASS_DEPTH_PASS: v[0] = c->stencil[1].dppass; return 1;
    case GL_STENCIL_CLEAR_VALUE: v[0] = c->clear_stencil; return 1;
    case GL_FOG_MODE: v[0] = c->fog_mode; return 1;
    case GL_FOG_DENSITY: v[0] = c->fog_density; return 1;
    case GL_FOG_START: v[0] = c->fog_start; return 1;
    case GL_FOG_END: v[0] = c->fog_end; return 1;
    case GL_FOG_COLOR: for (int i = 0; i < 4; i++) v[i] = c->fog_color[i]; return 4;
    case GL_MAX_TEXTURE_UNITS: v[0] = 2; return 1; /* fixed function texture stages */
    case GL_MAX_TEXTURE_COORDS: v[0] = 8; return 1;
    case GL_PROGRAM_ERROR_POSITION_ARB: v[0] = c->arb_error_pos; return 1;
    case GL_VERTEX_PROGRAM_ARB: v[0] = (c->enables & FGL_E_VP) != 0; return 1;
    case GL_FRAGMENT_PROGRAM_ARB: v[0] = (c->enables & FGL_E_FP) != 0; return 1;
    case GL_MAX_DEBUG_MESSAGE_LENGTH: v[0] = 1024; return 1;
    case GL_MAX_DEBUG_LOGGED_MESSAGES: case GL_DEBUG_LOGGED_MESSAGES: v[0] = 0; return 1;
    case GL_MAX_DEBUG_GROUP_STACK_DEPTH: v[0] = 64; return 1;
    case GL_MAX_LABEL_LENGTH: v[0] = 256; return 1;
    case GL_DEBUG_CALLBACK_FUNCTION: case GL_DEBUG_CALLBACK_USER_PARAM: v[0] = 0; return 1;
    case GL_CLIENT_ACTIVE_TEXTURE: v[0] = GL_TEXTURE0 + c->client_unit; return 1;
    case GL_TEXTURE_ENV_MODE: v[0] = c->tex_envs[c->active_unit]; return 1;
    case GL_POINT_SIZE: v[0] = c->point_size; return 1;
    case GL_LINE_WIDTH: v[0] = c->line_width; return 1;
    case GL_POINT_SIZE_RANGE: case GL_ALIASED_LINE_WIDTH_RANGE: case GL_SMOOTH_LINE_WIDTH_RANGE: v[0] = 1, v[1] = 64; return 2;
    case GL_POINT_SIZE_GRANULARITY: case GL_SMOOTH_LINE_WIDTH_GRANULARITY: v[0] = 0.125; return 1;
    case GL_PROVOKING_VERTEX: v[0] = c->provoking_vertex; return 1;
    case GL_LOGIC_OP_MODE: v[0] = c->logic_op; return 1;
    case GL_PRIMITIVE_RESTART_INDEX: v[0] = c->restart_index; return 1;
    case GL_SAMPLER_BINDING: v[0] = c->unit_sampler[c->active_unit]; return 1;
    case GL_TIMESTAMP: v[0] = (double)fgl_now_ns(); return 1;
    case GL_MAX_SAMPLES: v[0] = 8; return 1; /* window MSAA up to 8x (Doom 3 BFG sizes its antialiasing menu by it); multisample renderbuffers are single sampled */
    case GL_MAX_COLOR_TEXTURE_SAMPLES: case GL_MAX_DEPTH_TEXTURE_SAMPLES: case GL_MAX_INTEGER_SAMPLES:
        v[0] = 1;
        return 1;
    case GL_SAMPLES: v[0] = !c->draw_fbo && c->samples > 1 ? c->samples : 0; return 1;
    case GL_SAMPLE_BUFFERS: v[0] = !c->draw_fbo && c->samples > 1; return 1;
    case GL_MAX_SERVER_WAIT_TIMEOUT: v[0] = 0; return 1;
    case GL_MAX_3D_TEXTURE_SIZE: v[0] = 2048; return 1;
    case GL_MIN_MAP_BUFFER_ALIGNMENT: v[0] = 64; return 1;
    case GL_MAX_ARRAY_TEXTURE_LAYERS: v[0] = 2048; return 1;
    case GL_MAX_CUBE_MAP_TEXTURE_SIZE: v[0] = 8192; return 1;
    case GL_MAX_TEXTURE_LOD_BIAS: v[0] = 16; return 1;
    case 0x84FF /* GL_MAX_TEXTURE_MAX_ANISOTROPY */: v[0] = 16; return 1;
    case GL_MAX_TEXTURE_BUFFER_SIZE: v[0] = 1 << 27; return 1;
    case GL_MAX_RECTANGLE_TEXTURE_SIZE: v[0] = 8192; return 1;
    case GL_MAX_CLIP_DISTANCES: v[0] = 8; return 1;
    case GL_MAX_GEOMETRY_OUTPUT_VERTICES: case GL_MAX_GEOMETRY_TOTAL_OUTPUT_COMPONENTS: v[0] = 0; return 1;
    case GL_MAX_PROGRAM_TEXEL_OFFSET: v[0] = 7; return 1;
    case GL_MIN_PROGRAM_TEXEL_OFFSET: v[0] = -8; return 1;
    case GL_MAX_DUAL_SOURCE_DRAW_BUFFERS: v[0] = 0; return 1;
    case GL_MAX_COMBINED_UNIFORM_BLOCKS: v[0] = 24; return 1;
    case GL_MAX_COMBINED_VERTEX_UNIFORM_COMPONENTS: case GL_MAX_COMBINED_FRAGMENT_UNIFORM_COMPONENTS: v[0] = 16384 + 12 * 16384; return 1;
    case GL_MAX_VERTEX_UNIFORM_VECTORS: case GL_MAX_FRAGMENT_UNIFORM_VECTORS: v[0] = 4096; return 1;
    case GL_MAX_VARYING_VECTORS: v[0] = FM3D_MAX_SHADER_VARYINGS / 4; return 1;
    case GL_NUM_COMPRESSED_TEXTURE_FORMATS: v[0] = fgl_ncompressed_formats; return 1;
    case GL_COMPRESSED_TEXTURE_FORMATS:
        for (int i = 0; i < fgl_ncompressed_formats; i++) v[i] = fgl_compressed_formats[i];
        return fgl_ncompressed_formats;
    case GL_NUM_SHADER_BINARY_FORMATS: case GL_NUM_PROGRAM_BINARY_FORMATS: v[0] = 0; return 1;
    case GL_SUBPIXEL_BITS: v[0] = 4; return 1;
    case GL_DRAW_BUFFER: case GL_DRAW_BUFFER0: case GL_READ_BUFFER: v[0] = c->draw_fbo ? GL_COLOR_ATTACHMENT0 : GL_BACK; return 1;
    case GL_DOUBLEBUFFER: v[0] = 1; return 1;
    case GL_STEREO: v[0] = 0; return 1;
    case GL_UNPACK_ROW_LENGTH: v[0] = c->unpack_row; return 1;
    case GL_UNPACK_SKIP_ROWS: v[0] = c->unpack_skip_rows; return 1;
    case GL_UNPACK_SKIP_PIXELS: v[0] = c->unpack_skip_pixels; return 1;
    case GL_UNPACK_IMAGE_HEIGHT: case GL_UNPACK_SKIP_IMAGES:
    case GL_PACK_ROW_LENGTH: case GL_PACK_SKIP_ROWS: case GL_PACK_SKIP_PIXELS: case GL_PACK_IMAGE_HEIGHT:
    case GL_UNPACK_SWAP_BYTES: v[0] = c->unpack_swap; return 1;
    case GL_PACK_SWAP_BYTES: v[0] = c->pack_swap; return 1;
    case GL_PACK_SKIP_IMAGES: case GL_UNPACK_LSB_FIRST: case GL_PACK_LSB_FIRST:
        v[0] = 0;
        return 1;
    case GL_COPY_READ_BUFFER_BINDING: v[0] = c->copy_read; return 1;
    case GL_COPY_WRITE_BUFFER_BINDING: v[0] = c->copy_write; return 1;
    case GL_PIXEL_PACK_BUFFER_BINDING: v[0] = c->pack_buffer; return 1;
    case GL_PIXEL_UNPACK_BUFFER_BINDING: v[0] = c->unpack_buffer; return 1;
    case GL_POLYGON_OFFSET_FACTOR: v[0] = c->poly_factor; return 1;
    case GL_POLYGON_OFFSET_UNITS: v[0] = c->poly_units; return 1;
    case GL_ALPHA_TEST_FUNC: v[0] = c->alpha_func; return 1;
    case GL_ALPHA_TEST_REF: v[0] = c->alpha_ref; return 1;
    case GL_COLOR_WRITEMASK: for (int i = 0; i < 4; i++) v[i] = c->color_mask[i]; return 4;
    case GL_DEPTH_RANGE: v[0] = 0, v[1] = 1; return 2;
    case GL_ALIASED_POINT_SIZE_RANGE: v[0] = 1, v[1] = 64; return 2;
    case GL_IMPLEMENTATION_COLOR_READ_FORMAT: v[0] = GL_RGBA; return 1;
    case GL_IMPLEMENTATION_COLOR_READ_TYPE: v[0] = GL_UNSIGNED_BYTE; return 1;
    default: return 0;
    }
}

/* indexed state: uniform buffer bindings, blend / color mask of buffer 0 */
static int fgl_get_indexed(fgl_ctx* c, GLenum p, GLuint i, double* v)
{
    switch (p) {
    case GL_UNIFORM_BUFFER_BINDING:
    case GL_UNIFORM_BUFFER_START:
    case GL_UNIFORM_BUFFER_SIZE:
        if (i >= FGL_UBO_BINDS) {
            fgl_error(GL_INVALID_VALUE);
            return 0;
        }
        v[0] = p == GL_UNIFORM_BUFFER_BINDING ? (double)c->ubo[i].buffer
                                               : (p == GL_UNIFORM_BUFFER_START ? (double)c->ubo[i].offset : (double)c->ubo[i].size);
        return 1;
    case GL_COLOR_WRITEMASK: for (int k = 0; k < 4; k++) v[k] = c->color_mask[k]; return 4;
    case GL_BLEND: v[0] = (c->enables & FGL_E_BLEND) != 0; return 1;
    case GL_SAMPLE_MASK_VALUE: v[0] = (double)0xFFFFFFFFu; return 1;
    default: fgl_error(GL_INVALID_ENUM); return 0;
    }
}

void APIENTRY glGetIntegeri_v(GLenum p, GLuint i, GLint* out)
{
    FGL_CTX_OR_RETURN(c);
    double v[4];
    int    n = fgl_get_indexed(c, p, i, v);
    for (int k = 0; k < n; k++) out[k] = (GLint)v[k];
}
void APIENTRY glGetInteger64i_v(GLenum p, GLuint i, GLint64* out)
{
    FGL_CTX_OR_RETURN(c);
    double v[4];
    int    n = fgl_get_indexed(c, p, i, v);
    for (int k = 0; k < n; k++) out[k] = (GLint64)v[k];
}
void APIENTRY glGetBooleani_v(GLenum p, GLuint i, GLboolean* out)
{
    FGL_CTX_OR_RETURN(c);
    double v[4];
    int    n = fgl_get_indexed(c, p, i, v);
    for (int k = 0; k < n; k++) out[k] = v[k] != 0 ? GL_TRUE : GL_FALSE;
}
void APIENTRY glGetInteger64v(GLenum p, GLint64* out)
{
    if (p == GL_TIMESTAMP) {
        *out = (GLint64)fgl_now_ns();
        return;
    }
    if (p == GL_MAX_SERVER_WAIT_TIMEOUT || p == GL_MAX_UNIFORM_BLOCK_SIZE) {
        *out = p == GL_MAX_UNIFORM_BLOCK_SIZE ? 65536 : 0;
        return;
    }
    GLint v[16] = { 0 };
    glGetIntegerv(p, v);
    out[0] = v[0];
    if (p == GL_VIEWPORT || p == GL_SCISSOR_BOX)
        for (int i = 1; i < 4; i++) out[i] = v[i];
}

/* ---- buffer 0 only per draw buffer state ---- */
void APIENTRY glColorMaski(GLuint i, GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
    if (i == 0) glColorMask(r, g, b, a);
}
void APIENTRY glEnablei(GLenum cap, GLuint i)
{
    if (i == 0) glEnable(cap);
}
void APIENTRY glDisablei(GLenum cap, GLuint i)
{
    if (i == 0) glDisable(cap);
}
GLboolean APIENTRY glIsEnabledi(GLenum cap, GLuint i) { return i == 0 ? glIsEnabled(cap) : GL_FALSE; }

/* ---- small state ---- */
void APIENTRY glPointSize(GLfloat s)
{
    FGL_CTX_OR_RETURN(c);
    if (s <= 0) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    c->point_size = s;
}
void APIENTRY glLineWidth(GLfloat w)
{
    FGL_CTX_OR_RETURN(c);
    if (w <= 0) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    c->line_width = w;
}
void APIENTRY glPointParameterf(GLenum p, GLfloat v) { (void)p, (void)v; }
void APIENTRY glPointParameterfv(GLenum p, const GLfloat* v) { (void)p, (void)v; }
void APIENTRY glPointParameteri(GLenum p, GLint v) { (void)p, (void)v; }
void APIENTRY glPointParameteriv(GLenum p, const GLint* v) { (void)p, (void)v; }
void APIENTRY glProvokingVertex(GLenum mode)
{
    FGL_CTX_OR_RETURN(c);
    c->provoking_vertex = mode;
}
void APIENTRY glPrimitiveRestartIndex(GLuint index)
{
    FGL_CTX_OR_RETURN(c);
    c->restart_index = index;
}
void APIENTRY glLogicOp(GLenum op)
{
    FGL_CTX_OR_RETURN(c);
    c->logic_op = op;
    if (op != GL_COPY) fgl_unimplemented("glLogicOp (operations other than GL_COPY)");
}
void APIENTRY glSampleCoverage(GLfloat v, GLboolean invert) { (void)v, (void)invert; }
void APIENTRY glSampleMaski(GLuint i, GLbitfield mask) { (void)i, (void)mask; }
void APIENTRY glClampColor(GLenum target, GLenum clamp) { (void)target, (void)clamp; } /* 8 bit targets clamp anyway */
void APIENTRY glGetMultisamplefv(GLenum p, GLuint i, GLfloat* v)
{
    (void)p, (void)i;
    v[0] = v[1] = 0.5f;
}
void APIENTRY glPixelStoref(GLenum p, GLfloat v) { glPixelStorei(p, (GLint)v); }

/* ---- more generic vertex attribute forms ---- */
void APIENTRY glVertexAttrib1d(GLuint i, GLdouble x) { glVertexAttrib4f(i, (float)x, 0, 0, 1); }
void APIENTRY glVertexAttrib2d(GLuint i, GLdouble x, GLdouble y) { glVertexAttrib4f(i, (float)x, (float)y, 0, 1); }
void APIENTRY glVertexAttrib3d(GLuint i, GLdouble x, GLdouble y, GLdouble z) { glVertexAttrib4f(i, (float)x, (float)y, (float)z, 1); }
void APIENTRY glVertexAttrib4d(GLuint i, GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
    glVertexAttrib4f(i, (float)x, (float)y, (float)z, (float)w);
}
void APIENTRY glVertexAttrib1dv(GLuint i, const GLdouble* v) { glVertexAttrib1d(i, v[0]); }
void APIENTRY glVertexAttrib2dv(GLuint i, const GLdouble* v) { glVertexAttrib2d(i, v[0], v[1]); }
void APIENTRY glVertexAttrib3dv(GLuint i, const GLdouble* v) { glVertexAttrib3d(i, v[0], v[1], v[2]); }
void APIENTRY glVertexAttrib4dv(GLuint i, const GLdouble* v) { glVertexAttrib4d(i, v[0], v[1], v[2], v[3]); }
void APIENTRY glVertexAttrib1s(GLuint i, GLshort x) { glVertexAttrib4f(i, x, 0, 0, 1); }
void APIENTRY glVertexAttrib2s(GLuint i, GLshort x, GLshort y) { glVertexAttrib4f(i, x, y, 0, 1); }
void APIENTRY glVertexAttrib3s(GLuint i, GLshort x, GLshort y, GLshort z) { glVertexAttrib4f(i, x, y, z, 1); }
void APIENTRY glVertexAttrib4s(GLuint i, GLshort x, GLshort y, GLshort z, GLshort w) { glVertexAttrib4f(i, x, y, z, w); }
void APIENTRY glVertexAttrib1sv(GLuint i, const GLshort* v) { glVertexAttrib1s(i, v[0]); }
void APIENTRY glVertexAttrib2sv(GLuint i, const GLshort* v) { glVertexAttrib2s(i, v[0], v[1]); }
void APIENTRY glVertexAttrib3sv(GLuint i, const GLshort* v) { glVertexAttrib3s(i, v[0], v[1], v[2]); }
void APIENTRY glVertexAttrib4sv(GLuint i, const GLshort* v) { glVertexAttrib4s(i, v[0], v[1], v[2], v[3]); }
void APIENTRY glVertexAttrib4bv(GLuint i, const GLbyte* v) { glVertexAttrib4f(i, v[0], v[1], v[2], v[3]); }
void APIENTRY glVertexAttrib4iv(GLuint i, const GLint* v) { glVertexAttrib4f(i, (float)v[0], (float)v[1], (float)v[2], (float)v[3]); }
void APIENTRY glVertexAttrib4ubv(GLuint i, const GLubyte* v) { glVertexAttrib4f(i, v[0], v[1], v[2], v[3]); }
void APIENTRY glVertexAttrib4uiv(GLuint i, const GLuint* v) { glVertexAttrib4f(i, (float)v[0], (float)v[1], (float)v[2], (float)v[3]); }
void APIENTRY glVertexAttrib4usv(GLuint i, const GLushort* v) { glVertexAttrib4f(i, v[0], v[1], v[2], v[3]); }
/* normalized: signed values map to [-1, 1] (GL 4.2 rule), unsigned to [0, 1] */
#define FGL_SN(x, m) ((float)(x) / (float)(m) < -1.0f ? -1.0f : (float)(x) / (float)(m))
void APIENTRY glVertexAttrib4Nbv(GLuint i, const GLbyte* v) { glVertexAttrib4f(i, FGL_SN(v[0], 127), FGL_SN(v[1], 127), FGL_SN(v[2], 127), FGL_SN(v[3], 127)); }
void APIENTRY glVertexAttrib4Nsv(GLuint i, const GLshort* v)
{
    glVertexAttrib4f(i, FGL_SN(v[0], 32767), FGL_SN(v[1], 32767), FGL_SN(v[2], 32767), FGL_SN(v[3], 32767));
}
void APIENTRY glVertexAttrib4Niv(GLuint i, const GLint* v)
{
    glVertexAttrib4f(i, FGL_SN(v[0], 2147483647.0), FGL_SN(v[1], 2147483647.0), FGL_SN(v[2], 2147483647.0), FGL_SN(v[3], 2147483647.0));
}
void APIENTRY glVertexAttrib4Nub(GLuint i, GLubyte x, GLubyte y, GLubyte z, GLubyte w)
{
    glVertexAttrib4f(i, x / 255.0f, y / 255.0f, z / 255.0f, w / 255.0f);
}
void APIENTRY glVertexAttrib4Nubv(GLuint i, const GLubyte* v) { glVertexAttrib4Nub(i, v[0], v[1], v[2], v[3]); }
void APIENTRY glVertexAttrib4Nusv(GLuint i, const GLushort* v)
{
    glVertexAttrib4f(i, v[0] / 65535.0f, v[1] / 65535.0f, v[2] / 65535.0f, v[3] / 65535.0f);
}
void APIENTRY glVertexAttrib4Nuiv(GLuint i, const GLuint* v)
{
    glVertexAttrib4f(i, (float)(v[0] / 4294967295.0), (float)(v[1] / 4294967295.0), (float)(v[2] / 4294967295.0), (float)(v[3] / 4294967295.0));
}

/* integer attributes: the raw 32 bit words go into the vertex stream */
static void fgl_attr_int(GLuint index, const uint32_t* w)
{
    FGL_CTX_OR_RETURN(c);
    if (index >= FGL_ATTRIBS) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    memcpy(c->attr_value[index], w, 16);
}
void APIENTRY glVertexAttribI4i(GLuint i, GLint x, GLint y, GLint z, GLint w)
{
    uint32_t v[4] = { (uint32_t)x, (uint32_t)y, (uint32_t)z, (uint32_t)w };
    fgl_attr_int(i, v);
}
void APIENTRY glVertexAttribI4ui(GLuint i, GLuint x, GLuint y, GLuint z, GLuint w)
{
    uint32_t v[4] = { x, y, z, w };
    fgl_attr_int(i, v);
}
void APIENTRY glVertexAttribI1i(GLuint i, GLint x) { glVertexAttribI4i(i, x, 0, 0, 1); }
void APIENTRY glVertexAttribI2i(GLuint i, GLint x, GLint y) { glVertexAttribI4i(i, x, y, 0, 1); }
void APIENTRY glVertexAttribI3i(GLuint i, GLint x, GLint y, GLint z) { glVertexAttribI4i(i, x, y, z, 1); }
void APIENTRY glVertexAttribI1ui(GLuint i, GLuint x) { glVertexAttribI4ui(i, x, 0, 0, 1); }
void APIENTRY glVertexAttribI2ui(GLuint i, GLuint x, GLuint y) { glVertexAttribI4ui(i, x, y, 0, 1); }
void APIENTRY glVertexAttribI3ui(GLuint i, GLuint x, GLuint y, GLuint z) { glVertexAttribI4ui(i, x, y, z, 1); }
void APIENTRY glVertexAttribI1iv(GLuint i, const GLint* v) { glVertexAttribI4i(i, v[0], 0, 0, 1); }
void APIENTRY glVertexAttribI2iv(GLuint i, const GLint* v) { glVertexAttribI4i(i, v[0], v[1], 0, 1); }
void APIENTRY glVertexAttribI3iv(GLuint i, const GLint* v) { glVertexAttribI4i(i, v[0], v[1], v[2], 1); }
void APIENTRY glVertexAttribI4iv(GLuint i, const GLint* v) { glVertexAttribI4i(i, v[0], v[1], v[2], v[3]); }
void APIENTRY glVertexAttribI1uiv(GLuint i, const GLuint* v) { glVertexAttribI4ui(i, v[0], 0, 0, 1); }
void APIENTRY glVertexAttribI2uiv(GLuint i, const GLuint* v) { glVertexAttribI4ui(i, v[0], v[1], 0, 1); }
void APIENTRY glVertexAttribI3uiv(GLuint i, const GLuint* v) { glVertexAttribI4ui(i, v[0], v[1], v[2], 1); }
void APIENTRY glVertexAttribI4uiv(GLuint i, const GLuint* v) { glVertexAttribI4ui(i, v[0], v[1], v[2], v[3]); }
void APIENTRY glVertexAttribI4bv(GLuint i, const GLbyte* v) { glVertexAttribI4i(i, v[0], v[1], v[2], v[3]); }
void APIENTRY glVertexAttribI4sv(GLuint i, const GLshort* v) { glVertexAttribI4i(i, v[0], v[1], v[2], v[3]); }
void APIENTRY glVertexAttribI4ubv(GLuint i, const GLubyte* v) { glVertexAttribI4ui(i, v[0], v[1], v[2], v[3]); }
void APIENTRY glVertexAttribI4usv(GLuint i, const GLushort* v) { glVertexAttribI4ui(i, v[0], v[1], v[2], v[3]); }

/* packed 2_10_10_10 values */
static void fgl_unpack_p(GLenum type, GLboolean norm, GLuint p, int n, float* o)
{
    int bits[4] = { 10, 10, 10, 2 };
    for (int k = 0; k < 4; k++) {
        int      sh = k * 10;
        uint32_t u  = (p >> sh) & ((1u << bits[k]) - 1u);
        float    v;
        if (type == GL_INT_2_10_10_10_REV) {
            int32_t s = (int32_t)(u << (32 - bits[k])) >> (32 - bits[k]);
            v         = norm ? (float)s / (float)((1 << (bits[k] - 1)) - 1) : (float)s;
            if (v < -1.0f && norm) v = -1.0f;
        } else {
            v = norm ? (float)u / (float)((1u << bits[k]) - 1u) : (float)u;
        }
        o[k] = k < n ? v : (k == 3 ? 1.0f : 0.0f);
    }
}
static void fgl_attr_p(GLuint i, GLenum type, GLboolean norm, GLuint p, int n)
{
    float o[4];
    if (type != GL_INT_2_10_10_10_REV && type != GL_UNSIGNED_INT_2_10_10_10_REV) {
        fgl_error(GL_INVALID_ENUM);
        return;
    }
    fgl_unpack_p(type, norm, p, n, o);
    glVertexAttrib4f(i, o[0], o[1], o[2], o[3]);
}
void APIENTRY glVertexAttribP1ui(GLuint i, GLenum t, GLboolean n, GLuint v) { fgl_attr_p(i, t, n, v, 1); }
void APIENTRY glVertexAttribP2ui(GLuint i, GLenum t, GLboolean n, GLuint v) { fgl_attr_p(i, t, n, v, 2); }
void APIENTRY glVertexAttribP3ui(GLuint i, GLenum t, GLboolean n, GLuint v) { fgl_attr_p(i, t, n, v, 3); }
void APIENTRY glVertexAttribP4ui(GLuint i, GLenum t, GLboolean n, GLuint v) { fgl_attr_p(i, t, n, v, 4); }
void APIENTRY glVertexAttribP1uiv(GLuint i, GLenum t, GLboolean n, const GLuint* v) { fgl_attr_p(i, t, n, v[0], 1); }
void APIENTRY glVertexAttribP2uiv(GLuint i, GLenum t, GLboolean n, const GLuint* v) { fgl_attr_p(i, t, n, v[0], 2); }
void APIENTRY glVertexAttribP3uiv(GLuint i, GLenum t, GLboolean n, const GLuint* v) { fgl_attr_p(i, t, n, v[0], 3); }
void APIENTRY glVertexAttribP4uiv(GLuint i, GLenum t, GLboolean n, const GLuint* v) { fgl_attr_p(i, t, n, v[0], 4); }

void APIENTRY glGetVertexAttribfv(GLuint index, GLenum p, GLfloat* v)
{
    FGL_CTX_OR_RETURN(c);
    if (p == GL_CURRENT_VERTEX_ATTRIB) {
        if (index < FGL_ATTRIBS) memcpy(v, c->attr_value[index], 16);
        return;
    }
    GLint i = 0;
    glGetVertexAttribiv(index, p, &i);
    v[0] = (GLfloat)i;
}
void APIENTRY glGetVertexAttribdv(GLuint index, GLenum p, GLdouble* v)
{
    GLfloat f[4] = { 0 };
    glGetVertexAttribfv(index, p, f);
    for (int k = 0; k < (p == GL_CURRENT_VERTEX_ATTRIB ? 4 : 1); k++) v[k] = f[k];
}
void APIENTRY glGetVertexAttribIiv(GLuint index, GLenum p, GLint* v)
{
    FGL_CTX_OR_RETURN(c);
    if (p == GL_CURRENT_VERTEX_ATTRIB) {
        if (index < FGL_ATTRIBS) memcpy(v, c->attr_value[index], 16);
        return;
    }
    glGetVertexAttribiv(index, p, v);
}
void APIENTRY glGetVertexAttribIuiv(GLuint index, GLenum p, GLuint* v) { glGetVertexAttribIiv(index, p, (GLint*)v); }
