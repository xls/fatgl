/*
 * fatgl - 2D textures. Level 0 is kept as a straight (not premultiplied)
 * ARGB32 fatmap surface, like every fatgl color buffer; mipmaps are built by fatmap from it when a mipmap filter is used
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
    t->ifmt = GL_RGBA, t->min_lod = -1000, t->max_lod = 1000, t->max_level = 1000;
    t->compare_mode = GL_NONE, t->compare_func = GL_LEQUAL;
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
        fgl_texture_detach(c, names[i]);
        fgl_surface_gone(c, t->level0);
        fgl_surface_gone(c, t->depth);
        fm3d_texture_release(t->tex);
        fm_surface_destroy(t->level0);
        fm_surface_destroy(t->depth);
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

/* copy client pixels (unpack alignment / row length / skips, pixel unpack
 * buffer) into rows of s, as straight RGBA8 */
static void fgl_upload(fgl_ctx* c, fm_surface* s, int x0, int y0, int w, int h, GLenum fmt, GLenum type, const void* pixels)
{
    pixels  = fgl_unpack_ptr(c, pixels);
    int bpp = fgl_pixel_bytes(fmt, type);
    if (!pixels || !bpp) {
        if (!bpp) fgl_unimplemented("glTexImage2D / glTexSubImage2D (this format / type)");
        return;
    }
    int    rowpx  = c->unpack_row > 0 ? c->unpack_row : w;
    size_t stride = (size_t)rowpx * (size_t)bpp;
    stride        = (stride + (size_t)c->unpack_align - 1) / (size_t)c->unpack_align * (size_t)c->unpack_align;
    const uint8_t* base = (const uint8_t*)pixels + (size_t)c->unpack_skip_rows * stride + (size_t)c->unpack_skip_pixels * (size_t)bpp;
    for (int y = 0; y < h; y++) {
        uint32_t* d = fm_surface_row32(s, y0 + y) + x0;
        uint8_t   tmp[4 * 256];
        for (int x = 0; x < w; x += 256) {
            int n = w - x < 256 ? w - x : 256;
            fgl_pixels_to_rgba8(fmt, type, base + (size_t)y * stride + (size_t)x * (size_t)bpp, n, tmp);
            for (int i = 0; i < n; i++) d[x + i] = FM_RGBA(tmp[4 * i], tmp[4 * i + 1], tmp[4 * i + 2], tmp[4 * i + 3]);
        }
    }
}

/* a decoded RGBA8 image into rows of s */
static void fgl_put_rgba8(fm_surface* s, int x0, int y0, int w, int h, const uint8_t* rgba)
{
    for (int y = 0; y < h; y++) {
        uint32_t*      d = fm_surface_row32(s, y0 + y) + x0;
        const uint8_t* p = rgba + (size_t)y * (size_t)w * 4;
        for (int x = 0; x < w; x++) d[x] = FM_RGBA(p[4 * x], p[4 * x + 1], p[4 * x + 2], p[4 * x + 3]);
    }
}

