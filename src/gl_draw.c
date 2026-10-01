/*
 * fatgl - immediate mode, client arrays and the draw path: GL primitives
 * become fatmap triangles with the fixed function state (lights, material,
 * texture) translated.
 */
#include "fgl.h"
#include <math.h>

/* ---- current attributes ---- */
static void fgl_vertex(fgl_ctx* c, float x, float y, float z, float w)
{
    if (!c->in_begin) return; /* GL: undefined outside Begin / End */
    if (c->nimm == c->immcap) {
        int      n  = c->immcap ? c->immcap * 2 : 1024;
        fgl_vtx* nv = (fgl_vtx*)realloc(c->imm, (size_t)n * sizeof(fgl_vtx));
        if (!nv) {
            fgl_error(GL_OUT_OF_MEMORY);
            return;
        }
        c->imm = nv, c->immcap = n;
    }
    fgl_vtx* v = &c->imm[c->nimm++];
    v->pos[0] = x, v->pos[1] = y, v->pos[2] = z, v->pos[3] = w;
    memcpy(v->nrm, c->cur_normal, sizeof(v->nrm));
    v->tex[0] = c->cur_tex[0], v->tex[1] = c->cur_tex[1];
    memcpy(v->col, c->cur_color, sizeof(v->col));
}

#define FGL_REC(op, n, ...)                                                                                             \
    do {                                                                                                                \
        float a_[4] = { __VA_ARGS__ };                                                                                  \
        if (c->list_compiling && fgl_record(c, op, 0, 0, a_, n)) return;                                                \
    } while (0)

void APIENTRY glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    FGL_CTX_OR_RETURN(c);
    FGL_REC(FGL_OP_VERTEX, 4, x, y, z, w);
    fgl_vertex(c, x, y, z, w);
}
void APIENTRY glVertex3f(GLfloat x, GLfloat y, GLfloat z) { glVertex4f(x, y, z, 1.0f); }
void APIENTRY glVertex2f(GLfloat x, GLfloat y) { glVertex4f(x, y, 0.0f, 1.0f); }
void APIENTRY glVertex2i(GLint x, GLint y) { glVertex4f((float)x, (float)y, 0.0f, 1.0f); }
void APIENTRY glVertex3i(GLint x, GLint y, GLint z) { glVertex4f((float)x, (float)y, (float)z, 1.0f); }
void APIENTRY glVertex2d(GLdouble x, GLdouble y) { glVertex4f((float)x, (float)y, 0.0f, 1.0f); }
void APIENTRY glVertex3d(GLdouble x, GLdouble y, GLdouble z) { glVertex4f((float)x, (float)y, (float)z, 1.0f); }
void APIENTRY glVertex2fv(const GLfloat* v) { glVertex4f(v[0], v[1], 0.0f, 1.0f); }
void APIENTRY glVertex3fv(const GLfloat* v) { glVertex4f(v[0], v[1], v[2], 1.0f); }
void APIENTRY glVertex4fv(const GLfloat* v) { glVertex4f(v[0], v[1], v[2], v[3]); }
void APIENTRY glVertex3dv(const GLdouble* v) { glVertex4f((float)v[0], (float)v[1], (float)v[2], 1.0f); }

void APIENTRY glNormal3f(GLfloat x, GLfloat y, GLfloat z)
{
    FGL_CTX_OR_RETURN(c);
    FGL_REC(FGL_OP_NORMAL, 3, x, y, z);
    c->cur_normal[0] = x, c->cur_normal[1] = y, c->cur_normal[2] = z;
}
void APIENTRY glNormal3fv(const GLfloat* v) { glNormal3f(v[0], v[1], v[2]); }
void APIENTRY glNormal3d(GLdouble x, GLdouble y, GLdouble z) { glNormal3f((float)x, (float)y, (float)z); }

