/* fatgl example: points and lines. Left: fixed function (OpenGL 1.1) depth
 * tested wire cube, a line strip spiral, a line loop and points of several
 * sizes. Right: GLSL point sprites (gl_PointSize from the vertex shader,
 * round via gl_PointCoord + discard, additive blending) and GL_LINE_STRIP
 * with a shader, from a vertex buffer. */
#include "win.h"
#include <math.h>
#include <stddef.h>

typedef char      GLchar;
typedef ptrdiff_t GLsizeiptr;
#define GL_ARRAY_BUFFER        0x8892
#define GL_STREAM_DRAW         0x88E0
#define GL_FRAGMENT_SHADER     0x8B30
#define GL_VERTEX_SHADER       0x8B31
#define GL_COMPILE_STATUS      0x8B81
#define GL_LINK_STATUS         0x8B82
#define GL_PROGRAM_POINT_SIZE  0x8642

#define GLFN(ret, name, args) typedef ret(APIENTRY* PFN_##name) args; static PFN_##name name##_;
GLFN(GLuint, glCreateShader, (GLenum))
GLFN(void, glShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))
GLFN(void, glCompileShader, (GLuint))
GLFN(void, glGetShaderiv, (GLuint, GLenum, GLint*))
GLFN(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))
GLFN(GLuint, glCreateProgram, (void))
GLFN(void, glAttachShader, (GLuint, GLuint))
GLFN(void, glBindAttribLocation, (GLuint, GLuint, const GLchar*))
GLFN(void, glLinkProgram, (GLuint))
GLFN(void, glGetProgramiv, (GLuint, GLenum, GLint*))
GLFN(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))
GLFN(void, glUseProgram, (GLuint))
GLFN(GLint, glGetUniformLocation, (GLuint, const GLchar*))
GLFN(void, glUniform1f, (GLint, GLfloat))
GLFN(void, glGenBuffers, (GLsizei, GLuint*))
GLFN(void, glBindBuffer, (GLenum, GLuint))
GLFN(void, glBufferData, (GLenum, GLsizeiptr, const void*, GLenum))
GLFN(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*))
GLFN(void, glEnableVertexAttribArray, (GLuint))
GLFN(void, glDisableVertexAttribArray, (GLuint))

static void* load(const char* n)
{
    void* p = (void*)wglGetProcAddress(n);
    if (!p) ex_fail("missing entry point", n);
    return p;
}
#define LOAD(name) name##_ = (PFN_##name)load(#name)

static const char* vs_src = "#version 120\n"
                            "attribute vec4 aPos;\n" /* xy, size, hue */
                            "uniform float uAspect;\n"
                            "varying vec3 vColor;\n"
                            "void main() {\n"
                            "    gl_Position = vec4(aPos.x / uAspect, aPos.y, 0.0, 1.0);\n"
                            "    gl_PointSize = aPos.z;\n"
                            "    vColor = 0.5 + 0.5 * cos(6.2831 * (aPos.w + vec3(0.0, 0.33, 0.67)));\n"
                            "}\n";
static const char* fs_src = "#version 120\n"
                            "varying vec3 vColor;\n"
                            "uniform float uRound;\n"
                            "void main() {\n"
                            "    vec2 d = gl_PointCoord - 0.5;\n"
                            "    float r = dot(d, d) * 4.0;\n"
                            "    if (uRound > 0.5 && r > 1.0) discard;\n"
                            "    gl_FragColor = vec4(vColor * (uRound > 0.5 ? 1.0 - r * 0.7 : 1.0), 1.0);\n"
                            "}\n";

