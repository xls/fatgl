/* fatgl example: GL blending (OpenGL 1.4 / 2.0 functions through
 * wglGetProcAddress). A striped background and one quad per blend mode:
 * alpha, additive, multiply, glBlendColor constant, reverse subtract, min,
 * max, and a white texture with a straight alpha ramp (premultiplied
 * storage would darken it). */
#include "win.h"
#include <math.h>

#define GL_FUNC_ADD              0x8006
#define GL_MIN                   0x8007
#define GL_MAX                   0x8008
#define GL_FUNC_REVERSE_SUBTRACT 0x800B
#define GL_CONSTANT_COLOR        0x8001
typedef void(APIENTRY* PFN_glBlendEquation)(GLenum);
typedef void(APIENTRY* PFN_glBlendColor)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void(APIENTRY* PFN_glBlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);

static void quad(float x, float y, float w, float h)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0), glVertex2f(x, y);
    glTexCoord2f(1, 0), glVertex2f(x + w, y);
    glTexCoord2f(1, 1), glVertex2f(x + w, y + h);
    glTexCoord2f(0, 1), glVertex2f(x, y + h);
    glEnd();
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)inst, (void)prev, (void)cmd, (void)show;
    HDC dc = ex_open("fatgl blending");
    if (!dc) return 1;
    PFN_glBlendEquation      eq  = (PFN_glBlendEquation)(void*)wglGetProcAddress("glBlendEquation");
    PFN_glBlendColor         col = (PFN_glBlendColor)(void*)wglGetProcAddress("glBlendColor");
    PFN_glBlendFuncSeparate  sep = (PFN_glBlendFuncSeparate)(void*)wglGetProcAddress("glBlendFuncSeparate");
    if (!eq || !col || !sep) ex_fail("missing entry point", "glBlendEquation / glBlendColor / glBlendFuncSeparate");

    /* white, alpha ramping left to right */
    static unsigned char img[64 * 4 * 4];
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 64; x++) {
            unsigned char* p = img + (y * 64 + x) * 4;
            p[0] = p[1] = p[2] = 255, p[3] = (unsigned char)(x * 255 / 63);
        }
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    while (ex_poll()) {
        float t = (float)ex_time();
        glViewport(0, 0, g_w, g_h);
        glClearColor(0.1f, 0.1f, 0.12f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, 4, 0, 2.5, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glDisable(GL_BLEND);
        for (int i = 0; i < 8; i++) { /* stripes */
            glColor3f(i & 1 ? 0.85f : 0.25f, 0.45f + 0.05f * (float)i, i & 2 ? 0.8f : 0.3f);
            quad((float)i * 0.5f, 0, 0.25f, 2.5f);
        }
        glEnable(GL_BLEND);
        float wob = 0.05f * sinf(t * 2.0f);
        struct {
            GLenum s, d, e;
        } m[7] = { { GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_FUNC_ADD }, { GL_SRC_ALPHA, GL_ONE, GL_FUNC_ADD },
                   { GL_DST_COLOR, GL_ZERO, GL_FUNC_ADD },         { GL_CONSTANT_COLOR, GL_ONE_MINUS_SRC_ALPHA, GL_FUNC_ADD },
                   { GL_ONE, GL_ONE, GL_FUNC_REVERSE_SUBTRACT },   { GL_ONE, GL_ONE, GL_MIN },
                   { GL_ONE, GL_ONE, GL_MAX } };
        col(0.2f, 0.9f, 0.4f, 1);
        for (int i = 0; i < 7; i++) {
            float x = 0.15f + (float)(i % 4) * 0.97f, y = i < 4 ? 1.35f : 0.25f;
            glBlendFunc(m[i].s, m[i].d);
            eq(m[i].e);
            glColor4f(1.0f, 0.55f, 0.1f, 0.6f);
            quad(x, y + wob, 0.8f, 0.9f);
        }
        eq(GL_FUNC_ADD);
        sep(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_TEXTURE_2D);
        glColor4f(1, 1, 1, 1);
        quad(3.06f, 0.25f + wob, 0.8f, 0.9f);
        glDisable(GL_TEXTURE_2D);
        SwapBuffers(dc);
        ex_fps(dc, "blend");
    }
    return 0;
}