void APIENTRY glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    FGL_CTX_OR_RETURN(c);
    FGL_REC(FGL_OP_COLOR, 4, r, g, b, a);
    c->cur_color[0] = r, c->cur_color[1] = g, c->cur_color[2] = b, c->cur_color[3] = a;
}
void APIENTRY glColor3f(GLfloat r, GLfloat g, GLfloat b) { glColor4f(r, g, b, 1.0f); }
void APIENTRY glColor3fv(const GLfloat* v) { glColor4f(v[0], v[1], v[2], 1.0f); }
void APIENTRY glColor4fv(const GLfloat* v) { glColor4f(v[0], v[1], v[2], v[3]); }
void APIENTRY glColor3d(GLdouble r, GLdouble g, GLdouble b) { glColor4f((float)r, (float)g, (float)b, 1.0f); }
void APIENTRY glColor3ub(GLubyte r, GLubyte g, GLubyte b) { glColor4f(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f); }
void APIENTRY glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) { glColor4f(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f); }
void APIENTRY glColor3ubv(const GLubyte* v) { glColor3ub(v[0], v[1], v[2]); }
void APIENTRY glColor4ubv(const GLubyte* v) { glColor4ub(v[0], v[1], v[2], v[3]); }

void APIENTRY glTexCoord2f(GLfloat s, GLfloat t)
{
    FGL_CTX_OR_RETURN(c);
    FGL_REC(FGL_OP_TEXCOORD, 2, s, t);
    c->cur_tex[0] = s, c->cur_tex[1] = t;
}
void APIENTRY glTexCoord2fv(const GLfloat* v) { glTexCoord2f(v[0], v[1]); }
void APIENTRY glTexCoord2d(GLdouble s, GLdouble t) { glTexCoord2f((float)s, (float)t); }
void APIENTRY glTexCoord2i(GLint s, GLint t) { glTexCoord2f((float)s, (float)t); }

void APIENTRY glBegin(GLenum mode)
{
    FGL_CTX_OR_RETURN(c);
    if (c->list_compiling && fgl_record(c, FGL_OP_BEGIN, mode, 0, NULL, 0)) return;
    if (c->in_begin || mode > GL_POLYGON) {
        fgl_error(c->in_begin ? GL_INVALID_OPERATION : GL_INVALID_ENUM);
        return;
    }
    c->in_begin = 1, c->prim = mode, c->nimm = 0;
}

void APIENTRY glEnd(void)
{
    FGL_CTX_OR_RETURN(c);
    if (c->list_compiling && fgl_record(c, FGL_OP_END, 0, 0, NULL, 0)) return;
    if (!c->in_begin) {
        fgl_error(GL_INVALID_OPERATION);
        return;
    }
    c->in_begin = 0;
    if (c->program) fgl_draw_program_imm(c, c->prim, c->imm, c->nimm);
    else fgl_draw_prim(c, c->prim, c->imm, c->nimm);
}

void APIENTRY glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2)
{
    glBegin(GL_POLYGON);
    glVertex2f(x1, y1), glVertex2f(x2, y1), glVertex2f(x2, y2), glVertex2f(x1, y2);
    glEnd();
}
void APIENTRY glRecti(GLint x1, GLint y1, GLint x2, GLint y2) { glRectf((float)x1, (float)y1, (float)x2, (float)y2); }

/* ---- primitives -> triangles ---- */
static fm_color fgl_color(const float* c)
{
    uint32_t v[4];
    for (int i = 0; i < 4; i++) v[i] = (uint32_t)(c[i] <= 0 ? 0 : (c[i] >= 1 ? 255 : c[i] * 255.0f + 0.5f));
    return FM_RGBA(v[0], v[1], v[2], v[3]);
}

static void fgl_fmv(fm3d_vertex* o, const fgl_vtx* v, const fm_mat4* texm)
{
    float w = v->pos[3] != 0.0f ? v->pos[3] : 1.0f;
    o->x = v->pos[0] / w, o->y = v->pos[1] / w, o->z = v->pos[2] / w;
    o->nx = v->nrm[0], o->ny = v->nrm[1], o->nz = v->nrm[2];
    if (texm) {
        fm_vec4 t = fm_mat4_mul_vec4(*texm, fm_v4(v->tex[0], v->tex[1], 0.0f, 1.0f));
        o->u = t.x, o->v = t.y;
    } else {
        o->u = v->tex[0], o->v = v->tex[1];
    }
    o->color = fgl_color(v->col);
}