static GLuint shader(GLenum type, const char* src)
{
    GLuint s = glCreateShader_(type);
    glShaderSource_(s, 1, &src, NULL);
    glCompileShader_(s);
    GLint ok = 0;
    glGetShaderiv_(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog_(s, sizeof(log), NULL, log);
        ex_fail("shader", log);
    }
    return s;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)inst, (void)prev, (void)cmd, (void)show;
    HDC dc = ex_open("fatgl points and lines");
    if (!dc) return 1;
    LOAD(glCreateShader), LOAD(glShaderSource), LOAD(glCompileShader), LOAD(glGetShaderiv), LOAD(glGetShaderInfoLog);
    LOAD(glCreateProgram), LOAD(glAttachShader), LOAD(glBindAttribLocation), LOAD(glLinkProgram), LOAD(glGetProgramiv);
    LOAD(glGetProgramInfoLog), LOAD(glUseProgram), LOAD(glGetUniformLocation), LOAD(glUniform1f), LOAD(glGenBuffers);
    LOAD(glBindBuffer), LOAD(glBufferData), LOAD(glVertexAttribPointer), LOAD(glEnableVertexAttribArray);
    LOAD(glDisableVertexAttribArray);
    GLuint prog = glCreateProgram_();
    glAttachShader_(prog, shader(GL_VERTEX_SHADER, vs_src));
    glAttachShader_(prog, shader(GL_FRAGMENT_SHADER, fs_src));
    glBindAttribLocation_(prog, 0, "aPos");
    glLinkProgram_(prog);
    GLint ok = 0;
    glGetProgramiv_(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog_(prog, sizeof(log), NULL, log);
        ex_fail("link", log);
    }
    GLint  laspect = glGetUniformLocation_(prog, "uAspect"), lround = glGetUniformLocation_(prog, "uRound");
    GLuint vbo;
    glGenBuffers_(1, &vbo);
    enum { NS = 240 };
    static float sprites[NS * 4];

    while (ex_poll()) {
        float t = (float)ex_time(), a = (float)g_w / (float)(g_h > 0 ? g_h : 1);
        glViewport(0, 0, g_w, g_h);
        glClearColor(0.05f, 0.06f, 0.09f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        /* ---- left half: fixed function ---- */
        glUseProgram_(0);
        glViewport(0, 0, g_w / 2, g_h);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glFrustum(-0.25 * a, 0.25 * a, -0.5, 0.5, 1, 20); /* half the window wide */
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(0, 0, -4);
        glEnable(GL_DEPTH_TEST);
        glPushMatrix();
        glRotatef(t * 40, 1, 1, 0);
        glLineWidth(2);
        glBegin(GL_LINES); /* wire cube: 12 edges */
        for (int e = 0; e < 12; e++) {
            int   ax = e / 4, s1 = e & 1 ? 1 : -1, s2 = e & 2 ? 1 : -1;
            float p[3], q[3];
            p[ax] = -1, q[ax] = 1;
            p[(ax + 1) % 3] = q[(ax + 1) % 3] = (float)s1;
            p[(ax + 2) % 3] = q[(ax + 2) % 3] = (float)s2;
            glColor3f(0.3f + 0.35f * (float)ax, 0.9f - 0.3f * (float)ax, 0.4f);
            glVertex3fv(p), glVertex3fv(q);
        }
        glEnd();
        glPopMatrix();
        glLineWidth(1);
        glBegin(GL_LINE_STRIP); /* spiral */
        for (int i = 0; i <= 200; i++) {
            float u = (float)i / 200.0f, r = 0.2f + 1.2f * u, ang = u * 18.0f + t;
            glColor3f(1, u, 1 - u);
            glVertex3f(r * cosf(ang), r * sinf(ang), -1.5f);
        }
        glEnd();
        glBegin(GL_LINE_LOOP); /* hexagon */
        glColor3f(1, 1, 1);
        for (int i = 0; i < 6; i++) glVertex3f(1.6f * cosf((float)i * 1.0472f), 1.6f * sinf((float)i * 1.0472f), 0);
        glEnd();
        for (int s = 1; s <= 6; s++) { /* points of size 1 .. 11 */
            glPointSize((float)(2 * s - 1));
            glBegin(GL_POINTS);
            glColor3f(1, 0.8f, 0.2f);
            glVertex3f(-1.5f + 0.5f * (float)s, -1.35f, 0.5f);
            glEnd();
        }
        glPointSize(1);
        glDisable(GL_DEPTH_TEST);

        /* ---- right half: shaders, point sprites + a line strip ---- */
        glViewport(g_w / 2, 0, g_w - g_w / 2, g_h);
        for (int i = 0; i < NS; i++) {
            float u = (float)i / NS, ang = u * 25.0f + t * (0.5f + u);
            sprites[4 * i]     = (0.15f + 0.8f * u) * cosf(ang) * a * 0.5f;
            sprites[4 * i + 1] = (0.15f + 0.8f * u) * sinf(ang);
            sprites[4 * i + 2] = 4.0f + 20.0f * (0.5f + 0.5f * sinf(t * 2 + u * 20));
            sprites[4 * i + 3] = u + t * 0.1f;
        }
        glUseProgram_(prog);
        glUniform1f_(laspect, a * 0.5f);
        glBindBuffer_(GL_ARRAY_BUFFER, vbo);
        glBufferData_(GL_ARRAY_BUFFER, sizeof(sprites), sprites, GL_STREAM_DRAW);
        glVertexAttribPointer_(0, 4, GL_FLOAT, GL_FALSE, 16, (void*)0);
        glEnableVertexAttribArray_(0);
        glUniform1f_(lround, 0);
        glDrawArrays(GL_LINE_STRIP, 0, NS);
        glEnable(GL_PROGRAM_POINT_SIZE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glUniform1f_(lround, 1);
        glDrawArrays(GL_POINTS, 0, NS);
        glDisable(GL_BLEND);
        glDisable(GL_PROGRAM_POINT_SIZE);
        glDisableVertexAttribArray_(0);
        glBindBuffer_(GL_ARRAY_BUFFER, 0);
        glUseProgram_(0);
        SwapBuffers(dc);
        ex_fps(dc, "prims");
    }
    return 0;
}
