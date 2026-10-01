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
#define GL_FOG_INDEX              0x0B61
#define GL_FOG_DENSITY            0x0B62
#define GL_FOG_START              0x0B63
#define GL_FOG_END                0x0B64
#define GL_FOG_MODE               0x0B65
#define GL_FOG_COLOR              0x0B66
#define GL_EXP                    0x0800
#define GL_EXP2                   0x0801
#define GL_FOG_COORD_SRC          0x8450
#define GL_CLIENT_ACTIVE_TEXTURE  0x84E1
#define GL_MAX_TEXTURE_UNITS      0x84E2
#define GL_MAX_TEXTURE_COORDS     0x8871
/* ARB_vertex_program / ARB_fragment_program */
#define GL_VERTEX_PROGRAM_ARB                       0x8620
#define GL_FRAGMENT_PROGRAM_ARB                     0x8804
#define GL_PROGRAM_FORMAT_ASCII_ARB                 0x8875
#define GL_PROGRAM_ERROR_POSITION_ARB               0x864B
#define GL_PROGRAM_ERROR_STRING_ARB                 0x8874
#define GL_PROGRAM_LENGTH_ARB                       0x8627
#define GL_PROGRAM_FORMAT_ARB                       0x8876
#define GL_PROGRAM_BINDING_ARB                      0x8677
#define GL_PROGRAM_INSTRUCTIONS_ARB                 0x88A0
#define GL_MAX_PROGRAM_INSTRUCTIONS_ARB             0x88A1
#define GL_PROGRAM_NATIVE_INSTRUCTIONS_ARB          0x88A2
#define GL_MAX_PROGRAM_NATIVE_INSTRUCTIONS_ARB      0x88A3
#define GL_PROGRAM_TEMPORARIES_ARB                  0x88A4
#define GL_MAX_PROGRAM_TEMPORARIES_ARB              0x88A5
#define GL_PROGRAM_NATIVE_TEMPORARIES_ARB           0x88A6
#define GL_MAX_PROGRAM_NATIVE_TEMPORARIES_ARB       0x88A7
#define GL_PROGRAM_PARAMETERS_ARB                   0x88A8
#define GL_MAX_PROGRAM_PARAMETERS_ARB               0x88A9
#define GL_PROGRAM_NATIVE_PARAMETERS_ARB            0x88AA
#define GL_MAX_PROGRAM_NATIVE_PARAMETERS_ARB        0x88AB
#define GL_PROGRAM_ATTRIBS_ARB                      0x88AC
#define GL_MAX_PROGRAM_ATTRIBS_ARB                  0x88AD
#define GL_PROGRAM_NATIVE_ATTRIBS_ARB               0x88AE
#define GL_MAX_PROGRAM_NATIVE_ATTRIBS_ARB           0x88AF
#define GL_PROGRAM_ADDRESS_REGISTERS_ARB            0x88B0
#define GL_MAX_PROGRAM_ADDRESS_REGISTERS_ARB        0x88B1
#define GL_PROGRAM_NATIVE_ADDRESS_REGISTERS_ARB     0x88B2
#define GL_MAX_PROGRAM_NATIVE_ADDRESS_REGISTERS_ARB 0x88B3
#define GL_MAX_PROGRAM_LOCAL_PARAMETERS_ARB         0x88B4
#define GL_MAX_PROGRAM_ENV_PARAMETERS_ARB           0x88B5
#define GL_PROGRAM_UNDER_NATIVE_LIMITS_ARB          0x88B6
#define GL_MAX_PROGRAM_MATRICES_ARB                 0x862F
#define GL_MAX_PROGRAM_MATRIX_STACK_DEPTH_ARB       0x862E
#define GL_MAX_PROGRAM_ALU_INSTRUCTIONS_ARB         0x880B
#define GL_MAX_PROGRAM_TEX_INSTRUCTIONS_ARB         0x880C
#define GL_MAX_PROGRAM_TEX_INDIRECTIONS_ARB         0x880D
#define GL_MAX_PROGRAM_NATIVE_ALU_INSTRUCTIONS_ARB  0x880E
#define GL_MAX_PROGRAM_NATIVE_TEX_INSTRUCTIONS_ARB  0x880F
#define GL_MAX_PROGRAM_NATIVE_TEX_INDIRECTIONS_ARB  0x8810
#define GL_MATRIX_MODE            0x0BA0
#define GL_NORMALIZE              0x0BA1
#define GL_MODELVIEW_MATRIX       0x0BA6
#define GL_PROJECTION_MATRIX      0x0BA7
#define GL_TEXTURE_MATRIX         0x0BA8
#define GL_ALPHA_TEST             0x0BC0
#define GL_ALPHA_TEST_FUNC        0x0BC1
#define GL_ALPHA_TEST_REF         0x0BC2
#define GL_ALIASED_POINT_SIZE_RANGE 0x846D
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
#define GL_ALPHA                  0x1906
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
    FGL_E_STENCIL = 1u << 9, FGL_E_POFFSET = 1u << 10, FGL_E_RESTART = 1u << 11,
    FGL_E_PSIZE = 1u << 12, FGL_E_FOG = 1u << 13, FGL_E_DEBUG = 1u << 14,
    FGL_E_VP = 1u << 15, FGL_E_FP = 1u << 16 /* GL_VERTEX_PROGRAM_ARB, GL_FRAGMENT_PROGRAM_ARB */
};

