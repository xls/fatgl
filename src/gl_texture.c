/*
 * fatgl - textures. Level 0 is kept as a straight (not premultiplied)
 * ARGB32 fatmap surface, like every fatgl color buffer; mipmaps are built by
 * fatmap from it when a mipmap filter is used (images given for levels > 0
 * are ignored for now). Layered textures (cube maps, 3D, arrays) stack their
 * layers top to bottom in that one surface: layer i is rows i * h .. i * h + h - 1.
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
    t->used = 1, t->name = name, t->tt = -1, t->layers = 1;
    t->min_filter = GL_NEAREST_MIPMAP_LINEAR, t->mag_filter = GL_LINEAR, t->wrap_s = t->wrap_t = t->wrap_r = GL_REPEAT;
    t->ifmt = GL_RGBA, t->min_lod = -1000, t->max_lod = 1000, t->max_level = 1000;
    t->compare_mode = GL_NONE, t->compare_func = GL_LEQUAL;
    t->swizzle[0] = GL_RED, t->swizzle[1] = GL_GREEN, t->swizzle[2] = GL_BLUE, t->swizzle[3] = GL_ALPHA;
    return t;
}

int fgl_tex_target(GLenum target)
{
    switch (target) {
    case GL_TEXTURE_2D: return FGL_TT_2D;
    case GL_TEXTURE_1D: return FGL_TT_1D;
    case GL_TEXTURE_3D: return FGL_TT_3D;
    case GL_TEXTURE_CUBE_MAP: return FGL_TT_CUBE;
    case GL_TEXTURE_RECTANGLE: return FGL_TT_RECT;
    case GL_TEXTURE_1D_ARRAY: return FGL_TT_1D_ARRAY;
    case GL_TEXTURE_2D_ARRAY: return FGL_TT_2D_ARRAY;
    default: return target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z ? FGL_TT_CUBE : -1;
    }
}

/* FGL_TT_* of a proxy target, -1 if not one */
static int fgl_proxy_target(GLenum target)
{
    switch (target) {
    case GL_PROXY_TEXTURE_2D: return FGL_TT_2D;
    case GL_PROXY_TEXTURE_1D: return FGL_TT_1D;
    case GL_PROXY_TEXTURE_3D: return FGL_TT_3D;
    case GL_PROXY_TEXTURE_CUBE_MAP: return FGL_TT_CUBE;
    case GL_PROXY_TEXTURE_RECTANGLE: return FGL_TT_RECT;
    case GL_PROXY_TEXTURE_1D_ARRAY: return FGL_TT_1D_ARRAY;
    case GL_PROXY_TEXTURE_2D_ARRAY: return FGL_TT_2D_ARRAY;
    default: return -1;
    }
}

int fgl_sampler_target(GLenum type)
{
    switch (type) {
    case GL_SAMPLER_2D: case GL_SAMPLER_2D_SHADOW: case GL_INT_SAMPLER_2D: case GL_UNSIGNED_INT_SAMPLER_2D: return FGL_TT_2D;
    case GL_SAMPLER_1D: case GL_SAMPLER_1D_SHADOW: case GL_INT_SAMPLER_1D: case GL_UNSIGNED_INT_SAMPLER_1D: return FGL_TT_1D;
    case GL_SAMPLER_3D: case GL_INT_SAMPLER_3D: case GL_UNSIGNED_INT_SAMPLER_3D: return FGL_TT_3D;
    case GL_SAMPLER_CUBE: case GL_SAMPLER_CUBE_SHADOW: case GL_INT_SAMPLER_CUBE: case GL_UNSIGNED_INT_SAMPLER_CUBE: return FGL_TT_CUBE;
    case GL_SAMPLER_2D_RECT: case GL_SAMPLER_2D_RECT_SHADOW: case GL_INT_SAMPLER_2D_RECT: case GL_UNSIGNED_INT_SAMPLER_2D_RECT:
        return FGL_TT_RECT;
    case GL_SAMPLER_1D_ARRAY: case GL_SAMPLER_1D_ARRAY_SHADOW: case GL_INT_SAMPLER_1D_ARRAY: case GL_UNSIGNED_INT_SAMPLER_1D_ARRAY:
        return FGL_TT_1D_ARRAY;
    case GL_SAMPLER_2D_ARRAY: case GL_SAMPLER_2D_ARRAY_SHADOW: case GL_INT_SAMPLER_2D_ARRAY: case GL_UNSIGNED_INT_SAMPLER_2D_ARRAY:
        return FGL_TT_2D_ARRAY;
    default: return -1;
    }
}

/* the texture bound to target (cube faces: the cube map) on the active unit */
static fgl_tex* fgl_bound(fgl_ctx* c, GLenum target, int create)
{
    int    k = fgl_tex_target(target);
    GLuint n = k >= 0 ? c->unit_bind[k][c->active_unit] : 0;
    return n ? fgl_texture(c, n, create) : NULL;
}

