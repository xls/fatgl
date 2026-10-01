/*
 * fatgl - client pixel data: every format / type combination of GL 3.3
 * (packed types, half floats, signed / integer formats) to and from the
 * straight RGBA8 that fatgl's textures and framebuffers hold, S3TC (DXT1 /
 * 3 / 5) and RGTC (BC4 / BC5) decompression, pixel buffer objects.
 */
#include "fgl.h"

/* components of a client format in memory order: 0 r, 1 g, 2 b, 3 a, 4
 * luminance, 5 depth; returns the count (0: unknown) and whether values
 * are unnormalized integers */
static int fgl_fmt_comps(GLenum fmt, int* comp, int* integer)
{
    *integer = 0;
    switch (fmt) {
    case GL_RED_INTEGER: *integer = 1; /* fall through */
    case GL_RED: comp[0] = 0; return 1;
    case GL_GREEN_INTEGER: *integer = 1; /* fall through */
    case GL_GREEN: comp[0] = 1; return 1;
    case GL_BLUE_INTEGER: *integer = 1; /* fall through */
    case GL_BLUE: comp[0] = 2; return 1;
    case GL_ALPHA: comp[0] = 3; return 1;
    case GL_LUMINANCE: comp[0] = 4; return 1;
    case GL_LUMINANCE_ALPHA: comp[0] = 4, comp[1] = 3; return 2;
    case GL_DEPTH_COMPONENT: case GL_STENCIL_INDEX: comp[0] = 5; return 1;
    case GL_RG_INTEGER: *integer = 1; /* fall through */
    case GL_RG: comp[0] = 0, comp[1] = 1; return 2;
    case GL_RGB_INTEGER: *integer = 1; /* fall through */
    case GL_RGB: comp[0] = 0, comp[1] = 1, comp[2] = 2; return 3;
    case GL_BGR_INTEGER: *integer = 1; /* fall through */
    case GL_BGR: comp[0] = 2, comp[1] = 1, comp[2] = 0; return 3;
    case GL_RGBA_INTEGER: *integer = 1; /* fall through */
    case GL_RGBA: comp[0] = 0, comp[1] = 1, comp[2] = 2, comp[3] = 3; return 4;
    case GL_BGRA_INTEGER: *integer = 1; /* fall through */
    case GL_BGRA: comp[0] = 2, comp[1] = 1, comp[2] = 0, comp[3] = 3; return 4;
    default: return 0;
    }
}

/* packed types: bit widths from the most significant field down (non REV:
 * the first component is the most significant; REV: the least) */
typedef struct fgl_packed {
    GLenum type;
    int    bytes, n, rev;
    int    bits[4];
} fgl_packed;
static const fgl_packed g_packed[] = {
    { GL_UNSIGNED_BYTE_3_3_2, 1, 3, 0, { 3, 3, 2, 0 } },
    { GL_UNSIGNED_BYTE_2_3_3_REV, 1, 3, 1, { 3, 3, 2, 0 } },
    { GL_UNSIGNED_SHORT_5_6_5, 2, 3, 0, { 5, 6, 5, 0 } },
    { GL_UNSIGNED_SHORT_5_6_5_REV, 2, 3, 1, { 5, 6, 5, 0 } },
    { GL_UNSIGNED_SHORT_4_4_4_4, 2, 4, 0, { 4, 4, 4, 4 } },
    { GL_UNSIGNED_SHORT_4_4_4_4_REV, 2, 4, 1, { 4, 4, 4, 4 } },
    { GL_UNSIGNED_SHORT_5_5_5_1, 2, 4, 0, { 5, 5, 5, 1 } },
    { GL_UNSIGNED_SHORT_1_5_5_5_REV, 2, 4, 1, { 5, 5, 5, 1 } },
    { GL_UNSIGNED_INT_8_8_8_8, 4, 4, 0, { 8, 8, 8, 8 } },
    { GL_UNSIGNED_INT_8_8_8_8_REV, 4, 4, 1, { 8, 8, 8, 8 } },
    { GL_UNSIGNED_INT_10_10_10_2, 4, 4, 0, { 10, 10, 10, 2 } },
    { GL_UNSIGNED_INT_2_10_10_10_REV, 4, 4, 1, { 10, 10, 10, 2 } },
};

static const fgl_packed* fgl_packed_of(GLenum type)
{
    for (size_t i = 0; i < sizeof(g_packed) / sizeof(g_packed[0]); i++)
        if (g_packed[i].type == type) return &g_packed[i];
    return NULL;
}

