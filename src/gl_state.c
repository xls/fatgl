/*
 * fatgl - context setup, errors, enables, clears, viewport, queries.
 */
#include "fgl.h"
#include <stdio.h>

static const float g_ident[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

void fgl_ctx_init(fgl_ctx* c)
{
    c->c3 = fm3d_create();
    c->ex = fm_executor_create(0); /* every core; NULL without threads */
    if (!c->c3) return;
    if (c->ex) {
        fm3d_set_executor(c->c3, c->ex);
        fm3d_set_deferred(c->c3, 1);
    }
    fm3d_set_clip_depth(c->c3, FM3D_DEPTH_NEG_ONE_ONE);
    fm3d_set_origin(c->c3, FM3D_ORIGIN_LOWER_LEFT); /* GL window coordinates: row 0 at the bottom */
    fm3d_blend_state off = { FM3D_BF_ONE, FM3D_BF_ZERO, FM3D_BF_ONE, FM3D_BF_ZERO, FM3D_BLEND_ADD, FM3D_BLEND_ADD, 0 };
    fm3d_set_blend_state(c->c3, &off); /* straight colors everywhere, as GL keeps them */
    c->depth_func   = GL_LESS;
    c->depth_mask   = GL_TRUE;
    c->cull_face    = GL_BACK;
    c->front_face   = GL_CCW;
    c->shade_model  = GL_SMOOTH;
    c->poly_mode[0] = c->poly_mode[1] = GL_FILL;
    c->blend_src    = c->blend_src_a = GL_ONE, c->blend_dst = c->blend_dst_a = GL_ZERO;
    c->blend_eq     = c->blend_eq_a = GL_FUNC_ADD;
    c->alpha_func   = GL_ALWAYS;
    c->clear_depth  = 1.0f;
    c->unpack_align = c->pack_align = 4;
    c->tex_env      = GL_MODULATE;
    c->enables |= FGL_E_DEBUG; /* GL_DEBUG_OUTPUT starts enabled */
    for (int u = 0; u < FGL_UNITS; u++) c->tex_envs[u] = GL_MODULATE;
    c->cur_tex1[3]  = 1;
    c->fog_mode = GL_EXP, c->fog_density = 1, c->fog_start = 0, c->fog_end = 1;
    c->matrix_mode  = GL_MODELVIEW;
    for (int i = 0; i < 4; i++) c->color_mask[i] = GL_TRUE;
    for (int m = 0; m < 3; m++) memcpy(&c->mstack[m][0], g_ident, sizeof(g_ident));
    /* GL defaults: lights point down -z, light 0 white, the rest black */
    for (int i = 0; i < FGL_LIGHTS; i++) {
        float* p = c->light[i].pos;
        p[0] = 0, p[1] = 0, p[2] = 1, p[3] = 0;
        c->light[i].amb[3] = 1;
        c->light[i].dif[3] = c->light[i].spe[3] = 1;
        if (i == 0) c->light[i].dif[0] = c->light[i].dif[1] = c->light[i].dif[2] = c->light[i].spe[0] = c->light[i].spe[1] = c->light[i].spe[2] = 1;
        c->light[i].spot_dir[2] = -1;
        c->light[i].spot_cut    = 180;
        c->light[i].att[0]      = 1;
    }
    c->light_model_ambient[0] = c->light_model_ambient[1] = c->light_model_ambient[2] = 0.2f, c->light_model_ambient[3] = 1;
    c->mat.amb[0] = c->mat.amb[1] = c->mat.amb[2] = 0.2f, c->mat.amb[3] = 1;
    c->mat.dif[0] = c->mat.dif[1] = c->mat.dif[2] = 0.8f, c->mat.dif[3] = 1;
    c->mat.spe[3] = c->mat.emi[3] = 1;
    c->colmat_mode = GL_AMBIENT_AND_DIFFUSE;
    c->cur_color[0] = c->cur_color[1] = c->cur_color[2] = c->cur_color[3] = 1;
    c->cur_normal[2] = 1;
    c->cur_tex[3]    = 1;
    fgl_misc_init(c);
}

void fgl_ctx_free(fgl_ctx* c)
{
    fgl_flush(c);
    for (int i = 0; i < c->ntex; i++) fgl_tex_free(c, &c->tex[i]);
    fgl_fbo_free(c);
    fgl_misc_free(c);
    fgl_overlay_free(c);
    fgl_arb_free(c);
    free(c->tex);
    for (int i = 0; i < c->nlists; i++) free(c->lists[i].ops);
    free(c->lists);
    free(c->imm);
    fm3d_destroy(c->c3);
    if (c->ex) fm_executor_destroy(c->ex);
    fm_surface_destroy(c->color);
    fm_surface_destroy(c->depth);
}

int fgl_resize(fgl_ctx* c, int w, int h)
{
    if (c->color && c->fbw == w && c->fbh == h) return 1;
    fgl_flush(c);
    fm_surface* col = fm_surface_create(w, h, FM_FORMAT_ARGB32);
    fm_surface* dep = fm_surface_create(w, h, FM_FORMAT_D24S8);
    if (!col || !dep) {
        fm_surface_destroy(col);
        fm_surface_destroy(dep);
        return 0;
    }
    fm3d_set_target(c->c3, col, dep); /* off the old surfaces (flushes) */
    fm3d_set_stencil_buffer(c->c3, NULL);
    fm_surface_destroy(c->color);
    fm_surface_destroy(c->depth);
    c->color = col, c->depth = dep, c->fbw = w, c->fbh = h;
    c->tgt_color = NULL; /* fgl_bind_draw picks the draw framebuffer */
    return 1;
}

void fgl_flush(fgl_ctx* c)
{
    if (c && c->c3) fm3d_flush(c->c3);
}

void fgl_error(GLenum e)
{
    fgl_ctx* c = fgl_cur();
    if (c && c->error == GL_NO_ERROR) c->error = e;
    if (c && c->dbg_cb) {
        char m[64];
        snprintf(m, sizeof(m), "GL error 0x%04X", (unsigned)e);
        fgl_debug(c, GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_ERROR, GL_DEBUG_SEVERITY_HIGH, m);
    }
}

GLenum APIENTRY glGetError(void)
{
    fgl_ctx* c = fgl_cur();
    if (!c) return GL_NO_ERROR;
    GLenum e = c->error;
    c->error = GL_NO_ERROR;
    return e;
}

/* ---- enables ---- */
static unsigned fgl_cap_bit(GLenum cap)
{
    switch (cap) {
    case GL_DEPTH_TEST: return FGL_E_DEPTH;
    case GL_CULL_FACE: return FGL_E_CULL;
    case GL_LIGHTING: return FGL_E_LIGHTING;
    case GL_TEXTURE_2D: return FGL_E_TEX2D;
    case GL_FOG: return FGL_E_FOG;
    case GL_BLEND: return FGL_E_BLEND;
    case GL_ALPHA_TEST: return FGL_E_ALPHA;
    case GL_COLOR_MATERIAL: return FGL_E_COLMAT;
    case GL_NORMALIZE: return FGL_E_NORMALIZE;
    case GL_SCISSOR_TEST: return FGL_E_SCISSOR;
    case GL_STENCIL_TEST: return FGL_E_STENCIL;
    case GL_PRIMITIVE_RESTART: return FGL_E_RESTART;
    case GL_PROGRAM_POINT_SIZE: return FGL_E_PSIZE;
    case GL_DEBUG_OUTPUT: return FGL_E_DEBUG;
    case GL_VERTEX_PROGRAM_ARB: return FGL_E_VP;
    case GL_FRAGMENT_PROGRAM_ARB: return FGL_E_FP;
    case GL_POLYGON_OFFSET_FILL: return FGL_E_POFFSET;
    default: return 0;
    }
}

static void fgl_set_cap(GLenum cap, int on)
{
    FGL_CTX_OR_RETURN(c);
    if (c->list_compiling && fgl_record(c, on ? FGL_OP_ENABLE : FGL_OP_DISABLE, cap, 0, NULL, 0)) return;
    if (cap >= GL_LIGHT0 && cap < GL_LIGHT0 + FGL_LIGHTS) {
        unsigned b = 1u << (cap - GL_LIGHT0);
        c->light_on = on ? c->light_on | b : c->light_on & ~b;
        return;
    }
    if (cap == GL_TEXTURE_2D) { /* per texture unit (unit 0 also in enables) */
        unsigned u = 1u << c->active_unit;
        c->tex2d_units = on ? c->tex2d_units | u : c->tex2d_units & ~u;
        if (c->active_unit != 0) return;
    }
    unsigned b = fgl_cap_bit(cap);
    if (!b) { /* dithering, smoothing, ...: accepted, not implemented */
        /* GL_TEXTURE_1D / 3D / CUBE_MAP / RECTANGLE (fixed function: 2D only), GL_TEXTURE_CUBE_MAP_SEAMLESS (always) */
        if (cap == GL_DITHER || cap == GL_LINE_SMOOTH || cap == GL_POLYGON_SMOOTH || cap == GL_MULTISAMPLE) return;
        return;
    }
    c->enables = on ? c->enables | b : c->enables & ~b;
}

void APIENTRY glEnable(GLenum cap) { fgl_set_cap(cap, 1); }
void APIENTRY glDisable(GLenum cap) { fgl_set_cap(cap, 0); }

GLboolean APIENTRY glIsEnabled(GLenum cap)
{
    fgl_ctx* c = fgl_cur();
    if (!c) return GL_FALSE;
    if (cap >= GL_LIGHT0 && cap < GL_LIGHT0 + FGL_LIGHTS) return (c->light_on >> (cap - GL_LIGHT0)) & 1 ? GL_TRUE : GL_FALSE;
    if (cap == GL_TEXTURE_2D) return (c->tex2d_units >> c->active_unit) & 1 ? GL_TRUE : GL_FALSE;
    unsigned b = fgl_cap_bit(cap);
    return (c->enables & b) ? GL_TRUE : GL_FALSE;
}

/* ---- framebuffer state ---- */
void APIENTRY glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    FGL_CTX_OR_RETURN(c);
    c->clear_color[0] = r, c->clear_color[1] = g, c->clear_color[2] = b, c->clear_color[3] = a;
}
void APIENTRY glClearDepth(GLdouble d)
{
    FGL_CTX_OR_RETURN(c);
    c->clear_depth = (float)d;
}
void APIENTRY glClearStencil(GLint s)
{
    FGL_CTX_OR_RETURN(c);
    c->clear_stencil = s;
}