typedef struct fgl_vtx { /* an immediate mode vertex: object position, current attributes */
    float pos[4], nrm[3], tex[4], col[4], tex1[4]; /* s t r q; tex1: texture unit 1 (multitexture) */
} fgl_vtx;

/* texture binding targets (per texture unit) */
enum { FGL_TT_2D = 0, FGL_TT_1D, FGL_TT_3D, FGL_TT_CUBE, FGL_TT_RECT, FGL_TT_1D_ARRAY, FGL_TT_2D_ARRAY, FGL_TT_COUNT };

typedef struct fgl_tex {
    GLuint        name;
    int           used;
    int           tt;     /* FGL_TT_*: the target of the first glBindTexture */
    int           layers; /* images stacked top to bottom in level0: cube faces (+X -X +Y -Y +Z -Z), 3D slices, array layers */
    fm_surface*   level0; /* the image of level 0 (straight ARGB32) */
    fm_surface**  view;   /* views of the layers of level0 (framebuffer attachments), made on demand */
    fm_surface*   depth;  /* depth textures: D32F / D24S8 / D16 instead of level0 */
    fm3d_texture* tex;    /* built on use (mipmaps from level 0) */
    int           built_mips;
    int           rendered; /* drawn into through a framebuffer since tex was built */
    GLenum        min_filter, mag_filter, wrap_s, wrap_t, wrap_r;
    GLenum        ifmt; /* glTexImage2D internal format */
    float         min_lod, max_lod, lod_bias, border[4];
    GLenum        compare_mode, compare_func;
    GLint         base_level, max_level;
    GLenum        swizzle[4]; /* GL_TEXTURE_SWIZZLE_R / G / B / A (applied when the fatmap texture is built) */
} fgl_tex;

/* ---- GL 2.0 / 3.x objects ---- */
#define FGL_ATTRIBS   16 /* generic vertex attributes */
#define FGL_UNITS     16 /* texture image units */
#define FGL_UBO_BINDS 36 /* indexed uniform buffer binding points */
#define FGL_DEF_VS    15 /* fatmap uniform block binding of the default block (glslang links one block for both stages) */
#define FGL_DEF_FS    15

typedef struct fgl_buf {
    GLuint     name;
    int        used;
    uint8_t*   data;
    GLsizeiptr size;
    GLenum     usage;
    int        mapped;
} fgl_buf;

typedef struct fgl_attrib {
    int        enabled, integer, normalized;
    GLint      size;
    GLenum     type;
    GLsizei    stride;
    GLintptr   offset; /* into the buffer (or a client pointer with buffer 0) */
    GLuint     buffer;
    GLuint     divisor;
} fgl_attrib;

