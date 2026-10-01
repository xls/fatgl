/*
 * fatgl - 2D textures. Level 0 is kept as a premultiplied ARGB32 fatmap
 * surface; mipmaps are built by fatmap from it when a mipmap filter is used
 * (images given for levels > 0 are ignored for now).
 */
#include "fgl.h"

fgl_tex* fgl_texture(fgl_ctx* c, GLuint name, int create)
{
    for (int i = 0; i < c->ntex; i++)
        if (c->tex[i].used && c->tex[i].name == name) return &c->tex[i];
    if (!create) return NULL;
    int slot = -1;
    for (int i = 0; i < c->ntex && slot < 0; i++)
        if (!c->tex[i].used) slot = i;
    if (slot < 0) {
        fgl_tex* n = (fgl_tex*)realloc(c->tex, ((size_t)c->ntex + 16) * sizeof(fgl_tex));
        if (!n) return NULL;
        memset(n + c->ntex, 0, 16 * sizeof(fgl_tex));
        c->tex = n, slot = c->ntex, c->ntex += 16;
    }
    fgl_tex* t = &c->tex[slot];
    memset(t, 0, sizeof(*t));
    t->used = 1, t->name = name;
    t->min_filter = GL_NEAREST_MIPMAP_LINEAR, t->mag_filter = GL_LINEAR, t->wrap_s = t->wrap_t = GL_REPEAT;
    return t;
}

static GLuint g_next_tex = 1;

void APIENTRY glGenTextures(GLsizei n, GLuint* out)
{
    FGL_CTX_OR_RETURN(c);
    for (GLsizei i = 0; i < n; i++) {
        while (fgl_texture(c, g_next_tex, 0)) g_next_tex++;
        out[i] = g_next_tex++;
        fgl_texture(c, out[i], 1);
    }
}

void APIENTRY glDeleteTextures(GLsizei n, const GLuint* names)
{
    FGL_CTX_OR_RETURN(c);
    fgl_flush(c); /* deferred draws may still sample them */
    for (GLsizei i = 0; i < n; i++) {
        fgl_tex* t = names[i] ? fgl_texture(c, names[i], 0) : NULL;
        if (!t) continue;
        fm3d_texture_release(t->tex);
        fm_surface_destroy(t->level0);
        memset(t, 0, sizeof(*t));
        for (int u = 0; u < FGL_UNITS; u++)
            if (c->unit_tex[u] == names[i]) c->unit_tex[u] = 0;
    }
}

GLboolean APIENTRY glIsTexture(GLuint name)
{
    fgl_ctx* c = fgl_cur();
    return c && name && fgl_texture(c, name, 0) ? GL_TRUE : GL_FALSE;
}

void APIENTRY glBindTexture(GLenum target, GLuint name)
{
    FGL_CTX_OR_RETURN(c);
    if (c->list_compiling && fgl_record(c, FGL_OP_BINDTEXTURE, target, name, NULL, 0)) return;
    if (target != GL_TEXTURE_2D) {
        fgl_unimplemented("glBindTexture (targets other than GL_TEXTURE_2D)");
        return;
    }
    if (name) fgl_texture(c, name, 1);
    FGL_BOUND_TEX(c) = name;
}

/* one texel of client memory -> straight RGBA 0..255 */
static void fgl_texel(GLenum fmt, GLenum type, const uint8_t* p, int x, uint8_t* o)
{
    uint8_t r = 0, g = 0, b = 0, a = 255;
    if (type == GL_FLOAT) {
        const float* f = (const float*)p;
#define FGL_F(i) (uint8_t)(f[i] <= 0 ? 0 : (f[i] >= 1 ? 255 : f[i] * 255.0f + 0.5f))
        switch (fmt) {
        case GL_RGBA: r = FGL_F(4 * x), g = FGL_F(4 * x + 1), b = FGL_F(4 * x + 2), a = FGL_F(4 * x + 3); break;
        case GL_RGB: r = FGL_F(3 * x), g = FGL_F(3 * x + 1), b = FGL_F(3 * x + 2); break;
        default: r = g = b = FGL_F(x); break;
        }
#undef FGL_F
    } else {
        switch (fmt) {
        case GL_RGBA: r = p[4 * x], g = p[4 * x + 1], b = p[4 * x + 2], a = p[4 * x + 3]; break;
        case GL_BGRA: b = p[4 * x], g = p[4 * x + 1], r = p[4 * x + 2], a = p[4 * x + 3]; break;
        case GL_RGB: r = p[3 * x], g = p[3 * x + 1], b = p[3 * x + 2]; break;
        case GL_BGR: b = p[3 * x], g = p[3 * x + 1], r = p[3 * x + 2]; break;
        case GL_LUMINANCE: r = g = b = p[x]; break;
        case GL_LUMINANCE_ALPHA: r = g = b = p[2 * x], a = p[2 * x + 1]; break;
        case GL_ALPHA: r = g = b = 255, a = p[x]; break;
        case GL_RED: r = p[x], g = b = 0; break;
        case GL_RG: r = p[2 * x], g = p[2 * x + 1], b = 0; break;
        default: r = g = b = p[x]; break;
        }
    }
    o[0] = r, o[1] = g, o[2] = b, o[3] = a;
}

static int fgl_texel_bytes(GLenum fmt, GLenum type)
{
    int comps = fmt == GL_RGBA || fmt == GL_BGRA ? 4 : (fmt == GL_RGB || fmt == GL_BGR ? 3 : (fmt == GL_LUMINANCE_ALPHA || fmt == GL_RG ? 2 : 1));
    return comps * (type == GL_FLOAT ? 4 : 1);
}