/* the cube face of a face target (0 for other targets) */
static int fgl_face(GLenum target)
{
    return target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z ? (int)(target - GL_TEXTURE_CUBE_MAP_POSITIVE_X)
                                                                                                 : 0;
}

/* rows of one layer of level 0 */
static int fgl_layer_h(const fgl_tex* t) { return t->level0 ? t->level0->height / (t->layers > 0 ? t->layers : 1) : 0; }

fm_surface* fgl_tex_layer(fgl_tex* t, int layer)
{
    if (!t->level0 || layer < 0 || layer >= t->layers) return NULL;
    if (t->layers == 1) return t->level0;
    if (!t->view && !(t->view = (fm_surface**)calloc((size_t)t->layers, sizeof(fm_surface*)))) return NULL;
    int h = fgl_layer_h(t);
    if (!t->view[layer]) t->view[layer] = fm_surface_sub(t->level0, 0, layer * h, t->level0->width, h);
    return t->view[layer];
}

void fgl_tex_free(fgl_ctx* c, fgl_tex* t)
{
    fgl_surface_gone(c, t->level0); /* a framebuffer may draw into them */
    fgl_surface_gone(c, t->depth);
    for (int i = 0; t->view && i < t->layers; i++) {
        fgl_surface_gone(c, t->view[i]);
        fm_surface_destroy(t->view[i]);
    }
    free(t->view);
    fm3d_texture_release(t->tex);
    fm_surface_destroy(t->level0);
    fm_surface_destroy(t->depth);
    t->view = NULL, t->tex = NULL, t->level0 = t->depth = NULL, t->rendered = 0, t->layers = 1;
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
        fgl_tex_free(c, t);
        memset(t, 0, sizeof(*t));
        for (int k = 0; k < FGL_TT_COUNT; k++)
            for (int u = 0; u < FGL_UNITS; u++)
                if (c->unit_bind[k][u] == names[i]) c->unit_bind[k][u] = 0;
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
    int k = target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z ? -1 : fgl_tex_target(target);
    if (k < 0) {
        if (target == GL_TEXTURE_BUFFER || target == GL_TEXTURE_2D_MULTISAMPLE || target == GL_TEXTURE_2D_MULTISAMPLE_ARRAY)
            fgl_unimplemented("glBindTexture (buffer / multisample textures)");
        else fgl_error(GL_INVALID_ENUM);
        return;
    }
    if (name) {
        fgl_tex* t = fgl_texture(c, name, 1);
        if (t && t->tt < 0) t->tt = k;
        else if (t && t->tt != k) { /* a texture keeps its first target */
            fgl_error(GL_INVALID_OPERATION);
            return;
        }
    }
    c->unit_bind[k][c->active_unit] = name;
}

/* the channels an internal format keeps (the base internal format): what
 * a texture of it samples as, whatever the client data had */
enum { FGL_BASE_RGBA = 0, FGL_BASE_ALPHA, FGL_BASE_LUM, FGL_BASE_LUM_ALPHA, FGL_BASE_INTENSITY, FGL_BASE_RED, FGL_BASE_RG, FGL_BASE_RGB };
static int fgl_base_format(GLint ifmt)
{
    switch (ifmt) {
    case 0x1906: case 0x803B: case 0x803C: case 0x803D: case 0x803E: case 0x881C: case 0x8816: /* GL_ALPHA, ALPHA4 .. 16, 16F, 32F */
        return FGL_BASE_ALPHA;
    case 1: case 0x1909: case 0x803F: case 0x8040: case 0x8041: case 0x8042: case 0x881E: case 0x8818: case 0x8C46: case 0x8C47:
        return FGL_BASE_LUM; /* GL_LUMINANCE, 4 .. 16, 16F, 32F, SLUMINANCE(8) */
    case 2: case 0x190A: case 0x8043: case 0x8044: case 0x8045: case 0x8046: case 0x8047: case 0x8048: case 0x881F: case 0x8819:
    case 0x8C44: case 0x8C45:
        return FGL_BASE_LUM_ALPHA; /* GL_LUMINANCE_ALPHA and its sized forms */
    case 0x8049: case 0x804A: case 0x804B: case 0x804C: case 0x804D: case 0x881D: case 0x8817:
        return FGL_BASE_INTENSITY; /* GL_INTENSITY, 4 .. 16, 16F, 32F */
    case 0x1903: case 0x8229: case 0x822A: case 0x822D: case 0x822E: case 0x8F94: case 0x8225: case 0x8231: case 0x8232:
        return FGL_BASE_RED; /* GL_RED, R8, R16, R16F, R32F, R8_SNORM, COMPRESSED_RED, R8I, R8UI */
    case 0x8227: case 0x822B: case 0x822C: case 0x822F: case 0x8230: case 0x8F95: case 0x8226:
        return FGL_BASE_RG; /* GL_RG, RG8, RG16, RG16F, RG32F, RG8_SNORM, COMPRESSED_RG */
    case 3: case 0x1907: case 0x2A10: case 0x804F: case 0x8050: case 0x8051: case 0x8052: case 0x8053: case 0x8054: case 0x8D62:
    case 0x8C40: case 0x8C41: case 0x881B: case 0x8815: case 0x8F96: case 0x84ED: case 0x8C3A: case 0x8C3D:
        return FGL_BASE_RGB; /* GL_RGB and its sized / float / sRGB / packed forms */
    default: return FGL_BASE_RGBA;
    }
}