typedef struct fgl_vao {
    GLuint     name;
    int        used;
    GLuint     elements; /* GL_ELEMENT_ARRAY_BUFFER */
    fgl_attrib a[FGL_ATTRIBS];
} fgl_vao;

typedef struct fgl_shader {
    GLuint name;
    int    used;
    GLenum type;
    char*  src;
    int    compiled;
    char*  log;
    int    delete_pending;
} fgl_shader;

/* a uniform of the default block (both stages: their own offsets, -1 if
 * the stage does not use it) or a sampler */
typedef struct fgl_uniform {
    char   name[96];
    GLenum type;           /* GL_FLOAT_VEC4, GL_FLOAT_MAT4, GL_INT, GL_SAMPLER_2D, ... */
    int    count;          /* array elements (1 if not an array) */
    int    off[2];         /* byte offset in the vertex / fragment default block */
    int    astride, mstride;
    int    sampler_binding; /* samplers: the fatmap texture unit */
    int    unit;            /* samplers: the GL texture unit (glUniform1i) */
} fgl_uniform;

typedef struct fgl_ublock { /* a named uniform block */
    char name[96];
    int  spv_binding[2]; /* per stage, -1 if unused */
    int  size;
    int  gl_binding;     /* glUniformBlockBinding */
} fgl_ublock;

typedef struct fgl_input { /* a vertex shader input */
    char name[96];
    int  location;
} fgl_input;

typedef struct fgl_program {
    GLuint       name;
    int          used;
    GLuint       shaders[8];
    int          nshaders;
    int          linked;
    char*        log;
    fm3d_spirv*  sp;
    fm3d_program prog;
    uint8_t*     def[2]; /* default uniform block data per stage */
    int          defsize[2];
    fgl_uniform* u;
    int          nu;
    fgl_ublock   blocks[14];
    int          nblocks;
    fgl_input    in[FGL_ATTRIBS];
    int          nin;
    int          max_loc;
    struct {
        char name[96];
        int  index;
    } bind_attrib[FGL_ATTRIBS]; /* glBindAttribLocation */
    int nbind;
    int delete_pending;
} fgl_program;

/* framebuffer objects: one color attachment (fatmap has one color target) */
typedef struct fgl_rbo {
    GLuint      name;
    int         used;
    GLenum      ifmt;
    fm_surface* s; /* ARGB32, a depth format or A8 (stencil) */
} fgl_rbo;

typedef struct fgl_attach {
    GLenum type; /* GL_NONE, GL_TEXTURE, GL_RENDERBUFFER */
    GLuint name;
    GLint  level;
    GLint  layer; /* layered textures: the layer (cube maps: the face) */
} fgl_attach;

#define FGL_COLOR_ATTACHMENTS 8 /* accepted; only attachment 0 is drawn */
typedef struct fgl_fbo {
    GLuint      name;
    int         used;
    fgl_attach  color[FGL_COLOR_ATTACHMENTS], depth, stencil;
    GLenum      draw_buf[FGL_COLOR_ATTACHMENTS], read_buf;
    fm_surface* dummy; /* color target of depth only framebuffers */
} fgl_fbo;

/* sampler objects (GL 3.3): override the parameters of the texture on their unit */
typedef struct fgl_sampler {
    GLuint name;
    int    used;
    GLenum min_filter, mag_filter, wrap_s, wrap_t, wrap_r;
    float  min_lod, max_lod, lod_bias, border[4], aniso;
    GLenum compare_mode, compare_func;
} fgl_sampler;

/* queries: occlusion (fragments reaching the fragment stage), primitives, time */
typedef struct fgl_query {
    GLuint   name;
    int      used;
    GLenum   target;
    uint64_t start, result;
} fgl_query;

typedef struct fgl_op fgl_op; /* a recorded display list command */
typedef struct fgl_list {
    GLuint  name;
    int     used;
    fgl_op* ops;
    int     nops, cap;
} fgl_list;