static int fgl_type_size(GLenum type)
{
    switch (type) {
    case GL_UNSIGNED_BYTE: case GL_BYTE: return 1;
    case GL_UNSIGNED_SHORT: case GL_SHORT: case GL_HALF_FLOAT: return 2;
    case GL_UNSIGNED_INT: case GL_INT: case GL_FLOAT: return 4;
    default: return 0;
    }
}

int fgl_pixel_bytes(GLenum fmt, GLenum type)
{
    int comp[4], integer;
    int n = fgl_fmt_comps(fmt, comp, &integer);
    if (!n) return 0;
    if (type == GL_UNSIGNED_INT_10F_11F_11F_REV || type == GL_UNSIGNED_INT_5_9_9_9_REV || type == GL_UNSIGNED_INT_24_8) return 4;
    if (type == GL_FLOAT_32_UNSIGNED_INT_24_8_REV) return 8;
    const fgl_packed* pk = fgl_packed_of(type);
    if (pk) return pk->bytes;
    return n * fgl_type_size(type);
}

static float fgl_half2f(uint16_t h)
{
    uint32_t s = (uint32_t)(h >> 15) << 31, e = (h >> 10) & 31, m = h & 1023, u;
    if (e == 0) {
        if (!m) u = s;
        else { /* denormal */
            e = 113;
            while (!(m & 1024)) m <<= 1, e--;
            u = s | (e << 23) | ((m & 1023) << 13);
        }
    } else if (e == 31) {
        u = s | 0x7f800000u | (m << 13);
    } else {
        u = s | ((e + 112) << 23) | (m << 13);
    }
    float f;
    memcpy(&f, &u, 4);
    return f;
}

/* small unsigned floats of R11F_G11F_B10F (mantissa bits mb, 5 bit exponent) */
static float fgl_uf(uint32_t v, int mb)
{
    uint32_t e = v >> mb, m = v & ((1u << mb) - 1u);
    if (e == 0) return (float)m / (float)(1u << mb) * (1.0f / 16384.0f);
    if (e == 31) return 65504.0f;
    return (1.0f + (float)m / (float)(1u << mb)) * ldexpf(1.0f, (int)e - 15);
}

static uint8_t fgl_u8(float v) { return (uint8_t)(v <= 0 ? 0 : (v >= 1 ? 255 : v * 255.0f + 0.5f)); }