/* an RGBA8 pixel as the base internal format keeps it */
static uint32_t fgl_base_pixel(int base, const uint8_t* p)
{
    switch (base) {
    case FGL_BASE_ALPHA: return FM_RGBA(0, 0, 0, p[3]);
    case FGL_BASE_LUM: return FM_RGBA(p[0], p[0], p[0], 255);
    case FGL_BASE_LUM_ALPHA: return FM_RGBA(p[0], p[0], p[0], p[3]);
    case FGL_BASE_INTENSITY: return FM_RGBA(p[0], p[0], p[0], p[0]);
    case FGL_BASE_RED: return FM_RGBA(p[0], 0, 0, 255);
    case FGL_BASE_RG: return FM_RGBA(p[0], p[1], 0, 255);
    case FGL_BASE_RGB: return FM_RGBA(p[0], p[1], p[2], 255);
    default: return FM_RGBA(p[0], p[1], p[2], p[3]);
    }
}

/* copy client pixels (unpack alignment / row length / skips, pixel unpack
 * buffer) into rows of s, as straight RGBA8 the internal format ifmt keeps:
 * d images of w x h, image z at rows y0 + z * ystep */
static void fgl_upload(fgl_ctx* c, fm_surface* s, int x0, int y0, int w, int h, int d, int ystep, GLint ifmt, GLenum fmt, GLenum type,
                       const void* pixels)
{
    int bf = fgl_base_format(ifmt);
    pixels  = fgl_unpack_ptr(c, pixels);
    int bpp = fgl_pixel_bytes(fmt, type);
    if (!pixels || !bpp) {
        if (!bpp) fgl_unimplemented("glTexImage / glTexSubImage (this format / type)");
        return;
    }
    c->cnt.upload_bytes += (uint64_t)w * (uint64_t)h * (uint64_t)d * (uint64_t)bpp;
    int    rowpx  = c->unpack_row > 0 ? c->unpack_row : w;
    size_t stride = (size_t)rowpx * (size_t)bpp;
    stride        = (stride + (size_t)c->unpack_align - 1) / (size_t)c->unpack_align * (size_t)c->unpack_align;
    const uint8_t* base = (const uint8_t*)pixels + (size_t)c->unpack_skip_rows * stride + (size_t)c->unpack_skip_pixels * (size_t)bpp;
    for (int z = 0; z < d; z++)
        for (int y = 0; y < h; y++) {
            uint32_t*      o   = fm_surface_row32(s, y0 + z * ystep + y) + x0;
            const uint8_t* row = base + ((size_t)z * (size_t)h + (size_t)y) * stride;
            uint8_t        tmp[4 * 256];
            for (int x = 0; x < w; x += 256) {
                int n = w - x < 256 ? w - x : 256;
                fgl_pixels_to_rgba8(fmt, type, row + (size_t)x * (size_t)bpp, n, tmp);
                for (int i = 0; i < n; i++) o[x + i] = fgl_base_pixel(bf, tmp + 4 * i);
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

static struct {
    int    w, h, d;
    GLenum ifmt;
} g_proxy[FGL_TT_COUNT]; /* glTexImage* of proxy targets: what glGetTexLevelParameter reports */

/* glTexImage1D / 2D / 3D: level 0 of the texture bound to target (cube faces: one face) */
static void fgl_tex_image(fgl_ctx* c, GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLsizei d, GLint border, GLenum fmt,
                          GLenum type, const void* pixels)
{
    int pk = fgl_proxy_target(target);
    if (pk >= 0) { /* everything up to the limits fits */
        int ok = w >= 0 && h >= 0 && d >= 0 && w <= 8192 && h <= 8192 && d <= 2048 && border == 0;
        g_proxy[pk].w = ok ? w : 0, g_proxy[pk].h = ok ? h : 0, g_proxy[pk].d = ok ? d : 0, g_proxy[pk].ifmt = ok ? (GLenum)ifmt : 0;
        return;
    }
    int k = fgl_tex_target(target);
    if (k < 0 || target == GL_TEXTURE_CUBE_MAP) {
        fgl_error(GL_INVALID_ENUM);
        return;
    }
    if (w < 0 || h < 0 || d < 0 || border != 0 || level < 0) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    if (level > 0) return; /* mipmaps come from level 0 */
    fgl_tex* t = fgl_bound(c, target, 1);
    if (!t) {
        fgl_error(GL_INVALID_OPERATION);
        return;
    }
    int layers = k == FGL_TT_CUBE ? 6 : (k == FGL_TT_3D || k == FGL_TT_2D_ARRAY ? d : (k == FGL_TT_1D_ARRAY ? h : 1));
    int lh     = k == FGL_TT_1D_ARRAY ? 1 : h; /* rows per layer */
    int depth  = fmt == GL_DEPTH_COMPONENT || fmt == GL_DEPTH_STENCIL;
    fgl_flush(c);
    c->tgt_color = NULL;
    /* a cube face of the size of the others: the other faces stay */
    int keep = k == FGL_TT_CUBE && !depth && t->level0 && t->layers == 6 && t->level0->width == w && t->level0->height == 6 * h;
    if (keep) {
        fm3d_texture_release(t->tex);
        t->tex = NULL;
    } else {
        fgl_tex_free(c, t);
    }
    t->ifmt = (GLenum)ifmt;
    if (w <= 0 || lh <= 0 || layers <= 0) return;
    if (depth) { /* a depth (stencil) texture: a fatmap depth surface */
        if (layers != 1) {
            fgl_unimplemented("glTexImage (layered depth textures)");
            return;
        }
        fm_format f = ifmt == GL_DEPTH_COMPONENT16 ? FM_FORMAT_D16
                      : (ifmt == GL_DEPTH_COMPONENT32F || ifmt == GL_DEPTH_COMPONENT32) ? FM_FORMAT_D32F : FM_FORMAT_D24S8;
        t->depth = fm_surface_create(w, h, f);
        pixels   = fgl_unpack_ptr(c, pixels);
        if (t->depth && pixels && type == GL_FLOAT && fmt == GL_DEPTH_COMPONENT && f == FM_FORMAT_D32F)
            for (int y = 0; y < h; y++) memcpy(fm_surface_rowf(t->depth, y), (const float*)pixels + (size_t)y * (size_t)w, (size_t)w * 4);
        else if (pixels)
            fgl_unimplemented("glTexImage2D (depth texture data other than GL_FLOAT into GL_DEPTH_COMPONENT32F)");
        return;
    }
    if (!keep) {
        t->level0 = fm_surface_create(w, lh * layers, FM_FORMAT_ARGB32);
        t->layers = layers;
        if (!t->level0) {
            t->layers = 1;
            fgl_error(GL_OUT_OF_MEMORY);
            return;
        }
    }
    if (!pixels) return;
    if (k == FGL_TT_CUBE) fgl_upload(c, t->level0, 0, fgl_face(target) * h, w, h, 1, 0, ifmt, fmt, type, pixels);
    else if (k == FGL_TT_3D || k == FGL_TT_2D_ARRAY) fgl_upload(c, t->level0, 0, 0, w, h, d, h, ifmt, fmt, type, pixels);
    else fgl_upload(c, t->level0, 0, 0, w, k == FGL_TT_1D ? 1 : h, 1, 0, ifmt, fmt, type, pixels);
}

void APIENTRY glTexImage1D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLint border, GLenum fmt, GLenum type, const void* pixels)
{
    FGL_CTX_OR_RETURN(c);
    fgl_tex_image(c, target, level, ifmt, w, 1, 1, border, fmt, type, pixels);
}

void APIENTRY glTexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border, GLenum fmt, GLenum type,
                           const void* pixels)
{
    FGL_CTX_OR_RETURN(c);
    fgl_tex_image(c, target, level, ifmt, w, h, 1, border, fmt, type, pixels);
}

void APIENTRY glTexImage3D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLsizei d, GLint border, GLenum fmt, GLenum type,
                           const void* pixels)
{
    FGL_CTX_OR_RETURN(c);
    if (target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY && target != GL_PROXY_TEXTURE_3D && target != GL_PROXY_TEXTURE_2D_ARRAY) {
        if (target == GL_TEXTURE_CUBE_MAP_ARRAY) fgl_unimplemented("glTexImage3D (cube map arrays)");
        else fgl_error(GL_INVALID_ENUM);
        return;
    }
    fgl_tex_image(c, target, level, ifmt, w, h, d, border, fmt, type, pixels);
}