static uint8_t fgl_u8(float v) { return (uint8_t)(v <= 0 ? 0 : (v >= 1 ? 255 : v * 255.0f + 0.5f)); }

void APIENTRY glClear(GLbitfield mask)
{
    FGL_CTX_OR_RETURN(c);
    if (c->in_begin) {
        fgl_error(GL_INVALID_OPERATION);
        return;
    }
    fgl_sync(c); /* scissor */
    if (mask & GL_COLOR_BUFFER_BIT)
        fm3d_clear_color(c->c3, FM_RGBA(fgl_u8(c->clear_color[0]), fgl_u8(c->clear_color[1]), fgl_u8(c->clear_color[2]),
                                        fgl_u8(c->clear_color[3])));
    if (mask & GL_DEPTH_BUFFER_BIT) fm3d_clear_depth(c->c3, c->clear_depth);
    if (mask & GL_STENCIL_BUFFER_BIT) fm3d_clear_stencil(c->c3, (uint8_t)c->clear_stencil);
}

void APIENTRY glViewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    FGL_CTX_OR_RETURN(c);
    if (w < 0 || h < 0) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    c->viewport[0] = x, c->viewport[1] = y, c->viewport[2] = w, c->viewport[3] = h;
}

void APIENTRY glScissor(GLint x, GLint y, GLsizei w, GLsizei h)
{
    FGL_CTX_OR_RETURN(c);
    if (w < 0 || h < 0) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    c->scissor[0] = x, c->scissor[1] = y, c->scissor[2] = w, c->scissor[3] = h;
}