void APIENTRY glTexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border, GLenum fmt, GLenum type,
                           const void* pixels)
{
    FGL_CTX_OR_RETURN(c);
    if (target != GL_TEXTURE_2D) {
        fgl_unimplemented("glTexImage2D (targets other than GL_TEXTURE_2D)");
        return;
    }
    if (w < 0 || h < 0 || border != 0 || level < 0) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    int depth = fmt == GL_DEPTH_COMPONENT || fmt == GL_DEPTH_STENCIL;
    if (level > 0) return; /* mipmaps come from level 0 */
    fgl_tex* t = FGL_BOUND_TEX(c) ? fgl_texture(c, FGL_BOUND_TEX(c), 1) : NULL;
    if (!t) {
        fgl_error(GL_INVALID_OPERATION);
        return;
    }
    fgl_flush(c);
    fgl_surface_gone(c, t->level0); /* a framebuffer may draw into it */
    fgl_surface_gone(c, t->depth);
    c->tgt_color = NULL;
    fm3d_texture_release(t->tex);
    fm_surface_destroy(t->level0);
    fm_surface_destroy(t->depth);
    t->tex = NULL, t->level0 = t->depth = NULL, t->rendered = 0, t->ifmt = (GLenum)ifmt;
    if (w <= 0 || h <= 0) return;
    if (depth) { /* a depth (stencil) texture: a fatmap depth surface */
        fm_format f = ifmt == GL_DEPTH_COMPONENT16 ? FM_FORMAT_D16
                      : (ifmt == GL_DEPTH_COMPONENT32F || ifmt == GL_DEPTH_COMPONENT32) ? FM_FORMAT_D32F : FM_FORMAT_D24S8;
        t->depth = fm_surface_create(w, h, f);
        pixels = fgl_unpack_ptr(c, pixels);
        if (t->depth && pixels && type == GL_FLOAT && fmt == GL_DEPTH_COMPONENT && f == FM_FORMAT_D32F)
            for (int y = 0; y < h; y++) memcpy(fm_surface_rowf(t->depth, y), (const float*)pixels + (size_t)y * (size_t)w, (size_t)w * 4);
        else if (pixels)
            fgl_unimplemented("glTexImage2D (depth texture data other than GL_FLOAT into GL_DEPTH_COMPONENT32F)");
        return;
    }
    t->level0 = fm_surface_create(w, h, FM_FORMAT_ARGB32);
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

/* compressed images are decoded to RGBA8 (S3TC / RGTC; level 0, mipmaps come from it) */
void APIENTRY glCompressedTexImage2D(GLenum target, GLint level, GLenum ifmt, GLsizei w, GLsizei h, GLint border, GLsizei size,
                                     const void* data)
{
    FGL_CTX_OR_RETURN(c);
    if (!fgl_compressed_block_bytes(ifmt)) {
        fgl_error(GL_INVALID_ENUM);
        return;
    }
    if (level > 0) return;
    glTexImage2D(target, level, (GLint)ifmt, w, h, border, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    fgl_tex* t = FGL_BOUND_TEX(c) ? fgl_texture(c, FGL_BOUND_TEX(c), 0) : NULL;
    data       = fgl_unpack_ptr(c, data);
    if (!t || !t->level0 || !data) return;
    uint8_t* rgba = (uint8_t*)malloc((size_t)w * (size_t)h * 4);
    if (!rgba) {
        fgl_error(GL_OUT_OF_MEMORY);
        return;
    }
    if (fgl_decompress(ifmt, (const uint8_t*)data, (size_t)size, w, h, rgba)) fgl_put_rgba8(t->level0, 0, 0, w, h, rgba);
    else fgl_error(GL_INVALID_VALUE);
    free(rgba);
}

void APIENTRY glCompressedTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLsizei size,
                                        const void* data)
{
    FGL_CTX_OR_RETURN(c);
    fgl_tex* t = target == GL_TEXTURE_2D && FGL_BOUND_TEX(c) ? fgl_texture(c, FGL_BOUND_TEX(c), 0) : NULL;
    if (level > 0) return;
    if (!t || !t->level0 || !fgl_compressed_block_bytes(fmt) || x < 0 || y < 0 || x + w > t->level0->width ||
        y + h > t->level0->height) {
        fgl_error(GL_INVALID_OPERATION);
        return;
    }
    data          = fgl_unpack_ptr(c, data);
    uint8_t* rgba = data ? (uint8_t*)malloc((size_t)w * (size_t)h * 4) : NULL;
    if (!rgba) return;
    fgl_flush(c);
    if (fgl_decompress(fmt, (const uint8_t*)data, (size_t)size, w, h, rgba)) fgl_put_rgba8(t->level0, x, y, w, h, rgba);
    free(rgba);
    fm3d_texture_release(t->tex);
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
    case GL_TEXTURE_BASE_LEVEL: t->base_level = v; break;
    case GL_TEXTURE_MAX_LEVEL: t->max_level = v; break;
    case GL_TEXTURE_COMPARE_MODE: t->compare_mode = (GLenum)v; break;
    case GL_TEXTURE_COMPARE_FUNC: t->compare_func = (GLenum)v; break;
    case GL_TEXTURE_MIN_LOD: t->min_lod = (float)v; break;
    case GL_TEXTURE_MAX_LOD: t->max_lod = (float)v; break;
    default: break; /* wrap r, swizzles, anisotropy, generate mipmap: accepted */
    }
}
void APIENTRY glTexParameterf(GLenum target, GLenum p, GLfloat v)
{
    FGL_CTX_OR_RETURN(c);
    fgl_tex* t = target == GL_TEXTURE_2D && FGL_BOUND_TEX(c) ? fgl_texture(c, FGL_BOUND_TEX(c), 1) : NULL;
    if (t && p == GL_TEXTURE_MIN_LOD) t->min_lod = v;
    else if (t && p == GL_TEXTURE_MAX_LOD) t->max_lod = v;
    else if (t && p == GL_TEXTURE_LOD_BIAS) t->lod_bias = v;
    else glTexParameteri(target, p, (GLint)v);
}
void APIENTRY glTexParameteriv(GLenum target, GLenum p, const GLint* v)
{
    if (p == GL_TEXTURE_BORDER_COLOR) {
        GLfloat f[4] = { v[0] / 2147483647.0f, v[1] / 2147483647.0f, v[2] / 2147483647.0f, v[3] / 2147483647.0f };
        glTexParameterfv(target, p, f);
        return;
    }
    glTexParameteri(target, p, v[0]);
}
void APIENTRY glTexParameterfv(GLenum target, GLenum p, const GLfloat* v)
{
    FGL_CTX_OR_RETURN(c);
    fgl_tex* t = target == GL_TEXTURE_2D && FGL_BOUND_TEX(c) ? fgl_texture(c, FGL_BOUND_TEX(c), 1) : NULL;
    if (t && p == GL_TEXTURE_BORDER_COLOR) memcpy(t->border, v, 16);
    else glTexParameterf(target, p, v[0]);
}
void APIENTRY glTexParameterIiv(GLenum target, GLenum p, const GLint* v) { glTexParameteri(target, p, v[0]); }
void APIENTRY glTexParameterIuiv(GLenum target, GLenum p, const GLuint* v) { glTexParameteri(target, p, (GLint)v[0]); }