/* n pixels of client data -> straight RGBA8 */
void fgl_pixels_to_rgba8(GLenum fmt, GLenum type, const uint8_t* src, int n, uint8_t* out)
{
    int comp[4], integer;
    int nc = fgl_fmt_comps(fmt, comp, &integer);
    int ps = fgl_pixel_bytes(fmt, type);
    const fgl_packed* pk = fgl_packed_of(type);
    for (int i = 0; i < n; i++) {
        const uint8_t* p = src + (size_t)i * (size_t)ps;
        float          v[4] = { 0, 0, 0, 0 }, rgba[4] = { 0, 0, 0, 1 };
        if (type == GL_UNSIGNED_INT_10F_11F_11F_REV) {
            uint32_t w;
            memcpy(&w, p, 4);
            v[0] = fgl_uf(w & 2047, 6), v[1] = fgl_uf((w >> 11) & 2047, 6), v[2] = fgl_uf(w >> 22, 5);
        } else if (type == GL_UNSIGNED_INT_5_9_9_9_REV) {
            uint32_t w;
            memcpy(&w, p, 4);
            float sc = ldexpf(1.0f, (int)(w >> 27) - 15 - 9);
            v[0] = (float)(w & 511) * sc, v[1] = (float)((w >> 9) & 511) * sc, v[2] = (float)((w >> 18) & 511) * sc;
        } else if (type == GL_UNSIGNED_INT_24_8) {
            uint32_t w;
            memcpy(&w, p, 4);
            v[0] = (float)(w >> 8) / 16777215.0f;
        } else if (type == GL_FLOAT_32_UNSIGNED_INT_24_8_REV) {
            memcpy(&v[0], p, 4);
        } else if (pk) {
            uint32_t w = 0;
            memcpy(&w, p, (size_t)pk->bytes);
            int total = 0;
            for (int k = 0; k < pk->n; k++) total += pk->bits[k];
            int sh = pk->rev ? 0 : total;
            for (int k = 0; k < pk->n && k < nc; k++) {
                int      b = pk->bits[k];
                uint32_t f;
                if (pk->rev) f = (w >> sh) & ((1u << b) - 1u), sh += b;
                else sh -= b, f = (w >> sh) & ((1u << b) - 1u);
                v[k] = integer ? (float)f / 255.0f : (float)f / (float)((1u << b) - 1u);
            }
        } else {
            int ts = fgl_type_size(type);
            for (int k = 0; k < nc; k++) {
                const uint8_t* q = p + (size_t)k * (size_t)ts;
                float          f = 0;
                switch (type) {
                case GL_UNSIGNED_BYTE: f = integer ? (float)*q / 255.0f : (float)*q / 255.0f; break;
                case GL_BYTE: f = integer ? (float)(int8_t)*q / 255.0f : (float)(int8_t)*q / 127.0f; break;
                case GL_UNSIGNED_SHORT: { uint16_t s; memcpy(&s, q, 2); f = integer ? (float)s / 255.0f : (float)s / 65535.0f; break; }
                case GL_SHORT: { int16_t s; memcpy(&s, q, 2); f = integer ? (float)s / 255.0f : (float)s / 32767.0f; break; }
                case GL_UNSIGNED_INT: { uint32_t s; memcpy(&s, q, 4); f = integer ? (float)s / 255.0f : (float)((double)s / 4294967295.0); break; }
                case GL_INT: { int32_t s; memcpy(&s, q, 4); f = integer ? (float)s / 255.0f : (float)((double)s / 2147483647.0); break; }
                case GL_FLOAT: memcpy(&f, q, 4); break;
                case GL_HALF_FLOAT: { uint16_t h; memcpy(&h, q, 2); f = fgl_half2f(h); break; }
                default: break;
                }
                v[k] = f;
            }
        }
        for (int k = 0; k < nc; k++) {
            switch (comp[k]) {
            case 4: rgba[0] = rgba[1] = rgba[2] = v[k]; break;
            case 5: rgba[0] = rgba[1] = rgba[2] = v[k]; break; /* depth as gray */
            default: rgba[comp[k]] = v[k]; break;
            }
        }
        if (nc == 1 && comp[0] == 3) rgba[0] = rgba[1] = rgba[2] = 1.0f; /* GL_ALPHA: white with alpha (texenv modulate) */
        uint8_t* o = out + (size_t)i * 4;
        for (int k = 0; k < 4; k++) o[k] = fgl_u8(rgba[k]);
    }
}

/* n straight RGBA8 pixels -> client data (glReadPixels, glGetTexImage) */
int fgl_rgba8_to_pixels(GLenum fmt, GLenum type, const uint8_t* rgba, int n, uint8_t* dst)
{
    int comp[4], integer;
    int nc = fgl_fmt_comps(fmt, comp, &integer);
    int ps = fgl_pixel_bytes(fmt, type);
    if (!nc || !ps) return 0;
    const fgl_packed* pk = fgl_packed_of(type);
    for (int i = 0; i < n; i++) {
        const uint8_t* s = rgba + (size_t)i * 4;
        uint8_t*       p = dst + (size_t)i * (size_t)ps;
        float          v[4];
        for (int k = 0; k < nc; k++) {
            int c = comp[k];
            v[k]  = c == 4 ? (float)s[0] / 255.0f : (c == 5 ? (float)s[0] / 255.0f : (float)s[c] / 255.0f);
        }
        if (pk) {
            uint32_t w = 0;
            int      total = 0;
            for (int k = 0; k < pk->n; k++) total += pk->bits[k];
            int sh = pk->rev ? 0 : total;
            for (int k = 0; k < pk->n; k++) {
                int      b = pk->bits[k];
                uint32_t m = (1u << b) - 1u, f = k < nc ? (uint32_t)(v[k] * (float)m + 0.5f) : m;
                if (pk->rev) w |= (f & m) << sh, sh += b;
                else sh -= b, w |= (f & m) << sh;
            }
            memcpy(p, &w, (size_t)pk->bytes);
            continue;
        }
        int ts = fgl_type_size(type);
        for (int k = 0; k < nc; k++) {
            uint8_t* q = p + (size_t)k * (size_t)ts;
            float    f = v[k];
            switch (type) {
            case GL_UNSIGNED_BYTE: *q = (uint8_t)(f * 255.0f + 0.5f); break;
            case GL_BYTE: *q = (uint8_t)(int8_t)(f * 127.0f + 0.5f); break;
            case GL_UNSIGNED_SHORT: { uint16_t x = (uint16_t)(f * 65535.0f + 0.5f); memcpy(q, &x, 2); break; }
            case GL_SHORT: { int16_t x = (int16_t)(f * 32767.0f + 0.5f); memcpy(q, &x, 2); break; }
            case GL_UNSIGNED_INT: { uint32_t x = (uint32_t)((double)f * 4294967295.0 + 0.5); memcpy(q, &x, 4); break; }
            case GL_INT: { int32_t x = (int32_t)((double)f * 2147483647.0 + 0.5); memcpy(q, &x, 4); break; }
            case GL_FLOAT: memcpy(q, &f, 4); break;
            case GL_HALF_FLOAT: { /* 0..1: exact enough through the float bits */
                uint32_t u;
                memcpy(&u, &f, 4);
                uint16_t h = f <= 0 ? 0 : (uint16_t)((((u >> 23) & 255) - 112) << 10 | ((u >> 13) & 1023));
                memcpy(q, &h, 2);
                break;
            }
            default: return 0;
            }
        }
    }
    return 1;
}