/* the texture of a glTexSubImage / glCopyTexSubImage target with a level 0
 * region x, y, z (layer / cube face) + w x h x d inside it, as rows of level0
 * (*row0: the first row of layer z); NULL (with the GL error) if not */
static fgl_tex* fgl_sub_target(fgl_ctx* c, GLenum target, GLint level, int x, int y, int z, int w, int h, int d, int* row0)
{
    int      k = fgl_tex_target(target);
    fgl_tex* t = k >= 0 && target != GL_TEXTURE_CUBE_MAP ? fgl_bound(c, target, 0) : NULL;
    if (level > 0) return NULL; /* mipmaps come from level 0 */
    if (!t || !t->level0) {
        fgl_error(k < 0 ? GL_INVALID_ENUM : GL_INVALID_OPERATION);
        return NULL;
    }
    int lh = fgl_layer_h(t);
    if (k == FGL_TT_CUBE) z = fgl_face(target);
    if (k == FGL_TT_1D_ARRAY) z = y, y = 0, d = h, h = 1; /* the rows are the layers */
    if (x < 0 || y < 0 || z < 0 || w < 0 || h < 0 || d < 0 || x + w > t->level0->width || y + h > lh || z + d > t->layers) {
        fgl_error(GL_INVALID_VALUE);
        return NULL;
    }
    *row0 = z * lh + y;
    return t;
}