void APIENTRY glDepthFunc(GLenum f)
{
    FGL_CTX_OR_RETURN(c);
    if (c->list_compiling && fgl_record(c, FGL_OP_DEPTHFUNC, f, 0, NULL, 0)) return;
    if (f < GL_NEVER || f > GL_ALWAYS) {
        fgl_error(GL_INVALID_ENUM);
        return;
    }
    c->depth_func = f;
}
void APIENTRY glDepthMask(GLboolean m)
{
    FGL_CTX_OR_RETURN(c);
    c->depth_mask = m;
}
void APIENTRY glDepthRange(GLdouble n, GLdouble f)
{
    FGL_CTX_OR_RETURN(c);
    fm3d_set_depth_range(c->c3, (float)n, (float)f);
}
void APIENTRY glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
    FGL_CTX_OR_RETURN(c);
    c->color_mask[0] = r, c->color_mask[1] = g, c->color_mask[2] = b, c->color_mask[3] = a;
}
void APIENTRY glCullFace(GLenum m)
{
    FGL_CTX_OR_RETURN(c);
    if (c->list_compiling && fgl_record(c, FGL_OP_CULLFACE, m, 0, NULL, 0)) return;
    c->cull_face = m;
}
void APIENTRY glFrontFace(GLenum m)
{
    FGL_CTX_OR_RETURN(c);
    if (c->list_compiling && fgl_record(c, FGL_OP_FRONTFACE, m, 0, NULL, 0)) return;
    c->front_face = m;
}
void APIENTRY glShadeModel(GLenum m)
{
    FGL_CTX_OR_RETURN(c);
    if (c->list_compiling && fgl_record(c, FGL_OP_SHADEMODEL, m, 0, NULL, 0)) return;
    c->shade_model = m;
}
void APIENTRY glBlendFunc(GLenum s, GLenum d)
{
    FGL_CTX_OR_RETURN(c);
    if (c->list_compiling && fgl_record(c, FGL_OP_BLENDFUNC, s, d, NULL, 0)) return;
    c->blend_src = c->blend_src_a = s, c->blend_dst = c->blend_dst_a = d;
}
void APIENTRY glBlendFuncSeparate(GLenum s, GLenum d, GLenum sa, GLenum da)
{
    FGL_CTX_OR_RETURN(c);
    c->blend_src = s, c->blend_dst = d, c->blend_src_a = sa, c->blend_dst_a = da;
}
void APIENTRY glBlendEquationSeparate(GLenum rgb, GLenum a)
{
    FGL_CTX_OR_RETURN(c);
    c->blend_eq = rgb, c->blend_eq_a = a;
}
void APIENTRY glBlendEquation(GLenum e) { glBlendEquationSeparate(e, e); }
void APIENTRY glBlendColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    FGL_CTX_OR_RETURN(c);
    c->blend_color[0] = r, c->blend_color[1] = g, c->blend_color[2] = b, c->blend_color[3] = a;
}
void APIENTRY glAlphaFunc(GLenum f, GLfloat ref)
{
    FGL_CTX_OR_RETURN(c);
    c->alpha_func = f, c->alpha_ref = ref;
}
void APIENTRY glPolygonOffset(GLfloat factor, GLfloat units)
{
    FGL_CTX_OR_RETURN(c);
    c->poly_factor = factor, c->poly_units = units;
}
void APIENTRY glPixelStorei(GLenum p, GLint v)
{
    FGL_CTX_OR_RETURN(c);
    if (p == GL_UNPACK_ALIGNMENT) c->unpack_align = v;
    else if (p == GL_UNPACK_ROW_LENGTH) c->unpack_row = v;
    else if (p == GL_PACK_ALIGNMENT) c->pack_align = v > 0 ? v : 1;
    else if (p == GL_UNPACK_SKIP_ROWS) c->unpack_skip_rows = v > 0 ? v : 0;
    else if (p == GL_UNPACK_SKIP_PIXELS) c->unpack_skip_pixels = v > 0 ? v : 0;
}
void APIENTRY glHint(GLenum target, GLenum mode) { (void)target, (void)mode; }
void APIENTRY glPolygonMode(GLenum face, GLenum mode)
{
    FGL_CTX_OR_RETURN(c);
    if ((face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) || (mode != GL_FILL && mode != GL_LINE && mode != GL_POINT)) {
        fgl_error(GL_INVALID_ENUM);
        return;
    }
    if (face != GL_BACK) c->poly_mode[0] = mode;
    if (face != GL_FRONT) c->poly_mode[1] = mode;
}