static fm3d_light fgl_fm_light(const fgl_ctx* c, int i)
{
    const float* p = c->light[i].pos;
    fm3d_light   L;
    if (p[3] == 0.0f) {
        L           = fm3d_light_default(FM3D_LIGHT_DIRECTIONAL);
        L.direction = fm_v3_normalize(fm_v3(-p[0], -p[1], -p[2]));
    } else {
        int spot = c->light[i].spot_cut < 180.0f;
        L        = fm3d_light_default(spot ? FM3D_LIGHT_SPOT : FM3D_LIGHT_POINT);
        L.position = fm_v3(p[0] / p[3], p[1] / p[3], p[2] / p[3]);
        if (spot) {
            L.direction     = fm_v3_normalize(fm_v3(c->light[i].spot_dir[0], c->light[i].spot_dir[1], c->light[i].spot_dir[2]));
            L.spot_cutoff   = fm_radians(c->light[i].spot_cut);
            L.spot_exponent = c->light[i].spot_exp;
        }
        L.constant = c->light[i].att[0], L.linear = c->light[i].att[1], L.quadratic = c->light[i].att[2];
    }
    L.ambient  = fm_v3(c->light[i].amb[0], c->light[i].amb[1], c->light[i].amb[2]);
    L.diffuse  = fm_v3(c->light[i].dif[0], c->light[i].dif[1], c->light[i].dif[2]);
    L.specular = fm_v3(c->light[i].spe[0], c->light[i].spe[1], c->light[i].spe[2]);
    return L;
}

static void fgl_sync_fixed(fgl_ctx* c)
{
    fm3d_ctx* f   = c->c3;
    int       lit = (c->enables & FGL_E_LIGHTING) != 0;
    fm3d_set_lighting(f, lit);
    if (lit) {
        for (int i = 0; i < FGL_LIGHTS; i++) {
            if ((c->light_on >> i) & 1) {
                fm3d_light L = fgl_fm_light(c, i);
                fm3d_set_light(f, i, &L);
            } else {
                fm3d_set_light(f, i, NULL);
            }
        }
        fm3d_material m = fm3d_material_default();
        m.ambient       = fm_v3(c->mat.amb[0], c->mat.amb[1], c->mat.amb[2]);
        m.diffuse       = fm_v3(c->mat.dif[0], c->mat.dif[1], c->mat.dif[2]);
        m.specular      = fm_v3(c->mat.spe[0], c->mat.spe[1], c->mat.spe[2]);
        m.emission      = fm_v3(c->mat.emi[0], c->mat.emi[1], c->mat.emi[2]);
        m.alpha         = c->mat.dif[3];
        m.shininess     = c->mat.shin;
        fm3d_set_material(f, &m);
        fm3d_set_ambient_light(f, fm_v3(c->light_model_ambient[0], c->light_model_ambient[1], c->light_model_ambient[2]));
        fm3d_set_color_material(f, (c->enables & FGL_E_COLMAT) != 0);
    }
    fgl_tex*      t = (c->enables & FGL_E_TEX2D) && c->unit_tex[0] ? fgl_texture(c, c->unit_tex[0], 0) : NULL;
    fm3d_texture* ft = NULL;
    fm3d_sampler  s;
    if (fgl_texture_use(c, t, 0, &ft, &s)) {
        fm3d_set_texture(f, ft, &s);
        fm3d_set_texenv(f, c->tex_env == GL_REPLACE ? FM3D_TEXENV_REPLACE
                                                    : (c->tex_env == GL_DECAL ? FM3D_TEXENV_DECAL
                                                                               : (c->tex_env == GL_ADD ? FM3D_TEXENV_ADD : FM3D_TEXENV_MODULATE)));
    } else {
        fm3d_set_texture(f, NULL, NULL);
    }
}

/* GL's provoking vertex for flat shading: the last vertex of each
 * triangle (the first vertex for GL_POLYGON) */
/* GL_POINTS / GL_LINES / GL_LINE_STRIP / GL_LINE_LOOP: points or segment
 * pairs; fatmap expands them after the vertex stage */