static void fgl_tex_sub(fgl_ctx* c, GLenum target, GLint level, GLint x, GLint y, GLint z, GLsizei w, GLsizei h, GLsizei d, GLenum fmt,
                        GLenum type, const void* pixels)
{
    int      row0;
    fgl_tex* t = fgl_sub_target(c, target, level, x, y, z, w, h, d, &row0);
    if (!t) return;
    fgl_flush(c);
    fgl_upload(c, t->level0, x, row0, w, h, d, fgl_layer_h(t), (GLint)t->ifmt, fmt, type, pixels);
    fm3d_texture_release(t->tex); /* rebuilt (with its mipmaps) on the next use */
    t->tex = NULL;
}

void APIENTRY glTexSubImage1D(GLenum target, GLint level, GLint x, GLsizei w, GLenum fmt, GLenum type, const void* pixels)
{
    FGL_CTX_OR_RETURN(c);
    fgl_tex_sub(c, target, level, x, 0, 0, w, 1, 1, fmt, type, pixels);
}

void APIENTRY glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type,
                              const void* pixels)
{
    FGL_CTX_OR_RETURN(c);
    fgl_tex_sub(c, target, level, x, y, 0, w, h, 1, fmt, type, pixels);
}

void APIENTRY glTexSubImage3D(GLenum target, GLint level, GLint x, GLint y, GLint z, GLsizei w, GLsizei h, GLsizei d, GLenum fmt,
                              GLenum type, const void* pixels)
{
    FGL_CTX_OR_RETURN(c);
    fgl_tex_sub(c, target, level, x, y, z, w, h, d, fmt, type, pixels);
}

/* compressed images are decoded to RGBA8 (S3TC / RGTC; level 0, mipmaps
 * come from it); d images of `size / d` bytes each */
static void fgl_compressed_sub(fgl_ctx* c, GLenum target, GLint level, GLint x, GLint y, GLint z, GLsizei w, GLsizei h, GLsizei d,
                               GLenum fmt, GLsizei size, const void* data)
{
    int      row0;
    fgl_tex* t = fgl_sub_target(c, target, level, x, y, z, w, h, d, &row0);
    if (!t) return;
    if (!fgl_compressed_block_bytes(fmt)) {
        fgl_error(GL_INVALID_ENUM);
        return;
    }
    data          = fgl_unpack_ptr(c, data);
    uint8_t* rgba = data && d > 0 ? (uint8_t*)malloc((size_t)w * (size_t)h * 4) : NULL;
    if (!rgba) return;
    fgl_flush(c);
    size_t each = (size_t)size / (size_t)d;
    for (int i = 0; i < d; i++) {
        if (fgl_decompress(fmt, (const uint8_t*)data + (size_t)i * each, each, w, h, rgba))
            fgl_put_rgba8(t->level0, x, row0 + i * fgl_layer_h(t), w, h, rgba);
        else fgl_error(GL_INVALID_VALUE);
    }
    free(rgba);
    fm3d_texture_release(t->tex);
    t->tex = NULL;
}