static int fgl_tex_query(GLenum target, GLenum p, float* o)
{
    fgl_ctx* c = fgl_cur();
    fgl_tex* t = c && target == GL_TEXTURE_2D && FGL_BOUND_TEX(c) ? fgl_texture(c, FGL_BOUND_TEX(c), 1) : NULL;
    if (!t) {
        if (c) fgl_error(target == GL_TEXTURE_2D ? GL_INVALID_OPERATION : GL_INVALID_ENUM);
        return 0;
    }
    switch (p) {
    case GL_TEXTURE_MIN_FILTER: o[0] = (float)t->min_filter; return 1;
    case GL_TEXTURE_MAG_FILTER: o[0] = (float)t->mag_filter; return 1;
    case GL_TEXTURE_WRAP_S: o[0] = (float)t->wrap_s; return 1;
    case GL_TEXTURE_WRAP_T: o[0] = (float)t->wrap_t; return 1;
    case GL_TEXTURE_WRAP_R: o[0] = (float)GL_REPEAT; return 1;
    case GL_TEXTURE_BASE_LEVEL: o[0] = (float)t->base_level; return 1;
    case GL_TEXTURE_MAX_LEVEL: o[0] = (float)t->max_level; return 1;
    case GL_TEXTURE_MIN_LOD: o[0] = t->min_lod; return 1;
    case GL_TEXTURE_MAX_LOD: o[0] = t->max_lod; return 1;
    case GL_TEXTURE_LOD_BIAS: o[0] = t->lod_bias; return 1;
    case GL_TEXTURE_COMPARE_MODE: o[0] = (float)t->compare_mode; return 1;
    case GL_TEXTURE_COMPARE_FUNC: o[0] = (float)t->compare_func; return 1;
    case GL_TEXTURE_BORDER_COLOR: memcpy(o, t->border, 16); return 4;
    case GL_TEXTURE_IMMUTABLE_FORMAT: o[0] = 0; return 1;
    case GL_TEXTURE_SWIZZLE_R: o[0] = GL_RED; return 1;
    case GL_TEXTURE_SWIZZLE_G: o[0] = GL_GREEN; return 1;
    case GL_TEXTURE_SWIZZLE_B: o[0] = GL_BLUE; return 1;
    case GL_TEXTURE_SWIZZLE_A: o[0] = GL_ALPHA; return 1;
    default: fgl_error(GL_INVALID_ENUM); return 0;
    }
}
void APIENTRY glGetTexParameterfv(GLenum target, GLenum p, GLfloat* v)
{
    float o[4];
    int   n = fgl_tex_query(target, p, o);
    for (int i = 0; i < n; i++) v[i] = o[i];
}
void APIENTRY glGetTexParameteriv(GLenum target, GLenum p, GLint* v)
{
    float o[4];
    int   n = fgl_tex_query(target, p, o);
    for (int i = 0; i < n; i++) v[i] = p == GL_TEXTURE_BORDER_COLOR ? (GLint)(o[i] * 2147483647.0f) : (GLint)o[i];
}
void APIENTRY glGetTexParameterIiv(GLenum target, GLenum p, GLint* v) { glGetTexParameteriv(target, p, v); }
void APIENTRY glGetTexParameterIuiv(GLenum target, GLenum p, GLuint* v) { glGetTexParameteriv(target, p, (GLint*)v); }