/* one mode per draw: the faces that survive culling (front unless only back faces do) */
GLenum fgl_polygon_mode(const fgl_ctx* c)
{
    if (c->poly_mode[0] == c->poly_mode[1]) return c->poly_mode[0];
    return (c->enables & FGL_E_CULL) && c->cull_face == GL_FRONT ? c->poly_mode[1] : c->poly_mode[0];
}

void APIENTRY glFlush(void)
{
    FGL_CTX_OR_RETURN(c);
    fgl_flush(c);
}
void APIENTRY glFinish(void)
{
    FGL_CTX_OR_RETURN(c);
    fgl_flush(c);
}

/* ---- the fatmap state of the GL state ---- */
/* GL keeps straight colors in its framebuffers and textures: fatmap's
 * straight color output merger with GL's factors and equations */
static fm3d_blend_factor fgl_factor(GLenum f)
{
    switch (f) {
    case GL_ZERO: return FM3D_BF_ZERO;
    case GL_SRC_COLOR: return FM3D_BF_SRC_COLOR;
    case GL_ONE_MINUS_SRC_COLOR: return FM3D_BF_ONE_MINUS_SRC_COLOR;
    case GL_DST_COLOR: return FM3D_BF_DST_COLOR;
    case GL_ONE_MINUS_DST_COLOR: return FM3D_BF_ONE_MINUS_DST_COLOR;
    case GL_SRC_ALPHA: return FM3D_BF_SRC_ALPHA;
    case GL_ONE_MINUS_SRC_ALPHA: return FM3D_BF_ONE_MINUS_SRC_ALPHA;
    case GL_DST_ALPHA: return FM3D_BF_DST_ALPHA;
    case GL_ONE_MINUS_DST_ALPHA: return FM3D_BF_ONE_MINUS_DST_ALPHA;
    case GL_CONSTANT_COLOR: return FM3D_BF_CONSTANT_COLOR;
    case GL_ONE_MINUS_CONSTANT_COLOR: return FM3D_BF_ONE_MINUS_CONSTANT_COLOR;
    case GL_CONSTANT_ALPHA: return FM3D_BF_CONSTANT_ALPHA;
    case GL_ONE_MINUS_CONSTANT_ALPHA: return FM3D_BF_ONE_MINUS_CONSTANT_ALPHA;
    case GL_SRC_ALPHA_SATURATE: return FM3D_BF_SRC_ALPHA_SATURATE;
    default: return FM3D_BF_ONE;
    }
}
static fm3d_blend_eq fgl_equation(GLenum e)
{
    switch (e) {
    case GL_FUNC_SUBTRACT: return FM3D_BLEND_SUBTRACT;
    case GL_FUNC_REVERSE_SUBTRACT: return FM3D_BLEND_REVERSE_SUBTRACT;
    case GL_MIN: return FM3D_BLEND_MIN;
    case GL_MAX: return FM3D_BLEND_MAX;
    default: return FM3D_BLEND_ADD;
    }
}

static void fgl_sync_blend(fgl_ctx* c)
{
    fm3d_blend_state b = { FM3D_BF_ONE, FM3D_BF_ZERO, FM3D_BF_ONE, FM3D_BF_ZERO, FM3D_BLEND_ADD, FM3D_BLEND_ADD, 0 };
    if (c->enables & FGL_E_BLEND) {
        b.src_rgb = fgl_factor(c->blend_src), b.dst_rgb = fgl_factor(c->blend_dst);
        b.src_alpha = fgl_factor(c->blend_src_a), b.dst_alpha = fgl_factor(c->blend_dst_a);
        b.eq_rgb = fgl_equation(c->blend_eq), b.eq_alpha = fgl_equation(c->blend_eq_a);
        b.constant = FM_RGBA(fgl_u8(c->blend_color[0]), fgl_u8(c->blend_color[1]), fgl_u8(c->blend_color[2]),
                             fgl_u8(c->blend_color[3]));
    }
    fm3d_set_blend_state(c->c3, &b);
}