typedef struct fgl_clarray { /* a client vertex array (glVertexPointer & co.) */
    int         on;
    GLint       size;
    GLenum      type;
    GLsizei     stride;
    const void* ptr;
    GLuint      buffer; /* GL_ARRAY_BUFFER when the pointer was set: ptr is an offset */
} fgl_clarray;

/* fatgl's per frame counters (the overlay) */
typedef struct fgl_counters {
    uint64_t draws_fixed, draws_prog, tex_builds, upload_bytes;
} fgl_counters;

typedef struct fgl_ctx {
    HDC          hdc;
    fm3d_ctx*    c3;
    fm_executor* ex;
    fm_surface*  color; /* back buffer, straight ARGB32 (fm3d_set_blend_state) */
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
    GLenum   poly_mode[2]; /* glPolygonMode: front, back */
    GLboolean depth_mask, color_mask[4];
    GLenum   blend_src, blend_dst, blend_src_a, blend_dst_a; /* glBlendFuncSeparate */
    GLenum   blend_eq, blend_eq_a;                         /* glBlendEquationSeparate */
    float    blend_color[4];
    GLenum   alpha_func;
    float    alpha_ref;
    float    poly_factor, poly_units;
    int      unpack_align, unpack_row, pack_align, unpack_skip_rows, unpack_skip_pixels;
    int      unpack_swap, pack_swap; /* GL_UNPACK_SWAP_BYTES / GL_PACK_SWAP_BYTES */

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
    GLenum   tex_env;           /* unit 0 (= tex_envs[0]) */
    GLenum   tex_envs[FGL_UNITS]; /* glTexEnv GL_TEXTURE_ENV_MODE per texture unit */
    unsigned tex2d_units;       /* glEnable(GL_TEXTURE_2D) per texture unit */
    int      client_unit;       /* glClientActiveTexture */
    float    cur_tex1[4];       /* glMultiTexCoord of unit 1 */
    GLenum   fog_mode;
    float    fog_density, fog_start, fog_end, fog_color[4];

    /* display lists */
    fgl_list* lists;
    int       nlists;
    GLuint    list_compiling; /* 0: none */
    GLenum    list_mode;
    int       list_depth; /* glCallList nesting */

    /* GL 2.0 / 3.x objects (shared namespaces: shaders + programs) */
    fgl_buf*     bufs;
    int          nbufs;
    fgl_vao*     vaos;
    int          nvaos;
    fgl_vao      vao0; /* the default vertex array (compatibility) */
    GLuint       vao;  /* bound vertex array (0: vao0) */
    fgl_shader*  shaders;
    int          nshaders;
    fgl_program* progs;
    int          nprogs;
    GLuint       next_sp_name;
    GLuint       program; /* glUseProgram */
    GLuint       array_buffer, uniform_buffer, copy_read, copy_write, unpack_buffer, pack_buffer;
    struct {
        GLuint     buffer;
        GLintptr   offset;
        GLsizeiptr size; /* 0: the whole buffer */
    } ubo[FGL_UBO_BINDS];
    fgl_rbo*    rbos;
    int         nrbos;
    fgl_fbo*    fbos;
    int         nfbos;
    GLuint      draw_fbo, read_fbo, renderbuffer;
    fm_surface* tgt_color; /* what the fatmap context renders into (NULL: rebind) */
    fm_surface* tgt_depth;
    fm_surface* tgt_stencil;
    fgl_sampler* samplers;
    int          nsamplers;
    GLuint       unit_sampler[FGL_UNITS];
    fgl_query*   queries;
    int          nqueries;
    GLuint       active_query[4]; /* samples passed / any samples / primitives generated / time elapsed */
    struct {
        GLenum func, sfail, dpfail, dppass;
        GLint  ref;
        GLuint mask, wmask;
    } stencil[2]; /* front, back */
    float    point_size, line_width;
    uint8_t* builtins; /* the fgl_Builtins block of legacy GLSL programs */
    fgl_counters cnt;  /* this frame */
    /* ARB_vertex_program / ARB_fragment_program (arbprog.c) */
    void*  arb_progs;
    int    narb;
    GLuint arb_bound[2]; /* vertex, fragment */
    float  arb_env[2][96][4];
    char   arb_error[256];
    int    arb_error_pos;
    void*  arb_fixed[3];
    void*  arb_pairs;
    int    narb_pairs, carb_pairs;
    GLDEBUGPROC  dbg_cb; /* glDebugMessageCallback */
    const void*  dbg_user;
    void*        overlay;
    GLenum provoking_vertex, logic_op;
    GLuint restart_index;
    int    restart_on; /* GL_PRIMITIVE_RESTART */
    GLuint unit_bind[FGL_TT_COUNT][FGL_UNITS]; /* texture bindings per target and texture unit */
    int    active_unit;
    float  attr_value[FGL_ATTRIBS][4]; /* generic attribute values (glVertexAttrib*) */

    /* client vertex arrays: vertex, normal, color, texcoord */
    fgl_clarray va[5]; /* [4]: texture coordinates of unit 1 */
#define FGL_BOUND_TEX(c) ((c)->unit_bind[FGL_TT_2D][(c)->active_unit])
} fgl_ctx;