void APIENTRY glGetTexLevelParameteriv(GLenum target, GLint level, GLenum p, GLint* v)
{
    FGL_CTX_OR_RETURN(c);
    fgl_tex* t = target == GL_TEXTURE_2D && FGL_BOUND_TEX(c) ? fgl_texture(c, FGL_BOUND_TEX(c), 1) : NULL;
    if (!t) {
        if (target != GL_TEXTURE_2D && target != GL_PROXY_TEXTURE_2D) fgl_error(GL_INVALID_ENUM);
        else *v = 0;
        return;
    }
    const fm_surface* s = t->level0 ? t->level0 : t->depth;
    int w = s ? s->width >> level : 0, h = s ? s->height >> level : 0;
    if (s && level > 0) w = w < 1 ? 1 : w, h = h < 1 ? 1 : h;
    int dep = t->depth != NULL, d24 = dep && t->depth->format == FM_FORMAT_D24S8;
    switch (p) {
    case GL_TEXTURE_WIDTH: *v = w; break;
    case GL_TEXTURE_HEIGHT: *v = h; break;
    case GL_TEXTURE_DEPTH: *v = s ? 1 : 0; break;
    case GL_TEXTURE_INTERNAL_FORMAT: *v = (GLint)t->ifmt; break;
    case GL_TEXTURE_RED_SIZE: case GL_TEXTURE_GREEN_SIZE: case GL_TEXTURE_BLUE_SIZE: case GL_TEXTURE_ALPHA_SIZE:
        *v = s && !dep ? 8 : 0;
        break;
    case GL_TEXTURE_DEPTH_SIZE: *v = !dep ? 0 : (t->depth->format == FM_FORMAT_D16 ? 16 : (d24 ? 24 : 32)); break;
    case GL_TEXTURE_STENCIL_SIZE: *v = d24 ? 8 : 0; break;
    case GL_TEXTURE_COMPRESSED: *v = 0; break;
    case GL_TEXTURE_SAMPLES: *v = 0; break;
    case GL_TEXTURE_FIXED_SAMPLE_LOCATIONS: *v = 1; break;
    case GL_TEXTURE_RED_TYPE: case GL_TEXTURE_GREEN_TYPE: case GL_TEXTURE_BLUE_TYPE: case GL_TEXTURE_ALPHA_TYPE:
        *v = s && !dep ? GL_UNSIGNED_NORMALIZED : GL_NONE;
        break;
    case GL_TEXTURE_DEPTH_TYPE: *v = !dep ? GL_NONE : (t->depth->format == FM_FORMAT_D32F ? GL_FLOAT : GL_UNSIGNED_NORMALIZED); break;
    default: fgl_error(GL_INVALID_ENUM); break;
    }
}
void APIENTRY glGetTexLevelParameterfv(GLenum target, GLint level, GLenum p, GLfloat* v)
{
    GLint i = 0;
    glGetTexLevelParameteriv(target, level, p, &i);
    *v = (GLfloat)i;
}