void fgl_sync(fgl_ctx* c)
{
    fm3d_ctx* f = c->c3;
    fgl_bind_draw(c); /* the draw framebuffer (resets fatmap's viewport + scissor when it changes) */
    /* GL window coordinates; fatmap counts rows bottom up as well (FM3D_ORIGIN_LOWER_LEFT) */
    fm3d_set_viewport(f, c->viewport[0], c->viewport[1], c->viewport[2], c->viewport[3]);
    fm3d_set_scissor(f, (c->enables & FGL_E_SCISSOR) != 0, c->scissor[0], c->scissor[1], c->scissor[2], c->scissor[3]);
    fm3d_set_projection(f, &c->mstack[FGL_PROJ][c->msp[FGL_PROJ]]);
    fm_mat4 id;
    memcpy(&id, g_ident, sizeof(id));
    fm3d_set_view(f, &id); /* GL's modelview: fatmap model, identity view (lighting in eye space) */
    fm3d_set_model(f, &c->mstack[FGL_MV][c->msp[FGL_MV]]);
    fm3d_set_depth_test(f, (c->enables & FGL_E_DEPTH) ? (fm3d_compare)(c->depth_func - GL_NEVER) : FM3D_ALWAYS,
                        (c->enables & FGL_E_DEPTH) && c->depth_mask);
    fm3d_cull cull = FM3D_CULL_NONE;
    if (c->enables & FGL_E_CULL)
        cull = c->cull_face == GL_FRONT ? FM3D_CULL_FRONT : (c->cull_face == GL_FRONT_AND_BACK ? FM3D_CULL_FRONT_AND_BACK : FM3D_CULL_BACK);
    fm3d_set_cull(f, cull, c->front_face == GL_CW ? FM3D_FRONT_CW : FM3D_FRONT_CCW);
    fgl_sync_blend(c);
    fm3d_set_color_write(f, c->color_mask[0] || c->color_mask[1] || c->color_mask[2] || c->color_mask[3]);
    fm3d_set_alpha_test(f, (c->enables & FGL_E_ALPHA) ? (fm3d_compare)(c->alpha_func - GL_NEVER) : FM3D_ALWAYS, c->alpha_ref);
    if (c->enables & FGL_E_POFFSET) fm3d_set_depth_bias(f, c->poly_factor, c->poly_units);
    else fm3d_set_depth_bias(f, 0, 0);
    fgl_sync_stencil(c);
    fgl_bind_draw(c); /* again: depth only framebuffers turn color writes off */
}

/* ---- queries ---- */
static char g_renderer[96];

static const char* fgl_ext_string(void);

const GLubyte* APIENTRY glGetString(GLenum name)
{
    fgl_ctx* c = fgl_cur();
    switch (name) {
    case GL_VENDOR: return (const GLubyte*)"fatgl";
    case GL_RENDERER:
        snprintf(g_renderer, sizeof(g_renderer), "fatgl on fatmap %s (%s, %d threads)", fm_version_string(), fm_simd_name(fm_simd_best()),
                 c && c->ex ? c->ex->workers : 1);
        return (const GLubyte*)g_renderer;
    case GL_VERSION: return (const GLubyte*)(c && c->core ? "3.3.0 Core Profile fatgl 0.1.0" : "3.3.0 fatgl 0.1.0");
    case GL_SHADING_LANGUAGE_VERSION: return (const GLubyte*)"3.30 fatgl (glslang)";
    case GL_PROGRAM_ERROR_STRING_ARB: return (const GLubyte*)(c ? fgl_arb_error_string(c) : "");
    case GL_EXTENSIONS:
        if (c && c->core) {
            fgl_error(GL_INVALID_ENUM); /* core profile: glGetStringi */
            return NULL;
        }
        return (const GLubyte*)fgl_ext_string();
    default: fgl_error(GL_INVALID_ENUM); return NULL;
    }
}