static void fgl_draw_lines(fgl_ctx* c, GLenum prim, const fgl_vtx* v, int n)
{
    int per = prim == GL_POINTS ? 1 : 2;
    int np  = prim == GL_POINTS ? n : (prim == GL_LINES ? n / 2 : (prim == GL_LINE_STRIP ? n - 1 : (n >= 2 ? n : 0)));
    if (np <= 0) return;
    fm3d_vertex* t = (fm3d_vertex*)malloc((size_t)np * (size_t)per * sizeof(fm3d_vertex));
    if (!t) {
        fgl_error(GL_OUT_OF_MEMORY);
        return;
    }
    const fm_mat4* texm = NULL;
    const fm_mat4* tm   = &c->mstack[FGL_TEXM][c->msp[FGL_TEXM]];
    fm_mat4        id   = fm_mat4_identity();
    if (memcmp(tm, &id, sizeof(id))) texm = tm;
    int flat = c->shade_model == GL_FLAT;
    for (int i = 0; i < np; i++) {
        if (per == 1) {
            fgl_fmv(&t[i], &v[i], texm);
            continue;
        }
        int a = prim == GL_LINES ? 2 * i : i, b = prim == GL_LINES ? 2 * i + 1 : (i + 1) % n;
        fgl_fmv(&t[2 * i], &v[a], texm), fgl_fmv(&t[2 * i + 1], &v[b], texm);
        if (flat) { /* the segment's last vertex provokes */
            t[2 * i].color = t[2 * i + 1].color;
            t[2 * i].nx = t[2 * i + 1].nx, t[2 * i].ny = t[2 * i + 1].ny, t[2 * i].nz = t[2 * i + 1].nz;
        }
    }
    fgl_sync(c);
    fm3d_set_program(c->c3, NULL); /* fixed function */
    fgl_sync_fixed(c);
    fm3d_set_primitive(c->c3, per == 1 ? FM3D_PRIM_POINTS : FM3D_PRIM_LINES);
    fm3d_set_line_width(c->c3, c->line_width);
    fm3d_set_point_size(c->c3, c->point_size);
    fm3d_draw(c->c3, t, np * per);
    fm3d_set_primitive(c->c3, FM3D_PRIM_TRIANGLES);
    free(t);
}

void fgl_draw_prim(fgl_ctx* c, GLenum prim, const fgl_vtx* v, int n)
{
    int ntri = 0;
    switch (prim) {
    case GL_TRIANGLES: ntri = n / 3; break;
    case GL_TRIANGLE_STRIP: case GL_TRIANGLE_FAN: case GL_POLYGON: ntri = n >= 3 ? n - 2 : 0; break;
    case GL_QUADS: ntri = (n / 4) * 2; break;
    case GL_QUAD_STRIP: ntri = n >= 4 ? ((n - 2) / 2) * 2 : 0; break;
    case GL_POINTS: case GL_LINES: case GL_LINE_STRIP: case GL_LINE_LOOP: fgl_draw_lines(c, prim, v, n); return;
    default: fgl_error(GL_INVALID_ENUM); return;
    }
    if (!ntri) return;
    fm3d_vertex* t = (fm3d_vertex*)malloc((size_t)ntri * 3 * sizeof(fm3d_vertex));
    if (!t) {
        fgl_error(GL_OUT_OF_MEMORY);
        return;
    }
    const fm_mat4* texm = NULL;
    const fm_mat4* tm   = &c->mstack[FGL_TEXM][c->msp[FGL_TEXM]];
    fm_mat4        id   = fm_mat4_identity();
    if (memcmp(tm, &id, sizeof(id))) texm = tm;
    int k = 0, flat = c->shade_model == GL_FLAT;
    for (int i = 0; i < ntri; i++) {
        int a, b, d, pv; /* the triangle's vertices, the provoking one */
        switch (prim) {
        case GL_TRIANGLES: a = 3 * i, b = a + 1, d = a + 2, pv = d; break;
        case GL_TRIANGLE_STRIP: /* keep the winding of every second triangle */
            if (i & 1) a = i + 1, b = i, d = i + 2;
            else a = i, b = i + 1, d = i + 2;
            pv = i + 2;
            break;
        case GL_TRIANGLE_FAN: a = 0, b = i + 1, d = i + 2, pv = d; break;
        case GL_POLYGON: a = 0, b = i + 1, d = i + 2, pv = 0; break;
        case GL_QUADS: {
            int q = (i >> 1) * 4;
            if (i & 1) a = q, b = q + 2, d = q + 3;
            else a = q, b = q + 1, d = q + 2;
            pv = q + 3;
            break;
        }
        default: { /* GL_QUAD_STRIP: quad j = v[2j], v[2j+1], v[2j+3], v[2j+2] */
            int q = (i >> 1) * 2;
            if (i & 1) a = q + 1, b = q + 3, d = q + 2;
            else a = q, b = q + 1, d = q + 2;
            pv = q + 3;
            break;
        }
        }
        fgl_fmv(&t[k], &v[a], texm), fgl_fmv(&t[k + 1], &v[b], texm), fgl_fmv(&t[k + 2], &v[d], texm);
        if (flat) {
            fm3d_vertex p;
            fgl_fmv(&p, &v[pv], texm);
            for (int j = 0; j < 3; j++) {
                t[k + j].color = p.color;
                t[k + j].nx = p.nx, t[k + j].ny = p.ny, t[k + j].nz = p.nz;
            }
        }
        k += 3;
    }
    fgl_sync(c);
    fm3d_set_program(c->c3, NULL); /* fixed function */
    fgl_sync_fixed(c);
    fm3d_draw(c->c3, t, k);
    free(t);
}