/* level 0 back to client memory (pack alignment): RGBA / BGRA / RGB / BGR bytes or floats */
void APIENTRY glGetTexImage(GLenum target, GLint level, GLenum fmt, GLenum type, void* out)
{
    FGL_CTX_OR_RETURN(c);
    fgl_tex* t = target == GL_TEXTURE_2D && FGL_BOUND_TEX(c) ? fgl_texture(c, FGL_BOUND_TEX(c), 0) : NULL;
    if (!t || !t->level0) return;
    if (level != 0) {
        fgl_unimplemented("glGetTexImage (levels other than 0)");
        return;
    }
    int bpp = fgl_pixel_bytes(fmt, type);
    out     = fgl_pack_ptr(c, out);
    if (!bpp || !out) {
        if (!bpp) fgl_unimplemented("glGetTexImage (this format / type)");
        return;
    }
    if (t->rendered) fgl_flush(c);
    const fm_surface* s      = t->level0;
    size_t            stride = ((size_t)s->width * (size_t)bpp + (size_t)c->pack_align - 1) / (size_t)c->pack_align * (size_t)c->pack_align;
    for (int y = 0; y < s->height; y++) {
        const uint32_t* r = fm_surface_row32(s, y);
        uint8_t         tmp[4 * 256];
        for (int x = 0; x < s->width; x += 256) {
            int n = s->width - x < 256 ? s->width - x : 256;
            for (int i = 0; i < n; i++) {
                uint32_t p     = r[x + i];
                tmp[4 * i]     = (uint8_t)(p >> 16), tmp[4 * i + 1] = (uint8_t)(p >> 8), tmp[4 * i + 2] = (uint8_t)p;
                tmp[4 * i + 3] = (uint8_t)(p >> 24);
            }
            fgl_rgba8_to_pixels(fmt, type, tmp, n, (uint8_t*)out + (size_t)y * stride + (size_t)x * (size_t)bpp);
        }
    }
}

/* copies from the read framebuffer into level 0 */
void APIENTRY glCopyTexSubImage2D(GLenum target, GLint level, GLint xo, GLint yo, GLint x, GLint y, GLsizei w, GLsizei h)
{
    FGL_CTX_OR_RETURN(c);
    fgl_tex* t = target == GL_TEXTURE_2D && FGL_BOUND_TEX(c) ? fgl_texture(c, FGL_BOUND_TEX(c), 0) : NULL;
    if (!t || !t->level0) {
        fgl_error(GL_INVALID_OPERATION);
        return;
    }
    if (level != 0) return; /* mipmaps come from level 0 */
    fgl_flush(c);
    fm_surface* src = fgl_read_color(c);
    if (!src) {
        fgl_error(GL_INVALID_OPERATION);
        return;
    }
    for (int r = 0; r < h; r++) {
        int sy = y + r, dy = yo + r;
        if (dy < 0 || dy >= t->level0->height) continue;
        uint32_t* d = fm_surface_row32(t->level0, dy);
        for (int i = 0; i < w; i++) {
            int sx = x + i, dx = xo + i;
            if (dx < 0 || dx >= t->level0->width) continue;
            d[dx] = sx >= 0 && sy >= 0 && sx < src->width && sy < src->height ? fm_surface_row32(src, sy)[sx] : 0;
        }
    }
    fm3d_texture_release(t->tex);
    t->tex = NULL;
}

void APIENTRY glCopyTexImage2D(GLenum target, GLint level, GLenum ifmt, GLint x, GLint y, GLsizei w, GLsizei h, GLint border)
{
    FGL_CTX_OR_RETURN(c);
    if (level != 0) return;
    glTexImage2D(target, level, (GLint)ifmt, w, h, border, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glCopyTexSubImage2D(target, level, 0, 0, x, y, w, h);
}

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