/* ---- compressed textures ---- */
int fgl_compressed_block_bytes(GLenum ifmt)
{
    switch (ifmt) {
    case GL_COMPRESSED_RGB_S3TC_DXT1_EXT: case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT: case GL_COMPRESSED_SRGB_S3TC_DXT1_EXT:
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT: case GL_COMPRESSED_RED_RGTC1: case GL_COMPRESSED_SIGNED_RED_RGTC1:
        return 8;
    case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT: case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT: case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
    case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT: case GL_COMPRESSED_RG_RGTC2: case GL_COMPRESSED_SIGNED_RG_RGTC2:
        return 16;
    default: return 0;
    }
}

/* the formats glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS) lists */
const GLenum fgl_compressed_formats[] = { GL_COMPRESSED_RGB_S3TC_DXT1_EXT, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,
                                          GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,
                                          GL_COMPRESSED_RED_RGTC1,         GL_COMPRESSED_SIGNED_RED_RGTC1,
                                          GL_COMPRESSED_RG_RGTC2,          GL_COMPRESSED_SIGNED_RG_RGTC2 };
const int    fgl_ncompressed_formats = 8;

static void fgl_565(uint16_t c, uint8_t* o)
{
    o[0] = (uint8_t)((c >> 11) * 255 / 31), o[1] = (uint8_t)(((c >> 5) & 63) * 255 / 63), o[2] = (uint8_t)((c & 31) * 255 / 31);
}

/* a DXT color block: 4 x 4 RGBA into px (stride 4 pixels); dxt1 decides the 3 color + transparent mode */
static void fgl_dxt_color(const uint8_t* b, int dxt1, uint8_t px[16][4])
{
    uint16_t c0 = (uint16_t)(b[0] | b[1] << 8), c1 = (uint16_t)(b[2] | b[3] << 8);
    uint8_t  pal[4][4];
    fgl_565(c0, pal[0]), fgl_565(c1, pal[1]);
    pal[0][3] = pal[1][3] = 255;
    if (c0 > c1 || !dxt1) {
        for (int k = 0; k < 3; k++) {
            pal[2][k] = (uint8_t)((2 * pal[0][k] + pal[1][k] + 1) / 3);
            pal[3][k] = (uint8_t)((pal[0][k] + 2 * pal[1][k] + 1) / 3);
        }
        pal[2][3] = pal[3][3] = 255;
    } else {
        for (int k = 0; k < 3; k++) pal[2][k] = (uint8_t)((pal[0][k] + pal[1][k]) / 2), pal[3][k] = 0;
        pal[2][3] = 255, pal[3][3] = 0;
    }
    uint32_t idx = (uint32_t)b[4] | (uint32_t)b[5] << 8 | (uint32_t)b[6] << 16 | (uint32_t)b[7] << 24;
    for (int i = 0; i < 16; i++) memcpy(px[i], pal[(idx >> (2 * i)) & 3], 4);
}