/* ---- client vertex arrays (no buffer objects) ---- */
static int fgl_va_index(GLenum a)
{
    switch (a) {
    case GL_VERTEX_ARRAY: return 0;
    case GL_NORMAL_ARRAY: return 1;
    case GL_COLOR_ARRAY: return 2;
    case GL_TEXTURE_COORD_ARRAY: return 3;
    default: return -1;
    }
}

void APIENTRY glEnableClientState(GLenum a)
{
    FGL_CTX_OR_RETURN(c);
    int i = fgl_va_index(a);
    if (i >= 0) c->va[i].on = 1;
}
void APIENTRY glDisableClientState(GLenum a)
{
    FGL_CTX_OR_RETURN(c);
    int i = fgl_va_index(a);
    if (i >= 0) c->va[i].on = 0;
}

static void fgl_set_va(int i, GLint size, GLenum type, GLsizei stride, const void* p)
{
    FGL_CTX_OR_RETURN(c);
    c->va[i].size = size, c->va[i].type = type, c->va[i].stride = stride, c->va[i].ptr = p, c->va[i].buffer = c->array_buffer;
}
void APIENTRY glVertexPointer(GLint size, GLenum type, GLsizei stride, const void* p) { fgl_set_va(0, size, type, stride, p); }
void APIENTRY glNormalPointer(GLenum type, GLsizei stride, const void* p) { fgl_set_va(1, 3, type, stride, p); }
void APIENTRY glColorPointer(GLint size, GLenum type, GLsizei stride, const void* p) { fgl_set_va(2, size, type, stride, p); }
void APIENTRY glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void* p) { fgl_set_va(3, size, type, stride, p); }

static int fgl_type_size(GLenum t)
{
    switch (t) {
    case GL_BYTE: case GL_UNSIGNED_BYTE: return 1;
    case GL_SHORT: case GL_UNSIGNED_SHORT: return 2;
    case GL_DOUBLE: return 8;
    default: return 4;
    }
}