void APIENTRY glCompressedTexImage2D(GLenum target, GLint level, GLenum ifmt, GLsizei w, GLsizei h, GLint border, GLsizei size,
                                     const void* data)
{
    FGL_CTX_OR_RETURN(c);
    if (!fgl_compressed_block_bytes(ifmt)) {
        fgl_error(GL_INVALID_ENUM);
        return;
    }
    if (level > 0) return;
    fgl_tex_image(c, target, level, (GLint)ifmt, w, h, 1, border, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    if (data && fgl_proxy_target(target) < 0) fgl_compressed_sub(c, target, level, 0, 0, 0, w, h, 1, ifmt, size, data);
}

void APIENTRY glCompressedTexImage3D(GLenum target, GLint level, GLenum ifmt, GLsizei w, GLsizei h, GLsizei d, GLint border, GLsizei size,
                                     const void* data)
{
    FGL_CTX_OR_RETURN(c);
    if (!fgl_compressed_block_bytes(ifmt)) {
        fgl_error(GL_INVALID_ENUM);
        return;
    }
    if (level > 0) return;
    glTexImage3D(target, level, (GLint)ifmt, w, h, d, border, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    if (data && fgl_proxy_target(target) < 0) fgl_compressed_sub(c, target, level, 0, 0, 0, w, h, d, ifmt, size, data);
}

void APIENTRY glCompressedTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLsizei size,
                                        const void* data)
{
    FGL_CTX_OR_RETURN(c);
    fgl_compressed_sub(c, target, level, x, y, 0, w, h, 1, fmt, size, data);
}

void APIENTRY glCompressedTexSubImage3D(GLenum target, GLint level, GLint x, GLint y, GLint z, GLsizei w, GLsizei h, GLsizei d, GLenum fmt,
                                        GLsizei size, const void* data)
{
    FGL_CTX_OR_RETURN(c);
    fgl_compressed_sub(c, target, level, x, y, z, w, h, d, fmt, size, data);
}

void APIENTRY glTexParameteri(GLenum target, GLenum p, GLint v)
{
    FGL_CTX_OR_RETURN(c);
    fgl_tex* t = fgl_bound(c, target, 1);
    if (!t) return;
    switch (p) {
    case GL_TEXTURE_MIN_FILTER: t->min_filter = (GLenum)v; break;
    case GL_TEXTURE_MAG_FILTER: t->mag_filter = (GLenum)v; break;
    case GL_TEXTURE_WRAP_S: t->wrap_s = (GLenum)v; break;
    case GL_TEXTURE_WRAP_T: t->wrap_t = (GLenum)v; break;
    case GL_TEXTURE_WRAP_R: t->wrap_r = (GLenum)v; break;
    case GL_TEXTURE_BASE_LEVEL: t->base_level = v; break;
    case GL_TEXTURE_MAX_LEVEL: t->max_level = v; break;
    case GL_TEXTURE_COMPARE_MODE: t->compare_mode = (GLenum)v; break;
    case GL_TEXTURE_COMPARE_FUNC: t->compare_func = (GLenum)v; break;
    case GL_TEXTURE_MIN_LOD: t->min_lod = (float)v; break;
    case GL_TEXTURE_MAX_LOD: t->max_lod = (float)v; break;
    case GL_TEXTURE_SWIZZLE_R: case GL_TEXTURE_SWIZZLE_G: case GL_TEXTURE_SWIZZLE_B: case GL_TEXTURE_SWIZZLE_A:
        if (t->swizzle[p - GL_TEXTURE_SWIZZLE_R] != (GLenum)v) {
            t->swizzle[p - GL_TEXTURE_SWIZZLE_R] = (GLenum)v;
            fm3d_texture_release(t->tex); /* rebuilt with the new swizzle */
            t->tex = NULL;
        }
        break;
    default: break; /* anisotropy, generate mipmap: accepted */
    }
}
void APIENTRY glTexParameterf(GLenum target, GLenum p, GLfloat v)
{
    FGL_CTX_OR_RETURN(c);
    fgl_tex* t = fgl_bound(c, target, 1);
    if (t && p == GL_TEXTURE_MIN_LOD) t->min_lod = v;
    else if (t && p == GL_TEXTURE_MAX_LOD) t->max_lod = v;
    else if (t && p == GL_TEXTURE_LOD_BIAS) t->lod_bias = v;
    else glTexParameteri(target, p, (GLint)v);
}
void APIENTRY glTexParameteriv(GLenum target, GLenum p, const GLint* v)
{
    if (p == GL_TEXTURE_SWIZZLE_RGBA) {
        for (int k = 0; k < 4; k++) glTexParameteri(target, GL_TEXTURE_SWIZZLE_R + (GLenum)k, v[k]);
        return;
    }
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
    fgl_tex* t = fgl_bound(c, target, 1);
    if (t && p == GL_TEXTURE_BORDER_COLOR) memcpy(t->border, v, 16);
    else glTexParameterf(target, p, v[0]);
}
void APIENTRY glTexParameterIiv(GLenum target, GLenum p, const GLint* v) { glTexParameteri(target, p, v[0]); }
void APIENTRY glTexParameterIuiv(GLenum target, GLenum p, const GLuint* v) { glTexParameteri(target, p, (GLint)v[0]); }

static int fgl_tex_query(GLenum target, GLenum p, float* o)
{
    fgl_ctx* c = fgl_cur();
    fgl_tex* t = c ? fgl_bound(c, target, 1) : NULL;
    if (!t) {
        if (c) fgl_error(fgl_tex_target(target) >= 0 ? GL_INVALID_OPERATION : GL_INVALID_ENUM);
        return 0;
    }
    switch (p) {
    case GL_TEXTURE_MIN_FILTER: o[0] = (float)t->min_filter; return 1;
    case GL_TEXTURE_MAG_FILTER: o[0] = (float)t->mag_filter; return 1;
    case GL_TEXTURE_WRAP_S: o[0] = (float)t->wrap_s; return 1;
    case GL_TEXTURE_WRAP_T: o[0] = (float)t->wrap_t; return 1;
    case GL_TEXTURE_WRAP_R: o[0] = (float)t->wrap_r; return 1;
    case GL_TEXTURE_BASE_LEVEL: o[0] = (float)t->base_level; return 1;
    case GL_TEXTURE_MAX_LEVEL: o[0] = (float)t->max_level; return 1;
    case GL_TEXTURE_MIN_LOD: o[0] = t->min_lod; return 1;
    case GL_TEXTURE_MAX_LOD: o[0] = t->max_lod; return 1;
    case GL_TEXTURE_LOD_BIAS: o[0] = t->lod_bias; return 1;
    case GL_TEXTURE_COMPARE_MODE: o[0] = (float)t->compare_mode; return 1;
    case GL_TEXTURE_COMPARE_FUNC: o[0] = (float)t->compare_func; return 1;
    case GL_TEXTURE_BORDER_COLOR: memcpy(o, t->border, 16); return 4;
    case GL_TEXTURE_IMMUTABLE_FORMAT: o[0] = 0; return 1;
    case GL_TEXTURE_SWIZZLE_R: case GL_TEXTURE_SWIZZLE_G: case GL_TEXTURE_SWIZZLE_B: case GL_TEXTURE_SWIZZLE_A:
        o[0] = (float)t->swizzle[p - GL_TEXTURE_SWIZZLE_R];
        return 1;
    case GL_TEXTURE_SWIZZLE_RGBA: for (int k = 0; k < 4; k++) o[k] = (float)t->swizzle[k]; return 4;
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
    int pk = fgl_proxy_target(target);
    if (pk >= 0) { /* what the last proxy glTexImage gave */
        int ok = level == 0 && g_proxy[pk].w > 0;
        switch (p) {
        case GL_TEXTURE_WIDTH: *v = ok ? g_proxy[pk].w : 0; break;
        case GL_TEXTURE_HEIGHT: *v = ok ? g_proxy[pk].h : 0; break;
        case GL_TEXTURE_DEPTH: *v = ok ? g_proxy[pk].d : 0; break;
        case GL_TEXTURE_INTERNAL_FORMAT: *v = ok ? (GLint)g_proxy[pk].ifmt : 0; break;
        case GL_TEXTURE_RED_SIZE: case GL_TEXTURE_GREEN_SIZE: case GL_TEXTURE_BLUE_SIZE: case GL_TEXTURE_ALPHA_SIZE: *v = ok ? 8 : 0; break;
        default: *v = 0; break;
        }
        return;
    }
    int      k = fgl_tex_target(target);
    fgl_tex* t = k >= 0 && target != GL_TEXTURE_CUBE_MAP ? fgl_bound(c, target, 1) : NULL;
    if (!t) {
        if (k < 0 || target == GL_TEXTURE_CUBE_MAP) fgl_error(GL_INVALID_ENUM);
        else *v = 0;
        return;
    }
    const fm_surface* s = t->level0 ? t->level0 : t->depth;
    int               lh = t->level0 ? fgl_layer_h(t) : (s ? s->height : 0);
    int w = s ? s->width >> level : 0, h = k == FGL_TT_1D_ARRAY ? t->layers : lh >> level;
    int dd = s ? (k == FGL_TT_3D || k == FGL_TT_2D_ARRAY ? t->layers : 1) : 0;
    if (s && level > 0) w = w < 1 ? 1 : w, h = h < 1 ? 1 : h;
    if (k == FGL_TT_3D && level > 0) dd = dd >> level < 1 ? 1 : dd >> level;
    int dep = t->depth != NULL, d24 = dep && t->depth->format == FM_FORMAT_D24S8;
    switch (p) {
    case GL_TEXTURE_WIDTH: *v = w; break;
    case GL_TEXTURE_HEIGHT: *v = s ? h : 0; break;
    case GL_TEXTURE_DEPTH: *v = dd; break;
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

/* level 0 back to client memory (pack alignment): RGBA / BGRA / RGB / BGR
 * bytes or floats; layered textures: every layer, cube faces: that face */
void APIENTRY glGetTexImage(GLenum target, GLint level, GLenum fmt, GLenum type, void* out)
{
    FGL_CTX_OR_RETURN(c);
    fgl_tex* t = target != GL_TEXTURE_CUBE_MAP ? fgl_bound(c, target, 0) : NULL;
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
    int               cube   = fgl_tex_target(target) == FGL_TT_CUBE;
    int               y0     = cube ? fgl_face(target) * fgl_layer_h(t) : 0, rows = cube ? fgl_layer_h(t) : s->height;
    size_t            stride = ((size_t)s->width * (size_t)bpp + (size_t)c->pack_align - 1) / (size_t)c->pack_align * (size_t)c->pack_align;
    for (int y = 0; y < rows; y++) {
        const uint32_t* r = fm_surface_row32(s, y0 + y);
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

/* copies from the read framebuffer into level 0 (a layer / face) */
static void fgl_copy_sub(fgl_ctx* c, GLenum target, GLint level, GLint xo, GLint yo, GLint z, GLint x, GLint y, GLsizei w, GLsizei h)
{
    int      row0;
    fgl_tex* t = fgl_sub_target(c, target, level, xo, yo, z, w, h, 1, &row0);
    if (!t) return;
    fgl_flush(c);
    fm_surface* src = fgl_read_color(c);
    if (!src) {
        fgl_error(GL_INVALID_OPERATION);
        return;
    }
    for (int r = 0; r < h; r++) {
        int       sy = y + r;
        uint32_t* d  = fm_surface_row32(t->level0, row0 + r);
        for (int i = 0; i < w; i++) {
            int sx    = x + i;
            d[xo + i] = sx >= 0 && sy >= 0 && sx < src->width && sy < src->height ? fm_surface_row32(src, sy)[sx] : 0;
        }
    }
    fm3d_texture_release(t->tex);
    t->tex = NULL;
}

void APIENTRY glCopyTexSubImage1D(GLenum target, GLint level, GLint xo, GLint x, GLint y, GLsizei w)
{
    FGL_CTX_OR_RETURN(c);
    fgl_copy_sub(c, target, level, xo, 0, 0, x, y, w, 1);
}

void APIENTRY glCopyTexSubImage2D(GLenum target, GLint level, GLint xo, GLint yo, GLint x, GLint y, GLsizei w, GLsizei h)
{
    FGL_CTX_OR_RETURN(c);
    fgl_copy_sub(c, target, level, xo, yo, 0, x, y, w, h);
}

void APIENTRY glCopyTexSubImage3D(GLenum target, GLint level, GLint xo, GLint yo, GLint zo, GLint x, GLint y, GLsizei w, GLsizei h)
{
    FGL_CTX_OR_RETURN(c);
    fgl_copy_sub(c, target, level, xo, yo, zo, x, y, w, h);
}

void APIENTRY glCopyTexImage1D(GLenum target, GLint level, GLenum ifmt, GLint x, GLint y, GLsizei w, GLint border)
{
    FGL_CTX_OR_RETURN(c);
    if (level != 0) return;
    fgl_tex_image(c, target, level, (GLint)ifmt, w, 1, 1, border, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    fgl_copy_sub(c, target, level, 0, 0, 0, x, y, w, 1);
}

void APIENTRY glCopyTexImage2D(GLenum target, GLint level, GLenum ifmt, GLint x, GLint y, GLsizei w, GLsizei h, GLint border)
{
    FGL_CTX_OR_RETURN(c);
    if (level != 0) return;
    fgl_tex_image(c, target, level, (GLint)ifmt, w, h, 1, border, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    fgl_copy_sub(c, target, level, 0, 0, 0, x, y, w, h);
}

void APIENTRY glTexEnvi(GLenum target, GLenum p, GLint v)
{
    FGL_CTX_OR_RETURN(c);
    if (target == GL_TEXTURE_ENV && p == GL_TEXTURE_ENV_MODE) {
        c->tex_envs[c->active_unit] = (GLenum)v;
        if (c->active_unit == 0) c->tex_env = (GLenum)v;
    }
}
void APIENTRY glTexEnvf(GLenum target, GLenum p, GLfloat v) { glTexEnvi(target, p, (GLint)v); }
void APIENTRY glTexEnvfv(GLenum target, GLenum p, const GLfloat* v) { glTexEnvi(target, p, (GLint)v[0]); }
void APIENTRY glTexEnviv(GLenum target, GLenum p, const GLint* v) { glTexEnvi(target, p, v[0]); }

void APIENTRY glGenerateMipmap(GLenum target)
{
    FGL_CTX_OR_RETURN(c);
    fgl_tex* t = fgl_bound(c, target, 0);
    if (!t) return;
    fm3d_texture_release(t->tex); /* rebuilt with mipmaps (from level 0) on the next use */
    t->tex = NULL;
}