/* an RGTC / DXT5 alpha block: 16 values (signed: -127..127 mapped to 0..255) */
static void fgl_bc4(const uint8_t* b, int sgn, uint8_t* out)
{
    float e0 = sgn ? (float)(int8_t)b[0] : (float)b[0], e1 = sgn ? (float)(int8_t)b[1] : (float)b[1];
    if (sgn) e0 = e0 < -127 ? -127 : e0, e1 = e1 < -127 ? -127 : e1;
    float pal[8];
    pal[0] = e0, pal[1] = e1;
    if (e0 > e1)
        for (int k = 1; k < 7; k++) pal[k + 1] = ((float)(7 - k) * e0 + (float)k * e1) / 7.0f;
    else {
        for (int k = 1; k < 5; k++) pal[k + 1] = ((float)(5 - k) * e0 + (float)k * e1) / 5.0f;
        pal[6] = sgn ? -127.0f : 0.0f, pal[7] = sgn ? 127.0f : 255.0f;
    }
    uint64_t bits = 0;
    for (int k = 0; k < 6; k++) bits |= (uint64_t)b[2 + k] << (8 * k);
    for (int i = 0; i < 16; i++) {
        float v = pal[(bits >> (3 * i)) & 7];
        if (sgn) v = (v + 127.0f) * (255.0f / 254.0f); /* -1..1 stored as 0..1 */
        out[i] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v + 0.5f));
    }
}

/* decode a w x h compressed image into rgba (w * h * 4 bytes); 0 if size is short */
int fgl_decompress(GLenum ifmt, const uint8_t* data, size_t size, int w, int h, uint8_t* rgba)
{
    int bb = fgl_compressed_block_bytes(ifmt), bw = (w + 3) / 4, bh = (h + 3) / 4;
    if (!bb || (size_t)bw * (size_t)bh * (size_t)bb > size) return 0;
    int dxt1 = ifmt == GL_COMPRESSED_RGB_S3TC_DXT1_EXT || ifmt == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT || ifmt == GL_COMPRESSED_SRGB_S3TC_DXT1_EXT ||
               ifmt == GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT;
    int opaque1 = ifmt == GL_COMPRESSED_RGB_S3TC_DXT1_EXT || ifmt == GL_COMPRESSED_SRGB_S3TC_DXT1_EXT;
    int dxt3 = ifmt == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT || ifmt == GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT;
    int dxt5 = ifmt == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT || ifmt == GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT;
    int bc4  = ifmt == GL_COMPRESSED_RED_RGTC1 || ifmt == GL_COMPRESSED_SIGNED_RED_RGTC1;
    int sgn  = ifmt == GL_COMPRESSED_SIGNED_RED_RGTC1 || ifmt == GL_COMPRESSED_SIGNED_RG_RGTC2;
    for (int by = 0; by < bh; by++)
        for (int bx = 0; bx < bw; bx++) {
            const uint8_t* b = data + ((size_t)by * (size_t)bw + (size_t)bx) * (size_t)bb;
            uint8_t        px[16][4];
            if (dxt1) {
                fgl_dxt_color(b, 1, px);
                if (opaque1)
                    for (int i = 0; i < 16; i++) px[i][3] = 255;
            } else if (dxt3 || dxt5) {
                fgl_dxt_color(b + 8, 0, px);
                if (dxt3) {
                    for (int i = 0; i < 16; i++) px[i][3] = (uint8_t)(((b[i / 2] >> (4 * (i & 1))) & 15) * 17);
                } else {
                    uint8_t a[16];
                    fgl_bc4(b, 0, a);
                    for (int i = 0; i < 16; i++) px[i][3] = a[i];
                }
            } else { /* RGTC: red (+ green) */
                uint8_t r[16], g[16];
                fgl_bc4(b, sgn, r);
                if (!bc4) fgl_bc4(b + 8, sgn, g);
                for (int i = 0; i < 16; i++) px[i][0] = r[i], px[i][1] = bc4 ? 0 : g[i], px[i][2] = 0, px[i][3] = 255;
            }
            for (int i = 0; i < 16; i++) {
                int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x < w && y < h) memcpy(rgba + ((size_t)y * (size_t)w + (size_t)x) * 4, px[i], 4);
            }
        }
    return 1;
}

/* the client pointer of an upload: an offset into GL_PIXEL_UNPACK_BUFFER when one is bound */
const void* fgl_unpack_ptr(fgl_ctx* c, const void* pixels)
{
    if (!c->unpack_buffer) return pixels;
    fgl_buf* b = fgl_buffer(c, c->unpack_buffer);
    return b && b->data ? b->data + (uintptr_t)pixels : NULL;
}

/* the destination of a read: an offset into GL_PIXEL_PACK_BUFFER when one is bound */
void* fgl_pack_ptr(fgl_ctx* c, void* pixels)
{
    if (!c->pack_buffer) return pixels;
    fgl_buf* b = fgl_buffer(c, c->pack_buffer);
    return b && b->data ? b->data + (uintptr_t)pixels : NULL;
}
