/*
 * fatgl - an OpenGL implementation on the fatmap software rasterizer.
 * Internal header: the context, shared helpers.
 */
#ifndef FGL_H
#define FGL_H

#define _GDI32_ /* wingdi.h: the wgl* functions are defined here, not imported */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define GL_GLEXT_PROTOTYPES 1
#include "GL/glcorearb.h"
#include "gen/gl_decls.h"

#include <fatmap/fatmap.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---- compatibility profile enums (glcorearb.h has the core ones) ---- */
#define GL_QUADS                  0x0007
#define GL_QUAD_STRIP             0x0008
#define GL_POLYGON                0x0009
#define GL_CURRENT_COLOR          0x0B00
#define GL_CURRENT_NORMAL         0x0B02
#define GL_CURRENT_TEXTURE_COORDS 0x0B03
#define GL_MAX_LIST_NESTING       0x0B31
#define GL_LIGHTING               0x0B50
#define GL_LIGHT_MODEL_LOCAL_VIEWER 0x0B51
#define GL_LIGHT_MODEL_TWO_SIDE   0x0B52
#define GL_LIGHT_MODEL_AMBIENT    0x0B53
#define GL_SHADE_MODEL            0x0B54
#define GL_COLOR_MATERIAL         0x0B57
#define GL_FOG                    0x0B60
#define GL_MATRIX_MODE            0x0BA0
#define GL_NORMALIZE              0x0BA1
#define GL_MODELVIEW_MATRIX       0x0BA6
#define GL_PROJECTION_MATRIX      0x0BA7
#define GL_TEXTURE_MATRIX         0x0BA8
#define GL_ALPHA_TEST             0x0BC0
#define GL_MAX_LIGHTS             0x0D31
#define GL_MAX_MODELVIEW_STACK_DEPTH  0x0D36
#define GL_MAX_PROJECTION_STACK_DEPTH 0x0D38
#define GL_AMBIENT                0x1200
#define GL_DIFFUSE                0x1201
#define GL_SPECULAR               0x1202
#define GL_POSITION               0x1203
#define GL_SPOT_DIRECTION         0x1204
#define GL_SPOT_EXPONENT          0x1205
#define GL_SPOT_CUTOFF            0x1206
#define GL_CONSTANT_ATTENUATION   0x1207
#define GL_LINEAR_ATTENUATION     0x1208
#define GL_QUADRATIC_ATTENUATION  0x1209
#define GL_COMPILE                0x1300
#define GL_COMPILE_AND_EXECUTE    0x1301
#define GL_EMISSION               0x1600
#define GL_SHININESS              0x1601
#define GL_AMBIENT_AND_DIFFUSE    0x1602
#define GL_MODELVIEW              0x1700
#define GL_PROJECTION             0x1701
#define GL_LUMINANCE              0x1909
#define GL_LUMINANCE_ALPHA        0x190A
#define GL_FLAT                   0x1D00
#define GL_SMOOTH                 0x1D01
#define GL_MODULATE               0x2100
#define GL_DECAL                  0x2101
#define GL_ADD                    0x0104
#define GL_TEXTURE_ENV_MODE       0x2200
#define GL_TEXTURE_ENV            0x2300
#define GL_CLAMP                  0x2900
#define GL_INTENSITY              0x8049
#define GL_VERTEX_ARRAY           0x8074
#define GL_NORMAL_ARRAY           0x8075
#define GL_COLOR_ARRAY            0x8076
#define GL_TEXTURE_COORD_ARRAY    0x8078
#define GL_LIGHT0                 0x4000
#define GL_RED_BITS               0x0D52
#define GL_GREEN_BITS             0x0D53
#define GL_BLUE_BITS              0x0D54
#define GL_ALPHA_BITS             0x0D55
#define GL_DEPTH_BITS             0x0D56
#define GL_STENCIL_BITS           0x0D57

/* ---- context ---- */
#define FGL_STACK  32 /* matrix stack depth */
#define FGL_LIGHTS 8

enum { FGL_MV = 0, FGL_PROJ, FGL_TEXM };

/* enable bits */
enum {
    FGL_E_DEPTH = 1u << 0, FGL_E_CULL = 1u << 1, FGL_E_LIGHTING = 1u << 2, FGL_E_TEX2D = 1u << 3, FGL_E_BLEND = 1u << 4,
    FGL_E_ALPHA = 1u << 5, FGL_E_COLMAT = 1u << 6, FGL_E_NORMALIZE = 1u << 7, FGL_E_SCISSOR = 1u << 8,
    FGL_E_STENCIL = 1u << 9, FGL_E_POFFSET = 1u << 10
};

typedef struct fgl_vtx { /* an immediate mode vertex: object position, current attributes */
    float pos[4], nrm[3], tex[2], col[4];
} fgl_vtx;

typedef struct fgl_tex {
    GLuint        name;
    int           used;
    fm_surface*   level0; /* the image of level 0 (premultiplied ARGB32) */
    fm3d_texture* tex;    /* built on use (mipmaps from level 0) */
    int           built_mips;
    GLenum        min_filter, mag_filter, wrap_s, wrap_t;
} fgl_tex;

typedef struct fgl_op fgl_op; /* a recorded display list command */
typedef struct fgl_list {
    GLuint  name;
    int     used;
    fgl_op* ops;
    int     nops, cap;
} fgl_list;