/* copy client pixels (unpack alignment / row length) into rows of s */
static void fgl_upload(fgl_ctx* c, fm_surface* s, int x0, int y0, int w, int h, GLenum fmt, GLenum type, const void* pixels)
{
    int    bpp = fgl_texel_bytes(fmt, type), rowpx = c->unpack_row > 0 ? c->unpack_row : w;
    size_t stride = (size_t)rowpx * (size_t)bpp;
    stride        = (stride + (size_t)c->unpack_align - 1) / (size_t)c->unpack_align * (size_t)c->unpack_align;
    for (int y = 0; y < h; y++) {
        const uint8_t* row = (const uint8_t*)pixels + (size_t)y * stride;
        uint32_t*      d   = fm_surface_row32(s, y0 + y) + x0;
        for (int x = 0; x < w; x++) {
            uint8_t t[4];
            fgl_texel(fmt, type, row, x, t);
            d[x] = fm_premultiply(FM_RGBA(t[0], t[1], t[2], t[3]));
        }
    }
}

void APIENTRY glTexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border, GLenum fmt, GLenum type,
                           const void* pixels)
{
    FGL_CTX_OR_RETURN(c);
    (void)ifmt;
    if (target != GL_TEXTURE_2D) {
        fgl_unimplemented("glTexImage2D (targets other than GL_TEXTURE_2D)");
        return;
    }
    if (w < 0 || h < 0 || border != 0 || level < 0) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    if (type != GL_UNSIGNED_BYTE && type != GL_FLOAT) {
        fgl_unimplemented("glTexImage2D (pixel types other than GL_UNSIGNED_BYTE / GL_FLOAT)");
        return;
    }
    if (level > 0) return; /* mipmaps come from level 0 */
    fgl_tex* t = FGL_BOUND_TEX(c) ? fgl_texture(c, FGL_BOUND_TEX(c), 1) : NULL;
    if (!t) {
        fgl_error(GL_INVALID_OPERATION);
        return;
    }
    fgl_flush(c);
    fm3d_texture_release(t->tex);
    fm_surface_destroy(t->level0);
    t->tex    = NULL;
    t->level0 = w > 0 && h > 0 ? fm_surface_create(w, h, FM_FORMAT_ARGB32) : NULL;
    if (t->level0 && pixels) fgl_upload(c, t->level0, 0, 0, w, h, fmt, type, pixels);
}

void APIENTRY glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type,
                              const void* pixels)
{
    FGL_CTX_OR_RETURN(c);
    fgl_tex* t = FGL_BOUND_TEX(c) ? fgl_texture(c, FGL_BOUND_TEX(c), 0) : NULL;
    if (target != GL_TEXTURE_2D || !t || !t->level0 || level != 0) {
        if (level > 0) return;
        fgl_error(GL_INVALID_OPERATION);
        return;
    }
    if (x < 0 || y < 0 || x + w > t->level0->width || y + h > t->level0->height) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    fgl_flush(c);
    fgl_upload(c, t->level0, x, y, w, h, fmt, type, pixels);
    fm3d_texture_release(t->tex); /* rebuilt (with its mipmaps) on the next use */
    t->tex = NULL;
}

void APIENTRY glTexParameteri(GLenum target, GLenum p, GLint v)
{
    FGL_CTX_OR_RETURN(c);
    fgl_tex* t = target == GL_TEXTURE_2D && FGL_BOUND_TEX(c) ? fgl_texture(c, FGL_BOUND_TEX(c), 1) : NULL;
    if (!t) return;
    switch (p) {
    case GL_TEXTURE_MIN_FILTER: t->min_filter = (GLenum)v; break;
    case GL_TEXTURE_MAG_FILTER: t->mag_filter = (GLenum)v; break;
    case GL_TEXTURE_WRAP_S: t->wrap_s = (GLenum)v; break;
    case GL_TEXTURE_WRAP_T: t->wrap_t = (GLenum)v; break;
    default: break;
    }
}
void APIENTRY glTexParameterf(GLenum target, GLenum p, GLfloat v) { glTexParameteri(target, p, (GLint)v); }
void APIENTRY glTexParameteriv(GLenum target, GLenum p, const GLint* v) { glTexParameteri(target, p, v[0]); }
void APIENTRY glTexParameterfv(GLenum target, GLenum p, const GLfloat* v) { glTexParameteri(target, p, (GLint)v[0]); }

void APIENTRY glTexEnvi(GLenum target, GLenum p, GLint v)
{
    FGL_CTX_OR_RETURN(c);
    if (target == GL_TEXTURE_ENV && p == GL_TEXTURE_ENV_MODE) c->tex_env = (GLenum)v;
}
void APIENTRY glTexEnvf(GLenum target, GLenum p, GLfloat v) { glTexEnvi(target, p, (GLint)v); }
void APIENTRY glTexEnvfv(GLenum target, GLenum p, const GLfloat* v) { glTexEnvi(target, p, (GLint)v[0]); }
void APIENTRY glTexEnviv(GLenum target, GLenum p, const GLint* v) { glTexEnvi(target, p, v[0]); }

void APIENTRY glGenerateMipmap(GLenum target)
{
    FGL_CTX_OR_RETURN(c);
    fgl_tex* t = target == GL_TEXTURE_2D && FGL_BOUND_TEX(c) ? fgl_texture(c, FGL_BOUND_TEX(c), 0) : NULL;
    if (!t) return;
    fm3d_texture_release(t->tex); /* rebuilt with mipmaps (from level 0) on the next use */
    t->tex = NULL;
}