fgl_ctx* fgl_cur(void);
void     fgl_error(GLenum e);
void     fgl_unimplemented(const char* name);
void     fgl_log(const char* fmt, ...); /* fatgl.log (wgl.c) */
/* a debug output message to the application's glDebugMessageCallback */
void     fgl_debug(fgl_ctx* c, GLenum source, GLenum type, GLenum severity, const char* msg);
void     fgl_overlay_frame(fgl_ctx* c); /* overlay.c: F10 performance overlay, at SwapBuffers */
void     fgl_overlay_free(fgl_ctx* c);
void     fgl_ctx_init(fgl_ctx* c);
void     fgl_ctx_free(fgl_ctx* c);
int      fgl_resize(fgl_ctx* c, int w, int h);
void     fgl_flush(fgl_ctx* c);

/* the fatmap state of the current GL state, before drawing */
void fgl_sync(fgl_ctx* c);
/* triangles of a primitive (GL_TRIANGLES .. GL_POLYGON) through fatmap */
void fgl_draw_prim(fgl_ctx* c, GLenum prim, const fgl_vtx* v, int n);
/* glPolygonMode of the faces drawn (GL_FILL, GL_LINE, GL_POINT) */
GLenum fgl_polygon_mode(const fgl_ctx* c);
/* the edges of the polygons of `mode` (GL_TRIANGLES .. GL_POLYGON) from vertex numbers (-1: primitive restart) as
 * line index pairs relative to vmin; out holds 6 * count; returns the index count */
int fgl_outline(GLenum mode, const int* vid, int count, int vmin, uint32_t* out);
fgl_tex* fgl_texture(fgl_ctx* c, GLuint name, int create);
int      fgl_tex_target(GLenum target);         /* FGL_TT_* of a texture target (cube faces: FGL_TT_CUBE), -1 if none */
int      fgl_sampler_target(GLenum type);       /* FGL_TT_* of a sampler uniform type, -1 if not a sampler */
#define  fgl_is_sampler(type) (fgl_sampler_target(type) >= 0 || (type) == GL_SAMPLER_CUBE_MAP_ARRAY || (type) == GL_SAMPLER_BUFFER)
fm_surface* fgl_tex_layer(fgl_tex* t, int layer); /* a layer / face of level 0 (a view for layered textures) */
void     fgl_tex_free(fgl_ctx* c, fgl_tex* t);  /* the images of t */
/* fatmap texture + sampler of a GL texture object (built on demand); 0 without an image */
int fgl_texture_use(fgl_ctx* c, fgl_tex* t, int unit, fm3d_texture** tex, fm3d_sampler* s);
fgl_buf*     fgl_buffer(fgl_ctx* c, GLuint name);
fgl_vao*     fgl_cur_vao(fgl_ctx* c);
fgl_program* fgl_program_get(fgl_ctx* c, GLuint name);
/* a draw with the current program (GL 2.0+ path): mode, `count` vertices
 * or indices (indices NULL: arrays from `first`), instances */