typedef struct fgl_ctx {
    HDC          hdc;
    fm3d_ctx*    c3;
    fm_executor* ex;
    fm_surface*  color; /* back buffer, premultiplied ARGB32 (straight when alpha is 1) */
    fm_surface*  depth; /* D24S8 */
    int          fbw, fbh;
    int          made_current; /* the first wglMakeCurrent sets the viewport */
    int          major, minor, core; /* requested version (wglCreateContextAttribsARB) */
    GLenum       error;

    /* state */
    unsigned enables;
    unsigned light_on; /* bit i: GL_LIGHT0 + i */
    float    clear_color[4];
    float    clear_depth;
    int      clear_stencil;
    int      viewport[4], scissor[4];
    GLenum   depth_func, cull_face, front_face, shade_model;
    GLboolean depth_mask, color_mask[4];
    GLenum   blend_src, blend_dst;
    GLenum   alpha_func;
    float    alpha_ref;
    float    poly_factor, poly_units;
    int      unpack_align, unpack_row, pack_align;

    /* matrices */
    GLenum  matrix_mode;
    fm_mat4 mstack[3][FGL_STACK];
    int     msp[3];

    /* lighting (positions / directions in eye space, as GL keeps them) */
    struct {
        float amb[4], dif[4], spe[4], pos[4], spot_dir[3], spot_exp, spot_cut, att[3];
    } light[FGL_LIGHTS];
    float  light_model_ambient[4];
    struct {
        float amb[4], dif[4], spe[4], emi[4], shin;
    } mat;
    GLenum colmat_mode;

    /* current vertex attributes */
    float cur_color[4], cur_normal[3], cur_tex[4];

    /* immediate mode */
    int      in_begin;
    GLenum   prim;
    fgl_vtx* imm;
    int      nimm, immcap;

    /* textures */
    fgl_tex* tex;
    int      ntex;
    GLuint   bound_tex;
    GLenum   tex_env;

    /* display lists */
    fgl_list* lists;
    int       nlists;
    GLuint    list_compiling; /* 0: none */
    GLenum    list_mode;
    int       list_depth; /* glCallList nesting */

    /* client vertex arrays: vertex, normal, color, texcoord */
    struct {
        int         on;
        GLint       size;
        GLenum      type;
        GLsizei     stride;
        const void* ptr;
    } va[4];
} fgl_ctx;

fgl_ctx* fgl_cur(void);
void     fgl_error(GLenum e);
void     fgl_unimplemented(const char* name);
void     fgl_ctx_init(fgl_ctx* c);
void     fgl_ctx_free(fgl_ctx* c);
int      fgl_resize(fgl_ctx* c, int w, int h);
void     fgl_flush(fgl_ctx* c);

/* the fatmap state of the current GL state, before drawing */
void fgl_sync(fgl_ctx* c);
/* triangles of a primitive (GL_TRIANGLES .. GL_POLYGON) through fatmap */
void fgl_draw_prim(fgl_ctx* c, GLenum prim, const fgl_vtx* v, int n);
fgl_tex* fgl_texture(fgl_ctx* c, GLuint name, int create);
fgl_list* fgl_list_get(fgl_ctx* c, GLuint name, int create);

/* display lists: while compiling, commands are recorded (and executed
 * for GL_COMPILE_AND_EXECUTE) */
enum {
    FGL_OP_BEGIN, FGL_OP_END, FGL_OP_VERTEX, FGL_OP_NORMAL, FGL_OP_COLOR, FGL_OP_TEXCOORD, FGL_OP_MATERIAL,
    FGL_OP_LIGHT, FGL_OP_LIGHTMODEL, FGL_OP_SHADEMODEL, FGL_OP_ENABLE, FGL_OP_DISABLE, FGL_OP_MATRIXMODE,
    FGL_OP_LOADIDENTITY, FGL_OP_LOADMATRIX, FGL_OP_MULTMATRIX, FGL_OP_PUSHMATRIX, FGL_OP_POPMATRIX,
    FGL_OP_ROTATE, FGL_OP_TRANSLATE, FGL_OP_SCALE, FGL_OP_BINDTEXTURE, FGL_OP_CALLLIST, FGL_OP_BLENDFUNC,
    FGL_OP_DEPTHFUNC, FGL_OP_FRONTFACE, FGL_OP_CULLFACE, FGL_OP_COLORMATERIAL
};
struct fgl_op {
    int    op;
    GLenum e0, e1;
    float  f[16];
};
/* returns 1 when the caller must not execute the command now (GL_COMPILE) */
int fgl_record(fgl_ctx* c, int op, GLenum e0, GLenum e1, const float* f, int nf);

typedef struct fgl_proc {
    const char* name;
    PROC        proc;
} fgl_proc;
extern const fgl_proc fgl_procs[];
extern const int      fgl_nprocs;

/* WGL extensions (wgl.c) */
const char* WINAPI wglGetExtensionsStringARB(HDC hdc);
const char* WINAPI wglGetExtensionsStringEXT(void);
HGLRC WINAPI       wglCreateContextAttribsARB(HDC hdc, HGLRC share, const int* attribs);
BOOL WINAPI        wglChoosePixelFormatARB(HDC hdc, const int* iattr, const FLOAT* fattr, UINT max, int* formats, UINT* n);
BOOL WINAPI        wglGetPixelFormatAttribivARB(HDC hdc, int fmt, int layer, UINT n, const int* attr, int* values);
BOOL WINAPI        wglGetPixelFormatAttribfvARB(HDC hdc, int fmt, int layer, UINT n, const int* attr, FLOAT* values);
BOOL WINAPI        wglSwapIntervalEXT(int interval);
int WINAPI         wglGetSwapIntervalEXT(void);
BOOL WINAPI        wglMakeContextCurrentARB(HDC draw, HDC read, HGLRC hglrc);
HDC WINAPI         wglGetCurrentReadDCARB(void);

#define FGL_CTX_OR_RETURN(c)                                                                                            \
    fgl_ctx* c = fgl_cur();                                                                                             \
    if (!c) return
#endif