static const char* g_ext[] = {
    /* GL 1.x era (older games look for these in GL_EXTENSIONS) */
    "GL_ARB_multitexture", "GL_EXT_texture_env_add", "GL_ARB_texture_env_add", "GL_EXT_bgra", "GL_EXT_texture_edge_clamp",
    "GL_SGIS_texture_edge_clamp", "GL_ARB_texture_compression", "GL_EXT_texture_compression_s3tc", "GL_EXT_packed_pixels",
    "GL_EXT_blend_color", "GL_EXT_blend_minmax", "GL_EXT_blend_subtract", "GL_EXT_blend_func_separate", "GL_EXT_stencil_wrap",
    "GL_ARB_texture_non_power_of_two", "GL_ARB_depth_texture", "GL_ARB_point_sprite", "GL_ARB_vertex_buffer_object",
    "GL_ARB_pixel_buffer_object", "GL_ARB_occlusion_query", "GL_ARB_occlusion_query2", "GL_ARB_shader_objects",
    "GL_ARB_vertex_shader", "GL_ARB_fragment_shader", "GL_ARB_shading_language_100", "GL_EXT_framebuffer_object",
    "GL_EXT_framebuffer_blit", "GL_EXT_packed_depth_stencil",
    /* GL 3.x */
    "GL_ARB_framebuffer_object", "GL_ARB_vertex_array_object", "GL_ARB_uniform_buffer_object", "GL_ARB_draw_instanced",
    "GL_ARB_instanced_arrays", "GL_ARB_draw_elements_base_vertex", "GL_ARB_map_buffer_range", "GL_ARB_copy_buffer",
    "GL_ARB_half_float_vertex", "GL_ARB_half_float_pixel", "GL_ARB_texture_compression_rgtc", "GL_ARB_sync", "GL_ARB_timer_query",
    "GL_ARB_sampler_objects", "GL_ARB_vertex_type_2_10_10_10_rev", "GL_ARB_explicit_attrib_location",
    "GL_EXT_texture_filter_anisotropic", "GL_EXT_texture_lod_bias", "GL_ARB_debug_output", "GL_KHR_debug",
    "GL_ARB_vertex_program", "GL_ARB_fragment_program", "GL_ARB_texture_cube_map", "GL_EXT_texture_cube_map", "GL_EXT_texture3D",
    "GL_ARB_texture_rectangle", "GL_EXT_texture_rectangle", "GL_NV_texture_rectangle", "GL_EXT_texture_array",
    "GL_ARB_seamless_cube_map", "GL_ARB_texture_swizzle", "GL_EXT_texture_swizzle" };
#define FGL_NEXT ((int)(sizeof(g_ext) / sizeof(g_ext[0])))

/* the GL_EXTENSIONS string (compatibility contexts), built once */
static const char* fgl_ext_string(void)
{
    static char buf[2048];
    if (!buf[0]) {
        size_t n = 0;
        for (int i = 0; i < FGL_NEXT; i++) {
            size_t l = strlen(g_ext[i]);
            if (n + l + 2 >= sizeof(buf)) break;
            memcpy(buf + n, g_ext[i], l), n += l, buf[n++] = ' ';
        }
        if (n) buf[n - 1] = 0;
    }
    return buf;
}

const GLubyte* APIENTRY glGetStringi(GLenum name, GLuint i)
{
    if (name != GL_EXTENSIONS || i >= (GLuint)FGL_NEXT) {
        fgl_error(name != GL_EXTENSIONS ? GL_INVALID_ENUM : GL_INVALID_VALUE);
        return NULL;
    }
    return (const GLubyte*)g_ext[i];
}