void fgl_draw_program(fgl_ctx* c, GLenum mode, GLint first, GLsizei count, GLenum itype, const void* indices, GLint basevertex,
                      GLsizei instances);
fgl_list* fgl_list_get(fgl_ctx* c, GLuint name, int create);
void      fgl_draw_program_imm(fgl_ctx* c, GLenum mode, const fgl_vtx* v, int n);

/* gl_misc.c: stencil state into fatmap, glGet values kept there (0: not one), cleanup */
void fgl_sync_stencil(fgl_ctx* c);
/* arbprog.c: the GL program of the enabled ARB programs (NULL: none), their
 * parameter blocks (stage 0 vertex, 1 fragment; bytes written), errors, cleanup */
fgl_program* fgl_arb_program(fgl_ctx* c);
size_t       fgl_arb_block(fgl_ctx* c, int stage, float* out, size_t max);
const char*  fgl_arb_error_string(fgl_ctx* c);
void         fgl_arb_free(fgl_ctx* c);
/* the program a draw uses: the GLSL program, else the ARB programs */
fgl_program* fgl_active_program(fgl_ctx* c);
/* gl_pixels.c: client pixel data <-> straight RGBA8, compressed textures, pixel buffers */
int         fgl_pixel_bytes(GLenum fmt, GLenum type); /* 0: unsupported */
int         fgl_swap_unit(GLenum type);                /* bytes GL_*_SWAP_BYTES reverses (1: nothing) */
void        fgl_swap_bytes(uint8_t* p, size_t n, int unit);
void        fgl_pixels_to_rgba8(GLenum fmt, GLenum type, const uint8_t* src, int n, uint8_t* rgba);
int         fgl_rgba8_to_pixels(GLenum fmt, GLenum type, const uint8_t* rgba, int n, uint8_t* dst);
int         fgl_compressed_block_bytes(GLenum ifmt);
int         fgl_decompress(GLenum ifmt, const uint8_t* data, size_t size, int w, int h, uint8_t* rgba);
const void* fgl_unpack_ptr(fgl_ctx* c, const void* pixels);
void*       fgl_pack_ptr(fgl_ctx* c, void* pixels);
fm_surface* fgl_read_depth(fgl_ctx* c);
extern const GLenum fgl_compressed_formats[];
extern const int    fgl_ncompressed_formats;
/* glsl_legacy.c: legacy GLSL rewritten for glslang (NULL: no change), the fgl_Builtins block */
char* fgl_glsl_upgrade(const char* src, GLenum stage, int ntexcoords, int* uses_builtins);
int   fgl_glsl_texcoords(const char* src);
int   fgl_builtins_size(void);
void  fgl_builtins_fill(fgl_ctx* c, uint8_t* block);
int  fgl_get_misc(fgl_ctx* c, GLenum p, double* v);
void fgl_misc_init(fgl_ctx* c);
void fgl_misc_free(fgl_ctx* c);
fgl_sampler* fgl_sampler_get(fgl_ctx* c, GLuint name);

/* framebuffers (gl_fbo.c): point fatmap at the draw framebuffer; surfaces
 * of the read framebuffer; forget a surface about to be destroyed */
void        fgl_bind_draw(fgl_ctx* c);
fm_surface* fgl_read_color(fgl_ctx* c);
void        fgl_surface_gone(fgl_ctx* c, const fm_surface* s);
void        fgl_fbo_free(fgl_ctx* c);
void        fgl_texture_detach(fgl_ctx* c, GLuint name); /* from every framebuffer */
/* depth value 0..1 of a depth surface */
float fgl_depth_at(const fm_surface* s, int x, int y);

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