/* component j of array i at element e, as float (normalized for colors) */
static float fgl_va_get(fgl_ctx* c, int i, int e, int j, int normalize)
{
    const char* p = (const char*)c->va[i].ptr;
    if (c->va[i].buffer) { /* an offset into a buffer object */
        fgl_buf* b = fgl_buffer(c, c->va[i].buffer);
        if (!b || !b->data) return 0.0f;
        p = (const char*)b->data + (uintptr_t)c->va[i].ptr;
    }
    int         s = c->va[i].stride ? c->va[i].stride : c->va[i].size * fgl_type_size(c->va[i].type);
    p += (size_t)e * (size_t)s + (size_t)j * (size_t)fgl_type_size(c->va[i].type);
    switch (c->va[i].type) {
    case GL_FLOAT: return *(const float*)p;
    case GL_DOUBLE: return (float)*(const double*)p;
    case GL_INT: return normalize ? (float)*(const int32_t*)p / 2147483647.0f : (float)*(const int32_t*)p;
    case GL_UNSIGNED_INT: return normalize ? (float)*(const uint32_t*)p / 4294967295.0f : (float)*(const uint32_t*)p;
    case GL_SHORT: return normalize ? (float)*(const int16_t*)p / 32767.0f : (float)*(const int16_t*)p;
    case GL_UNSIGNED_SHORT: return normalize ? (float)*(const uint16_t*)p / 65535.0f : (float)*(const uint16_t*)p;
    case GL_BYTE: return normalize ? (float)*(const int8_t*)p / 127.0f : (float)*(const int8_t*)p;
    default: return normalize ? (float)*(const uint8_t*)p / 255.0f : (float)*(const uint8_t*)p;
    }
}

static void fgl_fetch(fgl_ctx* c, int e, fgl_vtx* v)
{
    v->pos[0] = v->pos[1] = v->pos[2] = 0, v->pos[3] = 1;
    for (int j = 0; j < c->va[0].size && j < 4; j++) v->pos[j] = fgl_va_get(c, 0, e, j, 0);
    if (c->va[1].on) for (int j = 0; j < 3; j++) v->nrm[j] = fgl_va_get(c, 1, e, j, 1);
    else memcpy(v->nrm, c->cur_normal, sizeof(v->nrm));
    if (c->va[2].on) {
        v->col[3] = 1;
        for (int j = 0; j < c->va[2].size && j < 4; j++) v->col[j] = fgl_va_get(c, 2, e, j, 1);
    } else {
        memcpy(v->col, c->cur_color, sizeof(v->col));
    }
    if (c->va[3].on) for (int j = 0; j < 2 && j < c->va[3].size; j++) v->tex[j] = fgl_va_get(c, 3, e, j, 0);
    else v->tex[0] = c->cur_tex[0], v->tex[1] = c->cur_tex[1];
}

void APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    FGL_CTX_OR_RETURN(c);
    if (count < 0) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    if (c->program) {
        fgl_draw_program(c, mode, first, count, 0, NULL, 0, 1);
        return;
    }
    if (!c->va[0].on || (!c->va[0].ptr && !c->va[0].buffer) || count == 0) return;
    fgl_vtx* v = (fgl_vtx*)malloc((size_t)count * sizeof(fgl_vtx));
    if (!v) return;
    for (int i = 0; i < count; i++) fgl_fetch(c, first + i, &v[i]);
    fgl_draw_prim(c, mode, v, count);
    free(v);
}

void APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type, const void* idx)
{
    FGL_CTX_OR_RETURN(c);
    if (count < 0) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    if (c->program) {
        fgl_draw_program(c, mode, 0, count, type, idx, 0, 1);
        return;
    }
    fgl_vao* vao = fgl_cur_vao(c);
    if (vao->elements) { /* indices in a buffer object */
        fgl_buf* eb = fgl_buffer(c, vao->elements);
        idx         = eb && eb->data ? eb->data + (uintptr_t)idx : NULL;
    }
    if (!c->va[0].on || (!c->va[0].ptr && !c->va[0].buffer) || !idx || count == 0) return;
    fgl_vtx* v = (fgl_vtx*)malloc((size_t)count * sizeof(fgl_vtx));
    if (!v) return;
    for (int i = 0; i < count; i++) {
        int e = type == GL_UNSIGNED_BYTE ? ((const uint8_t*)idx)[i]
                                         : (type == GL_UNSIGNED_SHORT ? ((const uint16_t*)idx)[i] : (int)((const uint32_t*)idx)[i]);
        fgl_fetch(c, e, &v[i]);
    }
    fgl_draw_prim(c, mode, v, count);
    free(v);
}