static int fgl_get_count(GLenum p, fgl_ctx* c, double* v)
{
    switch (p) {
    case GL_MAJOR_VERSION: v[0] = 3; return 1;
    case GL_MINOR_VERSION: v[0] = 3; return 1;
    case GL_NUM_EXTENSIONS: v[0] = FGL_NEXT; return 1;
    case GL_CONTEXT_PROFILE_MASK: v[0] = c->core ? GL_CONTEXT_CORE_PROFILE_BIT : GL_CONTEXT_COMPATIBILITY_PROFILE_BIT; return 1;
    case GL_CONTEXT_FLAGS: v[0] = 0; return 1;
    case GL_MAX_VERTEX_ATTRIBS: v[0] = FGL_ATTRIBS; return 1;
    case GL_MAX_TEXTURE_IMAGE_UNITS: v[0] = FM3D_MAX_TEXTURE_UNITS; return 1;
    case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS: v[0] = FGL_UNITS; return 1;
    case GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS: v[0] = FM3D_MAX_TEXTURE_UNITS; return 1;
    case GL_MAX_UNIFORM_BUFFER_BINDINGS: v[0] = FGL_UBO_BINDS; return 1;
    case GL_MAX_UNIFORM_BLOCK_SIZE: v[0] = 65536; return 1;
    case GL_MAX_VERTEX_UNIFORM_BLOCKS: case GL_MAX_FRAGMENT_UNIFORM_BLOCKS: v[0] = 12; return 1;
    case GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT: v[0] = 16; return 1;
    case GL_MAX_VERTEX_UNIFORM_COMPONENTS: case GL_MAX_FRAGMENT_UNIFORM_COMPONENTS: v[0] = 16384; return 1;
    case GL_MAX_VARYING_COMPONENTS: case GL_MAX_VERTEX_OUTPUT_COMPONENTS: case GL_MAX_FRAGMENT_INPUT_COMPONENTS:
        v[0] = FM3D_MAX_SHADER_VARYINGS;
        return 1;
    case GL_MAX_DRAW_BUFFERS: v[0] = 1; return 1;
    case GL_MAX_COLOR_ATTACHMENTS: v[0] = FGL_COLOR_ATTACHMENTS; return 1;
    case GL_MAX_RENDERBUFFER_SIZE: v[0] = 8192; return 1;
    case GL_MAX_ELEMENTS_VERTICES: case GL_MAX_ELEMENTS_INDICES: v[0] = 1 << 24; return 1;
    case GL_CURRENT_PROGRAM: v[0] = c->program; return 1;
    case GL_VERTEX_ARRAY_BINDING: v[0] = c->vao; return 1;
    case GL_ARRAY_BUFFER_BINDING: v[0] = c->array_buffer; return 1;
    case GL_ELEMENT_ARRAY_BUFFER_BINDING: v[0] = fgl_cur_vao(c)->elements; return 1;
    case GL_UNIFORM_BUFFER_BINDING: v[0] = c->uniform_buffer; return 1;
    case GL_ACTIVE_TEXTURE: v[0] = GL_TEXTURE0 + c->active_unit; return 1;
    case GL_DRAW_FRAMEBUFFER_BINDING: v[0] = c->draw_fbo; return 1;
    case GL_READ_FRAMEBUFFER_BINDING: v[0] = c->read_fbo; return 1;
    case GL_RENDERBUFFER_BINDING: v[0] = c->renderbuffer; return 1;
    case GL_BLEND_SRC_RGB: v[0] = c->blend_src; return 1;
    case GL_BLEND_SRC_ALPHA: v[0] = c->blend_src_a; return 1;
    case GL_BLEND_DST_RGB: v[0] = c->blend_dst; return 1;
    case GL_BLEND_DST_ALPHA: v[0] = c->blend_dst_a; return 1;
    case GL_BLEND_EQUATION_RGB: v[0] = c->blend_eq; return 1;
    case GL_BLEND_EQUATION_ALPHA: v[0] = c->blend_eq_a; return 1;
    case GL_BLEND_COLOR: for (int i = 0; i < 4; i++) v[i] = c->blend_color[i]; return 4;
    case GL_PACK_ALIGNMENT: v[0] = c->pack_align; return 1;
    case GL_VIEWPORT: for (int i = 0; i < 4; i++) v[i] = c->viewport[i]; return 4;
    case GL_SCISSOR_BOX: for (int i = 0; i < 4; i++) v[i] = c->scissor[i]; return 4;
    case GL_COLOR_CLEAR_VALUE: for (int i = 0; i < 4; i++) v[i] = c->clear_color[i]; return 4;
    case GL_CURRENT_COLOR: for (int i = 0; i < 4; i++) v[i] = c->cur_color[i]; return 4;
    case GL_DEPTH_CLEAR_VALUE: v[0] = c->clear_depth; return 1;
    case GL_MAX_TEXTURE_SIZE: v[0] = 8192; return 1;
    case GL_MAX_LIGHTS: v[0] = FGL_LIGHTS; return 1;
    case GL_MAX_MODELVIEW_STACK_DEPTH: case GL_MAX_PROJECTION_STACK_DEPTH: v[0] = FGL_STACK; return 1;
    case GL_MAX_LIST_NESTING: v[0] = 64; return 1;
    case GL_MAX_VIEWPORT_DIMS: v[0] = v[1] = 16384; return 2;
    case GL_MATRIX_MODE: v[0] = c->matrix_mode; return 1;
    case GL_SHADE_MODEL: v[0] = c->shade_model; return 1;
    case GL_DEPTH_FUNC: v[0] = c->depth_func; return 1;
    case GL_DEPTH_WRITEMASK: v[0] = c->depth_mask; return 1;
    case GL_CULL_FACE_MODE: v[0] = c->cull_face; return 1;
    case GL_POLYGON_MODE: v[0] = c->poly_mode[0], v[1] = c->poly_mode[1]; return 2;
    case GL_FRONT_FACE: v[0] = c->front_face; return 1;
    case GL_RED_BITS: case GL_GREEN_BITS: case GL_BLUE_BITS: case GL_ALPHA_BITS: v[0] = 8; return 1;
    case GL_DEPTH_BITS: v[0] = 24; return 1;
    case GL_STENCIL_BITS: v[0] = 8; return 1;
    case GL_TEXTURE_BINDING_2D: v[0] = FGL_BOUND_TEX(c); return 1;
    case GL_TEXTURE_BINDING_1D: v[0] = c->unit_bind[FGL_TT_1D][c->active_unit]; return 1;
    case GL_TEXTURE_BINDING_3D: v[0] = c->unit_bind[FGL_TT_3D][c->active_unit]; return 1;
    case GL_TEXTURE_BINDING_CUBE_MAP: v[0] = c->unit_bind[FGL_TT_CUBE][c->active_unit]; return 1;
    case GL_TEXTURE_BINDING_RECTANGLE: v[0] = c->unit_bind[FGL_TT_RECT][c->active_unit]; return 1;
    case GL_TEXTURE_BINDING_1D_ARRAY: v[0] = c->unit_bind[FGL_TT_1D_ARRAY][c->active_unit]; return 1;
    case GL_TEXTURE_BINDING_2D_ARRAY: v[0] = c->unit_bind[FGL_TT_2D_ARRAY][c->active_unit]; return 1;
    case GL_MAX_RECTANGLE_TEXTURE_SIZE: v[0] = 8192; return 1;
    case GL_UNPACK_ALIGNMENT: v[0] = c->unpack_align; return 1;
    case GL_MODELVIEW_MATRIX: case GL_PROJECTION_MATRIX: case GL_TEXTURE_MATRIX: {
        int      m = p == GL_MODELVIEW_MATRIX ? FGL_MV : (p == GL_PROJECTION_MATRIX ? FGL_PROJ : FGL_TEXM);
        const float* f = &c->mstack[m][c->msp[m]].c[0].x;
        for (int i = 0; i < 16; i++) v[i] = f[i];
        return 16;
    }
    default: {
        int n = fgl_get_misc(c, p, v);
        if (n) return n;
        unsigned b = fgl_cap_bit(p);
        if (b) {
            v[0] = (c->enables & b) != 0;
            return 1;
        }
        return 0;
    }
    }
}

