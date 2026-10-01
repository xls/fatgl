/*
 * fatgl - lights and materials (GL 1.x fixed function).
 */
#include "fgl.h"

static void fgl_xform_point(const fm_mat4* m, const float* in, float* out)
{
    fm_vec4 r = fm_mat4_mul_vec4(*m, fm_v4(in[0], in[1], in[2], in[3]));
    out[0] = r.x, out[1] = r.y, out[2] = r.z, out[3] = r.w;
}

static int fgl_light_params(GLenum p) /* values per parameter */
{
    switch (p) {
    case GL_AMBIENT: case GL_DIFFUSE: case GL_SPECULAR: case GL_POSITION: return 4;
    case GL_SPOT_DIRECTION: return 3;
    case GL_SPOT_EXPONENT: case GL_SPOT_CUTOFF: case GL_CONSTANT_ATTENUATION: case GL_LINEAR_ATTENUATION:
    case GL_QUADRATIC_ATTENUATION: return 1;
    default: return 0;
    }
}

void APIENTRY glLightfv(GLenum light, GLenum p, const GLfloat* v)
{
    FGL_CTX_OR_RETURN(c);
    int n = fgl_light_params(p);
    if (c->list_compiling && fgl_record(c, FGL_OP_LIGHT, light, p, v, n)) return;
    int i = (int)light - GL_LIGHT0;
    if (i < 0 || i >= FGL_LIGHTS || !n) {
        fgl_error(GL_INVALID_ENUM);
        return;
    }
    const fm_mat4* mv = &c->mstack[FGL_MV][c->msp[FGL_MV]];
    switch (p) {
    case GL_AMBIENT: memcpy(c->light[i].amb, v, 16); break;
    case GL_DIFFUSE: memcpy(c->light[i].dif, v, 16); break;
    case GL_SPECULAR: memcpy(c->light[i].spe, v, 16); break;
    case GL_POSITION: fgl_xform_point(mv, v, c->light[i].pos); break; /* eye space, as GL stores it */
    case GL_SPOT_DIRECTION: {
        float d[4] = { v[0], v[1], v[2], 0.0f }, o[4];
        fgl_xform_point(mv, d, o);
        memcpy(c->light[i].spot_dir, o, 12);
        break;
    }
    case GL_SPOT_EXPONENT: c->light[i].spot_exp = v[0]; break;
    case GL_SPOT_CUTOFF: c->light[i].spot_cut = v[0]; break;
    case GL_CONSTANT_ATTENUATION: c->light[i].att[0] = v[0]; break;
    case GL_LINEAR_ATTENUATION: c->light[i].att[1] = v[0]; break;
    default: c->light[i].att[2] = v[0]; break;
    }
}
void APIENTRY glLightf(GLenum light, GLenum p, GLfloat v)
{
    float f[4] = { v, 0, 0, 0 };
    glLightfv(light, p, f);
}
void APIENTRY glLighti(GLenum light, GLenum p, GLint v) { glLightf(light, p, (float)v); }
void APIENTRY glLightiv(GLenum light, GLenum p, const GLint* v)
{
    float f[4] = { 0, 0, 0, 0 };
    int   n    = fgl_light_params(p);
    for (int i = 0; i < n; i++) f[i] = (p == GL_AMBIENT || p == GL_DIFFUSE || p == GL_SPECULAR) ? (float)v[i] / 2147483647.0f : (float)v[i];
    glLightfv(light, p, f);
}

void APIENTRY glLightModelfv(GLenum p, const GLfloat* v)
{
    FGL_CTX_OR_RETURN(c);
    if (c->list_compiling && fgl_record(c, FGL_OP_LIGHTMODEL, p, 0, v, p == GL_LIGHT_MODEL_AMBIENT ? 4 : 1)) return;
    if (p == GL_LIGHT_MODEL_AMBIENT) memcpy(c->light_model_ambient, v, 16);
    /* local viewer, two sided lighting: accepted, not modelled */
}
void APIENTRY glLightModelf(GLenum p, GLfloat v)
{
    float f[4] = { v, 0, 0, 0 };
    glLightModelfv(p, f);
}
void APIENTRY glLightModeli(GLenum p, GLint v) { glLightModelf(p, (float)v); }

void APIENTRY glMaterialfv(GLenum face, GLenum p, const GLfloat* v)
{
    FGL_CTX_OR_RETURN(c);
    int n = p == GL_SHININESS ? 1 : 4;
    if (c->list_compiling && fgl_record(c, FGL_OP_MATERIAL, face, p, v, n)) return;
    /* one material for both faces (fatmap lights front faces) */
    switch (p) {
    case GL_AMBIENT: memcpy(c->mat.amb, v, 16); break;
    case GL_DIFFUSE: memcpy(c->mat.dif, v, 16); break;
    case GL_AMBIENT_AND_DIFFUSE: memcpy(c->mat.amb, v, 16), memcpy(c->mat.dif, v, 16); break;
    case GL_SPECULAR: memcpy(c->mat.spe, v, 16); break;
    case GL_EMISSION: memcpy(c->mat.emi, v, 16); break;
    case GL_SHININESS: c->mat.shin = v[0]; break;
    default: fgl_error(GL_INVALID_ENUM); break;
    }
}
void APIENTRY glMaterialf(GLenum face, GLenum p, GLfloat v)
{
    float f[4] = { v, 0, 0, 0 };
    glMaterialfv(face, p, f);
}
void APIENTRY glMateriali(GLenum face, GLenum p, GLint v) { glMaterialf(face, p, (float)v); }

void APIENTRY glColorMaterial(GLenum face, GLenum mode)
{
    FGL_CTX_OR_RETURN(c);
    if (c->list_compiling && fgl_record(c, FGL_OP_COLORMATERIAL, face, mode, NULL, 0)) return;
    c->colmat_mode = mode;
    if (mode != GL_AMBIENT_AND_DIFFUSE && mode != GL_DIFFUSE) fgl_unimplemented("glColorMaterial (this mode)");
}
