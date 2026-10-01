/*
 * fatgl - draws with a GLSL program (GL 2.0+ path). The bound vertex array's
 * attributes (any buffer, stride, type) are gathered into the stream the
 * fatmap program reads (shader input location L at L * 16 bytes, 4 floats
 * or raw 32 bit integers), primitives become a triangle index list.
 */
#include "fgl.h"
#include <math.h>

static float fgl_half(uint16_t h)
{
    uint32_t s = (uint32_t)(h >> 15) << 31, e = (h >> 10) & 31, m = h & 1023, u;
    if (e == 0) {
        float f = (float)m * (1.0f / 16777216.0f); /* denormal: m * 2^-24 */
        return s ? -f : f;
    }
    u = s | ((e == 31 ? 255 : e + 112) << 23) | (m << 13);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static int fgl_tsize(GLenum t)
{
    switch (t) {
    case GL_BYTE: case GL_UNSIGNED_BYTE: return 1;
    case GL_SHORT: case GL_UNSIGNED_SHORT: case GL_HALF_FLOAT: return 2;
    case GL_DOUBLE: return 8;
    default: return 4;
    }
}

/* one attribute element -> 4 words (floats, or integers for I attributes) */
static void fgl_fetch_attr(const fgl_attrib* a, const uint8_t* p, uint32_t* out)
{
    float f[4] = { 0, 0, 0, 1 };
    int32_t iv[4] = { 0, 0, 0, 1 };
    for (int j = 0; j < a->size && j < 4; j++) {
        const uint8_t* q = p + (size_t)j * (size_t)fgl_tsize(a->type);
        switch (a->type) {
        case GL_FLOAT: memcpy(&f[j], q, 4); break;
        case GL_DOUBLE: { double d; memcpy(&d, q, 8); f[j] = (float)d; break; }
        case GL_HALF_FLOAT: { uint16_t h; memcpy(&h, q, 2); f[j] = fgl_half(h); break; }
        case GL_BYTE: iv[j] = *(const int8_t*)q, f[j] = a->normalized ? fmaxf((float)iv[j] / 127.0f, -1.0f) : (float)iv[j]; break;
        case GL_UNSIGNED_BYTE: iv[j] = *q, f[j] = a->normalized ? (float)iv[j] / 255.0f : (float)iv[j]; break;
        case GL_SHORT: { int16_t v; memcpy(&v, q, 2); iv[j] = v; f[j] = a->normalized ? fmaxf((float)v / 32767.0f, -1.0f) : (float)v; break; }
        case GL_UNSIGNED_SHORT: { uint16_t v; memcpy(&v, q, 2); iv[j] = v; f[j] = a->normalized ? (float)v / 65535.0f : (float)v; break; }
        case GL_INT: { int32_t v; memcpy(&v, q, 4); iv[j] = v; f[j] = a->normalized ? fmaxf((float)v / 2147483647.0f, -1.0f) : (float)v; break; }
        default: { uint32_t v; memcpy(&v, q, 4); iv[j] = (int32_t)v; f[j] = a->normalized ? (float)v / 4294967295.0f : (float)v; break; }
        }
    }
    if (a->integer) memcpy(out, iv, 16);
    else memcpy(out, f, 16);
}

static const uint8_t* fgl_attr_base(fgl_ctx* c, const fgl_attrib* a)
{
    if (!a->buffer) return (const uint8_t*)a->offset; /* client memory (compatibility) */
    fgl_buf* b = fgl_buffer(c, a->buffer);
    return b && b->data ? b->data + a->offset : NULL;
}

/* the array feeding a program input: a generic attribute, or for the
 * built-in inputs of legacy GLSL (fgl_Vertex, ...) the client array; NULL:
 * a constant (val) */
static const fgl_attrib* fgl_input_source(fgl_ctx* c, const fgl_program* p, const fgl_vao* vao, const char* name, fgl_attrib* tmp,
                                          const float** val)
{
    static const float zero1[4] = { 0, 0, 0, 1 };
    if (!strncmp(name, "fgl_", 4)) {
        const char* n = name + 4;
        int         k = -1;
        *val          = zero1;
        if (!strcmp(n, "Vertex")) k = 0;
        else if (!strcmp(n, "Normal")) k = 1, *val = c->cur_normal;
        else if (!strcmp(n, "Color")) k = 2, *val = c->cur_color;
        else if (!strcmp(n, "MultiTexCoord0")) k = 3, *val = c->cur_tex;
        if (k == 0 && !c->va[0].on && vao->a[0].enabled) return &vao->a[0]; /* generic attribute 0 aliases gl_Vertex */
        if (k < 0 || !c->va[k].on) return NULL;
        memset(tmp, 0, sizeof(*tmp));
        tmp->enabled = 1, tmp->size = c->va[k].size, tmp->type = c->va[k].type, tmp->stride = c->va[k].stride;
        tmp->offset = (GLintptr)c->va[k].ptr, tmp->buffer = c->va[k].buffer;
        tmp->normalized = k == 2 && tmp->type != GL_FLOAT && tmp->type != GL_DOUBLE; /* colors */
        return tmp;
    }
    int g = glGetAttribLocation(p->name, name);
    *val  = g >= 0 && g < FGL_ATTRIBS ? c->attr_value[g] : c->attr_value[0];
    return g >= 0 && g < FGL_ATTRIBS && vao->a[g].enabled ? &vao->a[g] : NULL;
}

/* a depth texture as fatmap sees it: (d, d, d, 1), 8 bits */
static fm_surface* fgl_depth_image(const fm_surface* d)
{
    fm_surface* s = fm_surface_create(d->width, d->height, FM_FORMAT_ARGB32);
    if (!s) return NULL;
    for (int y = 0; y < d->height; y++) {
        uint32_t* o = fm_surface_row32(s, y);
        for (int x = 0; x < d->width; x++) {
            uint32_t v = (uint32_t)(fgl_depth_at(d, x, y) * 255.0f + 0.5f);
            o[x]       = 0xFF000000u | v << 16 | v << 8 | v;
        }
    }
    return s;
}

int fgl_texture_use(fgl_ctx* c, fgl_tex* t, int unit, fm3d_texture** tex, fm3d_sampler* s)
{
    if (!t || (!t->level0 && !t->depth)) return 0;
    /* a sampler object on the unit overrides the texture's parameters */
    const fgl_sampler* so = unit >= 0 && unit < FGL_UNITS && c->unit_sampler[unit] ? fgl_sampler_get(c, c->unit_sampler[unit]) : NULL;
    GLenum min_filter = so ? so->min_filter : t->min_filter, mag_filter = so ? so->mag_filter : t->mag_filter;
    GLenum wrap_s = so ? so->wrap_s : t->wrap_s, wrap_t = so ? so->wrap_t : t->wrap_t;
    int    mips = min_filter != GL_NEAREST && min_filter != GL_LINEAR;
    if (t->rendered) { /* drawn through a framebuffer: finish those draws, then copy the image again */
        fgl_flush(c);
        fm3d_texture_release(t->tex);
        t->tex = NULL, t->rendered = 0;
    }
    if (!t->tex || t->built_mips != mips) {
        fm3d_texture_release(t->tex);
        if (t->level0) {
            t->tex = fm3d_texture_create(t->level0, mips);
            c->cnt.tex_builds++;
        } else {
            fm_surface* img = fgl_depth_image(t->depth);
            t->tex          = img ? fm3d_texture_create(img, mips) : NULL;
            fm_surface_destroy(img);
        }
        t->built_mips = mips;
        if (!t->tex) return 0;
    }
    memset(s, 0, sizeof(*s));
    switch (min_filter) {
    case GL_NEAREST: s->filter = FM3D_FILTER_NEAREST; break;
    case GL_LINEAR: s->filter = FM3D_FILTER_BILINEAR; break;
    case GL_NEAREST_MIPMAP_NEAREST: case GL_NEAREST_MIPMAP_LINEAR: s->filter = FM3D_FILTER_NEAREST_MIPMAP; break;
    case GL_LINEAR_MIPMAP_NEAREST: s->filter = FM3D_FILTER_BILINEAR_MIPMAP; break;
    default: s->filter = FM3D_FILTER_TRILINEAR; break;
    }
    if (mag_filter == GL_NEAREST && min_filter == GL_NEAREST) s->filter = FM3D_FILTER_NEAREST;
    s->wrap_u = wrap_s == GL_REPEAT ? FM_WRAP_REPEAT : (wrap_s == GL_MIRRORED_REPEAT ? FM_WRAP_MIRROR : FM_WRAP_CLAMP);
    s->wrap_v = wrap_t == GL_REPEAT ? FM_WRAP_REPEAT : (wrap_t == GL_MIRRORED_REPEAT ? FM_WRAP_MIRROR : FM_WRAP_CLAMP);
    *tex      = t->tex;
    return 1;
}

/* the program's uniforms, blocks and textures as fatmap state */
static void fgl_sync_program(fgl_ctx* c, fgl_program* p)
{
    fm3d_ctx* f = c->c3;
    fm3d_set_lighting(f, 0);
    fm3d_program prog = p->prog;
    if (!(c->enables & FGL_E_PSIZE)) prog.point_size_var = 0; /* gl_PointSize counts with GL_PROGRAM_POINT_SIZE only */
    fm3d_set_program(f, &prog);
    /* one default block for both stages (the same layout in both: glslang merges it) */
    if (p->defsize[0]) fm3d_set_uniform_block(f, FGL_DEF_VS, p->def[0], (size_t)p->defsize[0]);
    else fm3d_set_uniform_block(f, FGL_DEF_FS, p->def[1], (size_t)p->defsize[1]);
    for (int i = 0; i < p->nblocks; i++) {
        const fgl_ublock* b  = &p->blocks[i];
        if (!strcmp(b->name, "fgl_Builtins")) { /* legacy GLSL: the fixed function state */
            if (!c->builtins) c->builtins = (uint8_t*)malloc((size_t)fgl_builtins_size());
            if (!c->builtins) continue;
            fgl_builtins_fill(c, c->builtins);
            for (int k = 0; k < 2; k++)
                if (b->spv_binding[k] >= 0) fm3d_set_uniform_block(f, b->spv_binding[k], c->builtins, (size_t)fgl_builtins_size());
            continue;
        }
        fgl_buf*          bf = fgl_buffer(c, c->ubo[b->gl_binding].buffer);
        const void*       d  = NULL;
        size_t            n  = 0;
        if (bf && bf->data && c->ubo[b->gl_binding].offset < bf->size) {
            d = bf->data + c->ubo[b->gl_binding].offset;
            n = c->ubo[b->gl_binding].size ? (size_t)c->ubo[b->gl_binding].size : (size_t)(bf->size - c->ubo[b->gl_binding].offset);
            if (n > (size_t)(bf->size - c->ubo[b->gl_binding].offset)) n = (size_t)(bf->size - c->ubo[b->gl_binding].offset);
        }
        for (int k = 0; k < 2; k++)
            if (b->spv_binding[k] >= 0) fm3d_set_uniform_block(f, b->spv_binding[k], d, n);
    }
    for (int i = 0; i < p->nu; i++) {
        const fgl_uniform* u = &p->u[i];
        if (u->type != GL_SAMPLER_2D || u->sampler_binding < 0 || u->sampler_binding >= FM3D_MAX_TEXTURE_UNITS) continue;
        fm3d_texture* t = NULL;
        fm3d_sampler  s;
        GLuint        name = u->unit >= 0 && u->unit < FGL_UNITS ? c->unit_tex[u->unit] : 0;
        if (fgl_texture_use(c, name ? fgl_texture(c, name, 0) : NULL, u->unit, &t, &s)) fm3d_set_texture_unit(f, u->sampler_binding, t, &s);
        else fm3d_set_texture_unit(f, u->sampler_binding, NULL, NULL);
    }
}

/* primitives of `mode` from vertex numbers (-1: primitive restart) as
 * fatmap index lists relative to vmin: 3 (triangles), 2 (lines) or 1
 * (points) per primitive; returns the index count */
static int fgl_assemble(GLenum mode, const int* vid, int count, int vmin, uint32_t* out)
{
    int k = 0;
    for (int s = 0; s < count;) {
        int e = s;
        while (e < count && vid[e] >= 0) e++;
        int n = e - s;
#define FGL_V(i) (uint32_t)(vid[s + (i)] - vmin)
        switch (mode) {
        case GL_POINTS:
            for (int i = 0; i < n; i++) out[k++] = FGL_V(i);
            break;
        case GL_LINES:
            for (int i = 0; i + 1 < n; i += 2) out[k++] = FGL_V(i), out[k++] = FGL_V(i + 1);
            break;
        case GL_LINE_STRIP: case GL_LINE_LOOP:
            for (int i = 0; i + 1 < n; i++) out[k++] = FGL_V(i), out[k++] = FGL_V(i + 1);
            if (mode == GL_LINE_LOOP && n >= 2) out[k++] = FGL_V(n - 1), out[k++] = FGL_V(0);
            break;
        case GL_TRIANGLES:
            for (int i = 0; i + 2 < n; i += 3) out[k++] = FGL_V(i), out[k++] = FGL_V(i + 1), out[k++] = FGL_V(i + 2);
            break;
        case GL_TRIANGLE_STRIP:
            for (int t = 0; t + 2 < n; t++) { /* keep the winding of every second triangle */
                if (t & 1) out[k++] = FGL_V(t + 1), out[k++] = FGL_V(t), out[k++] = FGL_V(t + 2);
                else out[k++] = FGL_V(t), out[k++] = FGL_V(t + 1), out[k++] = FGL_V(t + 2);
            }
            break;
        case GL_QUADS:
            for (int q = 0; q + 3 < n; q += 4) {
                out[k++] = FGL_V(q), out[k++] = FGL_V(q + 1), out[k++] = FGL_V(q + 2);
                out[k++] = FGL_V(q), out[k++] = FGL_V(q + 2), out[k++] = FGL_V(q + 3);
            }
            break;
        case GL_QUAD_STRIP: /* quad j = v[2j], v[2j+1], v[2j+3], v[2j+2] */
            for (int q = 0; q + 3 < n; q += 2) {
                out[k++] = FGL_V(q), out[k++] = FGL_V(q + 1), out[k++] = FGL_V(q + 2);
                out[k++] = FGL_V(q + 1), out[k++] = FGL_V(q + 3), out[k++] = FGL_V(q + 2);
            }
            break;
        default: /* GL_TRIANGLE_FAN, GL_POLYGON */
            for (int t = 0; t + 2 < n; t++) out[k++] = FGL_V(0), out[k++] = FGL_V(t + 1), out[k++] = FGL_V(t + 2);
            break;
        }
#undef FGL_V
        s = e + 1;
    }
    return k;
}

void fgl_draw_program(fgl_ctx* c, GLenum mode, GLint first, GLsizei count, GLenum itype, const void* indices, GLint basevertex,
                      GLsizei instances)
{
    fgl_program* p = fgl_program_get(c, c->program);
    if (!p || !p->linked || count <= 0 || instances <= 0) return;
    if (mode > GL_POLYGON) {
        if (mode >= GL_LINES_ADJACENCY && mode <= GL_TRIANGLE_STRIP_ADJACENCY) fgl_unimplemented("adjacency primitives (geometry shaders)");
        else fgl_error(GL_INVALID_ENUM);
        return;
    }
    fgl_vao* vao = fgl_cur_vao(c);
    /* the vertex numbers in draw order */
    int* vid = (int*)malloc((size_t)count * sizeof(int));
    if (!vid) {
        fgl_error(GL_OUT_OF_MEMORY);
        return;
    }
    int vmin = 0x7fffffff, vmax = -1;
    if (indices || itype) {
        const uint8_t* ip = (const uint8_t*)indices;
        if (vao->elements) {
            fgl_buf* eb = fgl_buffer(c, vao->elements);
            ip          = eb && eb->data ? eb->data + (uintptr_t)indices : NULL;
        }
        if (!ip) {
            free(vid);
            fgl_error(GL_INVALID_OPERATION);
            return;
        }
        /* GL_PRIMITIVE_RESTART: the index cuts strips / fans / loops (compared before the base vertex) */
        uint32_t restart = (c->enables & FGL_E_RESTART) ? c->restart_index : 0xFFFFFFFFu;
        int      cut     = (c->enables & FGL_E_RESTART) != 0;
        for (GLsizei i = 0; i < count; i++) {
            uint32_t e = itype == GL_UNSIGNED_BYTE ? ip[i] : (itype == GL_UNSIGNED_SHORT ? ((const uint16_t*)ip)[i] : ((const uint32_t*)ip)[i]);
            vid[i]     = cut && e == restart ? -1 : (int)e + basevertex;
        }
    } else {
        for (GLsizei i = 0; i < count; i++) vid[i] = first + i;
    }
    for (GLsizei i = 0; i < count; i++)
        if (vid[i] >= 0) vmin = vid[i] < vmin ? vid[i] : vmin, vmax = vid[i] > vmax ? vid[i] : vmax;
    if (vmax < 0) { /* only restart indices */
        free(vid);
        return;
    }
    int nv = vmax - vmin + 1;
    if (vmin < 0 || nv > (1 << 24)) {
        free(vid);
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    /* primitives (indices relative to vmin) */
    uint32_t* tri  = (uint32_t*)malloc(((size_t)count * 3 + 4) * sizeof(uint32_t));
    int       stride = 16 * (p->max_loc + 1 > 0 ? p->max_loc + 1 : 1);
    uint8_t*  stream = (uint8_t*)malloc((size_t)nv * (size_t)stride);
    if (!tri || !stream) {
        free(vid), free(tri), free(stream);
        fgl_error(GL_OUT_OF_MEMORY);
        return;
    }
    int nidx = fgl_assemble(mode, vid, count, vmin, tri);
    fm3d_primitive prim = mode == GL_POINTS ? FM3D_PRIM_POINTS : (mode <= GL_LINE_STRIP ? FM3D_PRIM_LINES : FM3D_PRIM_TRIANGLES);
    int per_instance = 0;
    for (int i = 0; i < p->nin; i++) {
        fgl_attrib        tmp;
        const float*      val;
        const fgl_attrib* a = fgl_input_source(c, p, vao, p->in[i].name, &tmp, &val);
        if (a && a->divisor) per_instance = 1;
    }
    fgl_sync(c);
    fgl_sync_program(c, p);
    fm3d_set_primitive(c->c3, prim);
    fm3d_set_line_width(c->c3, c->line_width);
    fm3d_set_point_size(c->c3, c->point_size);
    for (GLsizei inst = 0; inst < instances && nidx > 0; inst++) {
        if (inst == 0 || per_instance) { /* the stream (again when attributes advance per instance) */
            for (int i = 0; i < p->nin; i++) {
                int loc = p->in[i].location;
                if (loc < 0 || loc >= FGL_ATTRIBS) continue;
                fgl_attrib        tmp;
                const float*      val;
                const fgl_attrib* a    = fgl_input_source(c, p, vao, p->in[i].name, &tmp, &val);
                const uint8_t*    base = a ? fgl_attr_base(c, a) : NULL;
                for (int v = 0; v < nv; v++) {
                    uint32_t* o = (uint32_t*)(stream + (size_t)v * (size_t)stride + 16 * loc);
                    if (!base) {
                        memcpy(o, val, 16);
                        continue;
                    }
                    int    e  = a->divisor ? (int)(inst / (GLsizei)a->divisor) : vmin + v;
                    size_t st = a->stride ? (size_t)a->stride : (size_t)(a->size * fgl_tsize(a->type));
                    fgl_fetch_attr(a, base + (size_t)e * st, o);
                }
            }
        }
        fm3d_set_draw_ids(c->c3, vmin, (int)inst);
        fm3d_draw_vertices(c->c3, stream, stride, nv, tri, nidx);
        c->cnt.draws_prog++;
    }
    fm3d_set_draw_ids(c->c3, 0, 0);
    fm3d_set_primitive(c->c3, FM3D_PRIM_TRIANGLES);
    free(vid), free(tri), free(stream);
}

/* glBegin / glEnd with a program: the immediate vertices as client arrays */
void fgl_draw_program_imm(fgl_ctx* c, GLenum mode, const fgl_vtx* v, int n)
{
    if (n <= 0) return;
    fgl_clarray saved[4];
    memcpy(saved, c->va, sizeof(saved));
    const float* src[4] = { v->pos, v->nrm, v->col, v->tex };
    int          size[4] = { 4, 3, 4, 2 };
    for (int k = 0; k < 4; k++) {
        c->va[k].on = 1, c->va[k].size = size[k], c->va[k].type = GL_FLOAT;
        c->va[k].stride = (GLsizei)sizeof(fgl_vtx), c->va[k].ptr = src[k], c->va[k].buffer = 0;
    }
    fgl_vao* vao      = fgl_cur_vao(c);
    GLuint   elements = vao->elements;
    vao->elements     = 0;
    fgl_draw_program(c, mode, 0, n, 0, NULL, 0, 1);
    vao->elements = elements;
    memcpy(c->va, saved, sizeof(saved));
}

/* ---- entry points with a program ---- */
void APIENTRY glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei n)
{
    FGL_CTX_OR_RETURN(c);
    if (c->program) fgl_draw_program(c, mode, first, count, 0, NULL, 0, n);
}
void APIENTRY glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void* idx, GLsizei n)
{
    FGL_CTX_OR_RETURN(c);
    if (c->program) fgl_draw_program(c, mode, 0, count, type, idx, 0, n);
}
void APIENTRY glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void* idx, GLint base)
{
    FGL_CTX_OR_RETURN(c);
    if (c->program) fgl_draw_program(c, mode, 0, count, type, idx, base, 1);
}
void APIENTRY glDrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type, const void* idx, GLsizei n, GLint base)
{
    FGL_CTX_OR_RETURN(c);
    if (c->program) fgl_draw_program(c, mode, 0, count, type, idx, base, n);
}
void APIENTRY glDrawRangeElements(GLenum mode, GLuint lo, GLuint hi, GLsizei count, GLenum type, const void* idx)
{
    (void)lo, (void)hi;
    glDrawElements(mode, count, type, idx);
}
void APIENTRY glDrawRangeElementsBaseVertex(GLenum mode, GLuint lo, GLuint hi, GLsizei count, GLenum type, const void* idx, GLint base)
{
    (void)lo, (void)hi;
    glDrawElementsBaseVertex(mode, count, type, idx, base);
}
void APIENTRY glMultiDrawArrays(GLenum mode, const GLint* first, const GLsizei* count, GLsizei n)
{
    for (GLsizei i = 0; i < n; i++) glDrawArrays(mode, first[i], count[i]);
}
void APIENTRY glMultiDrawElements(GLenum mode, const GLsizei* count, GLenum type, const void* const* idx, GLsizei n)
{
    for (GLsizei i = 0; i < n; i++) glDrawElements(mode, count[i], type, idx[i]);
}

/* ---- texture units ---- */
void APIENTRY glActiveTexture(GLenum unit)
{
    FGL_CTX_OR_RETURN(c);
    int u = (int)unit - GL_TEXTURE0;
    if (u < 0 || u >= FGL_UNITS) {
        fgl_error(GL_INVALID_ENUM);
        return;
    }
    c->active_unit = u;
}