void APIENTRY glGetDoublev(GLenum p, GLdouble* out)
{
    FGL_CTX_OR_RETURN(c);
    double v[16];
    int    n = fgl_get_count(p, c, v);
    if (!n) fgl_error(GL_INVALID_ENUM);
    for (int i = 0; i < n; i++) out[i] = v[i];
}
void APIENTRY glGetFloatv(GLenum p, GLfloat* out)
{
    FGL_CTX_OR_RETURN(c);
    double v[16];
    int    n = fgl_get_count(p, c, v);
    if (!n) fgl_error(GL_INVALID_ENUM);
    for (int i = 0; i < n; i++) out[i] = (float)v[i];
}
void APIENTRY glGetIntegerv(GLenum p, GLint* out)
{
    FGL_CTX_OR_RETURN(c);
    double v[16];
    int    n = fgl_get_count(p, c, v);
    if (!n) fgl_error(GL_INVALID_ENUM);
    for (int i = 0; i < n; i++) out[i] = (GLint)v[i];
}
void APIENTRY glGetBooleanv(GLenum p, GLboolean* out)
{
    FGL_CTX_OR_RETURN(c);
    double v[16];
    int    n = fgl_get_count(p, c, v);
    if (!n) fgl_error(GL_INVALID_ENUM);
    for (int i = 0; i < n; i++) out[i] = v[i] != 0 ? GL_TRUE : GL_FALSE;
}

void APIENTRY glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type, void* out)
{
    FGL_CTX_OR_RETURN(c);
    if (w < 0 || h < 0) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    fgl_flush(c);
    int bpp = fgl_pixel_bytes(fmt, type);
    out     = fgl_pack_ptr(c, out);
    if (fmt == GL_DEPTH_COMPONENT && out) { /* depth of the read framebuffer as GL_FLOAT / unsigned types */
        fm_surface* d = fgl_read_depth(c);
        if (!d) {
            fgl_error(GL_INVALID_OPERATION);
            return;
        }
        size_t es = type == GL_FLOAT || type == GL_UNSIGNED_INT ? 4 : (type == GL_UNSIGNED_SHORT ? 2 : 1);
        size_t stride = ((size_t)w * es + (size_t)c->pack_align - 1) / (size_t)c->pack_align * (size_t)c->pack_align;
        for (int r = 0; r < h; r++)
            for (int i = 0; i < w; i++) {
                int     sx = x + i, sy = y + r;
                float   z  = sx >= 0 && sy >= 0 && sx < d->width && sy < d->height ? fgl_depth_at(d, sx, sy) : 1.0f;
                uint8_t* o  = (uint8_t*)out + (size_t)r * stride + (size_t)i * es;
                if (type == GL_FLOAT) memcpy(o, &z, 4);
                else if (type == GL_UNSIGNED_INT) { uint32_t v = (uint32_t)((double)z * 4294967295.0); memcpy(o, &v, 4); }
                else if (type == GL_UNSIGNED_SHORT) { uint16_t v = (uint16_t)(z * 65535.0f + 0.5f); memcpy(o, &v, 2); }
                else *o = (uint8_t)(z * 255.0f + 0.5f);
            }
        return;
    }
    fm_surface* src = fgl_read_color(c);
    if (!src || !bpp || !out) {
        if (!bpp) fgl_unimplemented("glReadPixels (this format / type)");
        else fgl_error(GL_INVALID_OPERATION);
        return;
    }
    size_t stride = ((size_t)w * (size_t)bpp + (size_t)c->pack_align - 1) / (size_t)c->pack_align * (size_t)c->pack_align;
    uint8_t* tmp  = (uint8_t*)malloc((size_t)w * 4);
    if (!tmp) return;
    for (int r = 0; r < h; r++) {
        int sy = y + r; /* rows bottom up, as GL returns them */
        for (int i = 0; i < w; i++) {
            int      sx = x + i;
            uint32_t p  = sx >= 0 && sx < src->width && sy >= 0 && sy < src->height ? fm_surface_row32(src, sy)[sx] : 0; /* straight */
            tmp[4 * i] = (uint8_t)(p >> 16), tmp[4 * i + 1] = (uint8_t)(p >> 8), tmp[4 * i + 2] = (uint8_t)p, tmp[4 * i + 3] = (uint8_t)(p >> 24);
        }
        fgl_rgba8_to_pixels(fmt, type, tmp, w, (uint8_t*)out + (size_t)r * stride);
    }
    free(tmp);
}
