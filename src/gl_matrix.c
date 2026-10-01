/*
 * fatgl - the matrix stacks (modelview, projection, texture).
 */
#include "fgl.h"
#include <math.h>

static int fgl_mode_index(GLenum m) { return m == GL_PROJECTION ? FGL_PROJ : (m == GL_TEXTURE ? FGL_TEXM : FGL_MV); }

static fm_mat4* fgl_top(fgl_ctx* c)
{
    int m = fgl_mode_index(c->matrix_mode);
    return &c->mstack[m][c->msp[m]];
}

static void fgl_mul(fgl_ctx* c, const fm_mat4* r) /* top = top * r (GL post multiplies) */
{
    fm_mat4* t = fgl_top(c);
    *t         = fm_mat4_mul(*t, *r);
}

void APIENTRY glMatrixMode(GLenum mode)
{
    FGL_CTX_OR_RETURN(c);
    if (c->list_compiling && fgl_record(c, FGL_OP_MATRIXMODE, mode, 0, NULL, 0)) return;
    if (mode != GL_MODELVIEW && mode != GL_PROJECTION && mode != GL_TEXTURE) {
        fgl_error(GL_INVALID_ENUM);
        return;
    }
    c->matrix_mode = mode;
}

void APIENTRY glLoadIdentity(void)
{
    FGL_CTX_OR_RETURN(c);
    if (c->list_compiling && fgl_record(c, FGL_OP_LOADIDENTITY, 0, 0, NULL, 0)) return;
    *fgl_top(c) = fm_mat4_identity();
}

void APIENTRY glLoadMatrixf(const GLfloat* m)
{
    FGL_CTX_OR_RETURN(c);
    if (c->list_compiling && fgl_record(c, FGL_OP_LOADMATRIX, 0, 0, m, 16)) return;
    memcpy(fgl_top(c), m, 16 * sizeof(float));
}

void APIENTRY glLoadMatrixd(const GLdouble* m)
{
    float f[16];
    for (int i = 0; i < 16; i++) f[i] = (float)m[i];
    glLoadMatrixf(f);
}

void APIENTRY glMultMatrixf(const GLfloat* m)
{
    FGL_CTX_OR_RETURN(c);
    if (c->list_compiling && fgl_record(c, FGL_OP_MULTMATRIX, 0, 0, m, 16)) return;
    fm_mat4 r;
    memcpy(&r, m, sizeof(r));
    fgl_mul(c, &r);
}

void APIENTRY glMultMatrixd(const GLdouble* m)
{
    float f[16];
    for (int i = 0; i < 16; i++) f[i] = (float)m[i];
    glMultMatrixf(f);
}

void APIENTRY glPushMatrix(void)
{
    FGL_CTX_OR_RETURN(c);
    if (c->list_compiling && fgl_record(c, FGL_OP_PUSHMATRIX, 0, 0, NULL, 0)) return;
    int m = fgl_mode_index(c->matrix_mode);
    if (c->msp[m] + 1 >= FGL_STACK) {
        fgl_error(GL_STACK_OVERFLOW);
        return;
    }
    c->mstack[m][c->msp[m] + 1] = c->mstack[m][c->msp[m]];
    c->msp[m]++;
}

void APIENTRY glPopMatrix(void)
{
    FGL_CTX_OR_RETURN(c);
    if (c->list_compiling && fgl_record(c, FGL_OP_POPMATRIX, 0, 0, NULL, 0)) return;
    int m = fgl_mode_index(c->matrix_mode);
    if (c->msp[m] == 0) {
        fgl_error(GL_STACK_UNDERFLOW);
        return;
    }
    c->msp[m]--;
}

void APIENTRY glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    FGL_CTX_OR_RETURN(c);
    float a[4] = { angle, x, y, z };
    if (c->list_compiling && fgl_record(c, FGL_OP_ROTATE, 0, 0, a, 4)) return;
    float l = sqrtf(x * x + y * y + z * z);
    if (l == 0.0f) return;
    fm_mat4 r = fm_rotate(fm_mat4_identity(), fm_radians(angle), fm_v3(x / l, y / l, z / l));
    fgl_mul(c, &r);
}
void APIENTRY glRotated(GLdouble a, GLdouble x, GLdouble y, GLdouble z) { glRotatef((float)a, (float)x, (float)y, (float)z); }

void APIENTRY glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    FGL_CTX_OR_RETURN(c);
    float a[3] = { x, y, z };
    if (c->list_compiling && fgl_record(c, FGL_OP_TRANSLATE, 0, 0, a, 3)) return;
    fm_mat4 r = fm_translate(fm_mat4_identity(), fm_v3(x, y, z));
    fgl_mul(c, &r);
}
void APIENTRY glTranslated(GLdouble x, GLdouble y, GLdouble z) { glTranslatef((float)x, (float)y, (float)z); }

void APIENTRY glScalef(GLfloat x, GLfloat y, GLfloat z)
{
    FGL_CTX_OR_RETURN(c);
    float a[3] = { x, y, z };
    if (c->list_compiling && fgl_record(c, FGL_OP_SCALE, 0, 0, a, 3)) return;
    fm_mat4 r = fm_scale(fm_mat4_identity(), fm_v3(x, y, z));
    fgl_mul(c, &r);
}
void APIENTRY glScaled(GLdouble x, GLdouble y, GLdouble z) { glScalef((float)x, (float)y, (float)z); }

void APIENTRY glFrustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    FGL_CTX_OR_RETURN(c);
    if (n <= 0 || f <= 0 || n == f || l == r || b == t) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    float m[16];
    memset(m, 0, sizeof(m));
    m[0]  = (float)(2 * n / (r - l));
    m[5]  = (float)(2 * n / (t - b));
    m[8]  = (float)((r + l) / (r - l));
    m[9]  = (float)((t + b) / (t - b));
    m[10] = (float)(-(f + n) / (f - n));
    m[11] = -1;
    m[14] = (float)(-2 * f * n / (f - n));
    glMultMatrixf(m);
}

void APIENTRY glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    FGL_CTX_OR_RETURN(c);
    if (l == r || b == t || n == f) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    float m[16];
    memset(m, 0, sizeof(m));
    m[0]  = (float)(2 / (r - l));
    m[5]  = (float)(2 / (t - b));
    m[10] = (float)(-2 / (f - n));
    m[12] = (float)(-(r + l) / (r - l));
    m[13] = (float)(-(t + b) / (t - b));
    m[14] = (float)(-(f + n) / (f - n));
    m[15] = 1;
    glMultMatrixf(m);
}
