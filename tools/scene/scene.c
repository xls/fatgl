/* A Doom 3 BFG style frame through the system opengl32.dll (fatgl's when it
 * sits next to the executable), so game paths are tested without the game:
 * a depth prepass, per light a stencil shadow volume pass (color writes
 * masked, two sided stencil) and an additive light pass (stencil == 0, depth
 * EQUAL) with BFG's interaction shaders, a render to texture "monitor", and a
 * screen copy post process (BFG's _currentRender heat haze).
 *   scene <dir with interaction.vert / .frag> [frames] [out.ppm]
 * Prints the best frame time and the image's FNV-1a hash; run.py runs it in
 * every fatgl mode (MSAA, JIT / interpreter, fast textures, SIMD levels) and
 * compares the images. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../bench/bench_scene.h"

#define W 640
#define H 360
#define NOCC 3 /* occluder boxes */

typedef char      GLchar;
typedef ptrdiff_t GLsizeiptr;
#define GLP(ret, name, args) typedef ret(APIENTRY* T_##name) args; static T_##name name##_;
GLP(GLuint, glCreateShader, (GLenum))
GLP(void, glShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))
GLP(void, glCompileShader, (GLuint))
GLP(void, glGetShaderiv, (GLuint, GLenum, GLint*))
GLP(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))
GLP(GLuint, glCreateProgram, (void))
GLP(void, glAttachShader, (GLuint, GLuint))
GLP(void, glLinkProgram, (GLuint))
GLP(void, glGetProgramiv, (GLuint, GLenum, GLint*))
GLP(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))
GLP(void, glUseProgram, (GLuint))
GLP(GLint, glGetUniformLocation, (GLuint, const GLchar*))
GLP(void, glUniform4fv, (GLint, GLsizei, const GLfloat*))
GLP(void, glUniform1i, (GLint, GLint))
GLP(void, glGenBuffers, (GLsizei, GLuint*))
GLP(void, glBindBuffer, (GLenum, GLuint))
GLP(void, glBufferData, (GLenum, GLsizeiptr, const void*, GLenum))
GLP(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*))
GLP(void, glEnableVertexAttribArray, (GLuint))
GLP(void, glActiveTexture, (GLenum))
GLP(void, glGenerateMipmap, (GLenum))
GLP(void, glGenVertexArrays, (GLsizei, GLuint*))
GLP(void, glBindVertexArray, (GLuint))
GLP(void, glStencilOpSeparate, (GLenum, GLenum, GLenum, GLenum))
GLP(void, glGenFramebuffers, (GLsizei, GLuint*))
GLP(void, glBindFramebuffer, (GLenum, GLuint))
GLP(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))
#define LD(n) n##_ = (T_##n)(void*)wglGetProcAddress(#n)

#define GL_TEXTURE0_    0x84C0
#define GL_ARRAY_BUF    0x8892
#define GL_ELEM_BUF     0x8893
#define GL_STATIC_DRAW_ 0x88E4
#define GL_FRAMEBUF     0x8D40
#define GL_COLOR_ATT0   0x8CE0
#define GL_INCR_WRAP_   0x8507
#define GL_DECR_WRAP_   0x8508

static char* slurp(const char* dir, const char* name)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* d = (char*)malloc((size_t)n + 1);
    if (fread(d, 1, (size_t)n, f) != (size_t)n) n = 0;
    d[n] = 0;
    fclose(f);
    return d;
}

static GLuint program(const char* vs, const char* fs)
{
    GLuint p = glCreateProgram_();
    for (int k = 0; k < 2; k++) {
        GLuint s = glCreateShader_(k ? 0x8B30 : 0x8B31);
        const char* src = k ? fs : vs;
        glShaderSource_(s, 1, &src, NULL);
        glCompileShader_(s);
        GLint ok = 0;
        glGetShaderiv_(s, 0x8B81, &ok);
        if (!ok) {
            char log[4096];
            glGetShaderInfoLog_(s, sizeof(log), NULL, log);
            printf("compile: %s\n", log);
            exit(1);
        }
        glAttachShader_(p, s);
    }
    glLinkProgram_(p);
    GLint ok = 0;
    glGetProgramiv_(p, 0x8B82, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog_(p, sizeof(log), NULL, log);
        printf("link: %s\n", log);
        exit(1);
    }
    return p;
}

/* a textured quad (monitor, post process): position xy z, uv */
static const char* g_quad_vs = "#version 330\n"
                               "layout(location = 0) in vec4 pos;\n"
                               "layout(location = 8) in vec2 uv;\n"
                               "out vec2 t;\n"
                               "void main() { gl_Position = vec4(pos.xy, pos.z * 0.5, 1.0); t = uv; }\n";
static const char* g_monitor_fs = "#version 330\n"
                                  "uniform sampler2D img;\n"
                                  "in vec2 t;\n"
                                  "out vec4 c;\n"
                                  "void main() { c = texture(img, t) * vec4(0.9, 1.0, 0.9, 1.0); }\n";
/* _currentRender heat haze: the copied screen read through a wobble, at gl_FragCoord */
static const char* g_haze_fs = "#version 330\n"
                               "uniform sampler2D screen;\n"
                               "uniform vec4 size;\n"
                               "in vec2 t;\n"
                               "out vec4 c;\n"
                               "void main() {\n"
                               "  vec2 p = gl_FragCoord.xy * size.xy;\n"
                               "  p += vec2(sin(t.y * 40.0), cos(t.x * 30.0)) * 0.006;\n"
                               "  c = texture(screen, p) * vec4(1.05, 1.0, 0.95, 1.0);\n"
                               "}\n";
/* the monitor's own scene: flat colored triangles */
static const char* g_flat_vs = "#version 330\n"
                               "layout(location = 0) in vec4 pos;\n"
                               "layout(location = 3) in vec4 col;\n"
                               "out vec4 k;\n"
                               "void main() { gl_Position = vec4(pos.xy, 0.0, 1.0); k = col; }\n";
static const char* g_flat_fs = "#version 330\n"
                               "in vec4 k;\n"
                               "out vec4 c;\n"
                               "void main() { c = k; }\n";

typedef struct qvert {
    float   xyzw[4];
    float   st[2];
    uint8_t normal[4], tangent[4], color[4];
} qvert; /* = scene_vert */

static void qv(qvert* v, float x, float y, float z, float s, float t)
{
    memset(v, 0, sizeof(*v));
    v->xyzw[0] = x, v->xyzw[1] = y, v->xyzw[2] = z, v->xyzw[3] = 1.0f;
    v->st[0] = s, v->st[1] = t;
    v->normal[0] = 128, v->normal[1] = 128, v->normal[2] = 255;
    v->tangent[0] = 255, v->tangent[1] = 128, v->tangent[2] = 128, v->tangent[3] = 255;
    v->color[0] = v->color[1] = v->color[2] = v->color[3] = 255;
}

/* a box from z0 to z1 over [x0, x1] x [y0, y1]: 36 vertices (6 faces, 2 triangles each) */
static int box(qvert* v, float x0, float y0, float x1, float y1, float z0, float z1)
{
    const float c[8][3] = { { x0, y0, z0 }, { x1, y0, z0 }, { x1, y1, z0 }, { x0, y1, z0 },
                            { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 } };
    static const int f[6][4] = { { 0, 1, 2, 3 }, { 5, 4, 7, 6 }, { 4, 5, 1, 0 }, { 3, 2, 6, 7 }, { 4, 0, 3, 7 }, { 1, 5, 6, 2 } };
    int n = 0;
    for (int i = 0; i < 6; i++) {
        static const int tri[6] = { 0, 1, 2, 0, 2, 3 };
        for (int k = 0; k < 6; k++) {
            const float* p = c[f[i][tri[k]]];
            qv(&v[n++], p[0], p[1], p[2], 0.5f + 0.5f * p[0], 0.5f + 0.5f * p[1]);
        }
    }
    return n;
}

static const float g_occ[NOCC][4] = { { -0.62f, -0.15f, -0.38f, 0.15f }, { 0.1f, -0.7f, 0.3f, -0.45f }, { 0.35f, 0.15f, 0.6f, 0.4f } };

static void attribs(GLuint vb)
{
    glBindBuffer_(GL_ARRAY_BUF, vb);
    glVertexAttribPointer_(0, 4, GL_FLOAT, GL_FALSE, sizeof(qvert), (const void*)offsetof(qvert, xyzw));
    glVertexAttribPointer_(8, 2, GL_FLOAT, GL_FALSE, sizeof(qvert), (const void*)offsetof(qvert, st));
    glVertexAttribPointer_(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(qvert), (const void*)offsetof(qvert, normal));
    glVertexAttribPointer_(9, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(qvert), (const void*)offsetof(qvert, tangent));
    glVertexAttribPointer_(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(qvert), (const void*)offsetof(qvert, color));
}

static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM wp, LPARAM lp) { return DefWindowProcA(w, m, wp, lp); }

int main(int argc, char** argv)
{
    const char* dir    = argc > 1 ? argv[1] : ".";
    int         frames = argc > 2 ? atoi(argv[2]) : 10;
    const char* out    = argc > 3 ? argv[3] : "scene.ppm";
    WNDCLASSA   wc     = { 0 };
    wc.lpfnWndProc = proc, wc.hInstance = GetModuleHandleA(NULL), wc.lpszClassName = "fatgl_scene";
    RegisterClassA(&wc);
    HWND win = CreateWindowA("fatgl_scene", "scene", WS_POPUP, 0, 0, W, H, NULL, NULL, wc.hInstance, NULL); /* never shown */
    HDC  dc  = GetDC(win);
    PIXELFORMATDESCRIPTOR pfd = { 0 };
    pfd.nSize = sizeof(pfd), pfd.nVersion = 1, pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.cColorBits = 32, pfd.cDepthBits = 24, pfd.cStencilBits = 8;
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
    HGLRC rc = wglCreateContext(dc);
    wglMakeCurrent(dc, rc);
    printf("renderer: %s\n", (const char*)glGetString(GL_RENDERER));
    LD(glCreateShader), LD(glShaderSource), LD(glCompileShader), LD(glGetShaderiv), LD(glGetShaderInfoLog), LD(glCreateProgram);
    LD(glAttachShader), LD(glLinkProgram), LD(glGetProgramiv), LD(glGetProgramInfoLog), LD(glUseProgram), LD(glGetUniformLocation);
    LD(glUniform4fv), LD(glUniform1i), LD(glGenBuffers), LD(glBindBuffer), LD(glBufferData), LD(glVertexAttribPointer);
    LD(glEnableVertexAttribArray), LD(glActiveTexture), LD(glGenerateMipmap), LD(glGenVertexArrays), LD(glBindVertexArray);
    LD(glStencilOpSeparate), LD(glGenFramebuffers), LD(glBindFramebuffer), LD(glFramebufferTexture2D);
    char *ivs = slurp(dir, "interaction.vert"), *ifs = slurp(dir, "interaction.frag");
    if (!ivs || !ifs) {
        printf("interaction.vert / .frag not found in %s\n", dir);
        return 1;
    }
    GLuint pi = program(ivs, ifs), pm = program(g_quad_vs, g_monitor_fs), ph = program(g_quad_vs, g_haze_fs), pf = program(g_flat_vs, g_flat_fs);

    /* the interaction textures (bench_scene.h) on units 0..4 */
    GLuint tex[5];
    glGenTextures(5, tex);
    for (int i = 0; i < 5; i++) {
        int      w = scene_tex_w(i), h = scene_tex_h(i);
        uint8_t* img = (uint8_t*)malloc((size_t)w * (size_t)h * 4);
        scene_texture(i, img);
        glActiveTexture_(GL_TEXTURE0_ + (GLenum)i);
        glBindTexture(GL_TEXTURE_2D, tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
        glGenerateMipmap_(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        GLint wrap = scene_tex_clamp(i) ? 0x812D /* GL_CLAMP_TO_BORDER */ : GL_REPEAT;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
        free(img);
    }
    /* the monitor's render target (unit 5) and the screen copy (unit 6) */
    GLuint mon, scr, fbo;
    glGenTextures(1, &mon), glGenTextures(1, &scr);
    glActiveTexture_(GL_TEXTURE0_ + 5);
    glBindTexture(GL_TEXTURE_2D, mon);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glActiveTexture_(GL_TEXTURE0_ + 6);
    glBindTexture(GL_TEXTURE_2D, scr);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F /* GL_CLAMP_TO_EDGE */);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
    glGenFramebuffers_(1, &fbo);
    glBindFramebuffer_(GL_FRAMEBUF, fbo);
    glFramebufferTexture2D_(GL_FRAMEBUF, GL_COLOR_ATT0, GL_TEXTURE_2D, mon, 0);
    glBindFramebuffer_(GL_FRAMEBUF, 0);

    /* geometry: the floor grid, the occluders (their tops are lit too), per light shadow
     * volumes (the occluder footprints pushed away from the light, from in front of the
     * floor to behind it: z-pass counting leaves stencil 1 on the shadowed floor), the
     * monitor and haze quads, the monitor's triangles */
    static scene_vert grid[SCENE_NV];
    static uint32_t   gidx[SCENE_NI];
    scene_mesh(grid, gidx);
    static qvert occ[NOCC * 36], vol[SCENE_LIGHTS][NOCC * 36], quads[12], tris[9];
    int          nocc = 0;
    for (int i = 0; i < NOCC; i++) nocc += box(occ + nocc, g_occ[i][0], g_occ[i][1], g_occ[i][2], g_occ[i][3], -0.30f, -0.05f);
    for (int l = 0; l < SCENE_LIGHTS; l++) {
        float va[18][4], fa[2][4];
        scene_uniforms(l, va, fa);
        int n = 0;
        for (int i = 0; i < NOCC; i++) {
            float cx = 0.5f * (g_occ[i][0] + g_occ[i][2]), cy = 0.5f * (g_occ[i][1] + g_occ[i][3]);
            float dx = (cx - va[0][0]) * 0.35f, dy = (cy - va[0][1]) * 0.35f; /* away from the light */
            n += box(vol[l] + n, g_occ[i][0] + dx, g_occ[i][1] + dy, g_occ[i][2] + dx, g_occ[i][3] + dy, -0.6f, 0.6f);
        }
    }
    qv(&quads[0], -0.95f, 0.55f, -0.4f, 0, 0), qv(&quads[1], -0.55f, 0.55f, -0.4f, 1, 0), qv(&quads[2], -0.55f, 0.95f, -0.4f, 1, 1);
    qv(&quads[3], -0.95f, 0.55f, -0.4f, 0, 0), qv(&quads[4], -0.55f, 0.95f, -0.4f, 1, 1), qv(&quads[5], -0.95f, 0.95f, -0.4f, 0, 1);
    qv(&quads[6], -0.2f, -0.2f, -0.9f, 0, 0), qv(&quads[7], 0.6f, -0.2f, -0.9f, 1, 0), qv(&quads[8], 0.6f, 0.5f, -0.9f, 1, 1);
    qv(&quads[9], -0.2f, -0.2f, -0.9f, 0, 0), qv(&quads[10], 0.6f, 0.5f, -0.9f, 1, 1), qv(&quads[11], -0.2f, 0.5f, -0.9f, 0, 1);
    for (int i = 0; i < 9; i++) {
        float a = (float)(i / 3) * 2.1f + (float)(i % 3) * 2.094f;
        qv(&tris[i], 0.8f * (float)(i % 3 == 0 ? 0.1 : 1.0) * (float)cos(a), 0.8f * (float)sin(a), 0, 0, 0);
        tris[i].color[0] = (uint8_t)(i % 3 == 0 ? 255 : 40), tris[i].color[1] = (uint8_t)(i % 3 == 1 ? 255 : 60), tris[i].color[2] = (uint8_t)(i % 3 == 2 ? 255 : 80);
    }
    GLuint vao, vb[5], ib;
    glGenVertexArrays_(1, &vao);
    glBindVertexArray_(vao);
    glGenBuffers_(5, vb), glGenBuffers_(1, &ib);
    glBindBuffer_(GL_ARRAY_BUF, vb[0]), glBufferData_(GL_ARRAY_BUF, sizeof(grid), grid, GL_STATIC_DRAW_);
    glBindBuffer_(GL_ARRAY_BUF, vb[1]), glBufferData_(GL_ARRAY_BUF, sizeof(occ), occ, GL_STATIC_DRAW_);
    glBindBuffer_(GL_ARRAY_BUF, vb[2]), glBufferData_(GL_ARRAY_BUF, sizeof(vol), vol, GL_STATIC_DRAW_);
    glBindBuffer_(GL_ARRAY_BUF, vb[3]), glBufferData_(GL_ARRAY_BUF, sizeof(quads), quads, GL_STATIC_DRAW_);
    glBindBuffer_(GL_ARRAY_BUF, vb[4]), glBufferData_(GL_ARRAY_BUF, sizeof(tris), tris, GL_STATIC_DRAW_);
    glBindBuffer_(GL_ELEM_BUF, ib), glBufferData_(GL_ELEM_BUF, sizeof(gidx), gidx, GL_STATIC_DRAW_);
    static const GLuint en[5] = { 0, 8, 2, 9, 3 };
    for (int i = 0; i < 5; i++) glEnableVertexAttribArray_(en[i]);

    glUseProgram_(pi);
    for (int i = 0; i < 5; i++) {
        char nm[8];
        snprintf(nm, sizeof(nm), "samp%d", i);
        glUniform1i_(glGetUniformLocation_(pi, nm), i);
    }
    GLint lva = glGetUniformLocation_(pi, "_va_"), lfa = glGetUniformLocation_(pi, "_fa_");
    glUseProgram_(pm), glUniform1i_(glGetUniformLocation_(pm, "img"), 5);
    glUseProgram_(ph), glUniform1i_(glGetUniformLocation_(ph, "screen"), 6);
    const float size[4] = { 1.0f / (float)W, 1.0f / (float)H, 0, 0 };
    glUniform4fv_(glGetUniformLocation_(ph, "size"), 1, size);

    static uint8_t img[W * H * 4];
    double        best = 1e30;
    LARGE_INTEGER fq;
    QueryPerformanceFrequency(&fq);
    for (int f = 0; f < frames + 1; f++) { /* frame 0: warm up */
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        /* the monitor: its own little scene in a framebuffer object */
        glBindFramebuffer_(GL_FRAMEBUF, fbo);
        glViewport(0, 0, 128, 128);
        glDisable(GL_DEPTH_TEST), glDisable(GL_STENCIL_TEST), glDisable(GL_BLEND);
        glClearColor(0.05f, 0.1f, 0.2f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram_(pf);
        attribs(vb[4]);
        glDrawArrays(GL_TRIANGLES, 0, 9);
        glBindFramebuffer_(GL_FRAMEBUF, 0);
        glViewport(0, 0, W, H);

        glClearColor(0, 0, 0, 0.25f); /* destination alpha for the blend light pass */
        glClearDepth(1.0);
        glClearStencil(0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glUseProgram_(pi);
        {   /* depth prepass, color writes masked */
            float va[18][4], fa[2][4];
            scene_uniforms(0, va, fa);
            glUniform4fv_(lva, 18, &va[0][0]), glUniform4fv_(lfa, 2, &fa[0][0]);
            glEnable(GL_DEPTH_TEST), glDepthFunc(GL_LESS), glDepthMask(GL_TRUE);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            attribs(vb[0]);
            glDrawElements(GL_TRIANGLES, SCENE_NI, GL_UNSIGNED_INT, NULL);
            attribs(vb[1]);
            glDrawArrays(GL_TRIANGLES, 0, nocc);
        }
        glDepthMask(GL_FALSE);
        for (int l = 0; l < SCENE_LIGHTS; l++) {
            float va[18][4], fa[2][4];
            scene_uniforms(l, va, fa);
            glUniform4fv_(lva, 18, &va[0][0]), glUniform4fv_(lfa, 2, &fa[0][0]);
            /* shadow volumes: no color, two sided z-pass stencil */
            glClear(GL_STENCIL_BUFFER_BIT);
            glEnable(GL_STENCIL_TEST);
            glStencilFunc(GL_ALWAYS, 0, 0xFF);
            glStencilOpSeparate_(GL_FRONT, GL_KEEP, GL_KEEP, GL_INCR_WRAP_);
            glStencilOpSeparate_(GL_BACK, GL_KEEP, GL_KEEP, GL_DECR_WRAP_);
            glDisable(GL_CULL_FACE);
            glDepthFunc(GL_LESS);
            glDisable(GL_BLEND);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            attribs(vb[2]);
            glDrawArrays(GL_TRIANGLES, l * NOCC * 36, NOCC * 36);
            /* the light: unshadowed (stencil 0), on the prepass depth, added; alpha masked
             * (BFG's interactions keep destination alpha for later blends) */
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
            glStencilFunc(GL_EQUAL, 0, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            glDepthFunc(GL_EQUAL);
            glEnable(GL_BLEND), glBlendFunc(GL_ONE, GL_ONE);
            attribs(vb[0]);
            glDrawElements(GL_TRIANGLES, SCENE_NI, GL_UNSIGNED_INT, NULL);
            attribs(vb[1]);
            glDrawArrays(GL_TRIANGLES, 0, nocc);
        }
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDisable(GL_STENCIL_TEST);
        /* a blend light: white scaled by the destination alpha the light passes kept (a mask
         * that does not hold paints it white) */
        glDisable(GL_DEPTH_TEST);
        glBlendFunc(GL_DST_ALPHA, GL_ONE);
        glUseProgram_(pf);
        attribs(vb[3]);
        glDrawArrays(GL_TRIANGLES, 6, 6);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        /* the monitor on the wall */
        glDepthFunc(GL_LEQUAL);
        glUseProgram_(pm);
        attribs(vb[3]);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        /* heat haze through a copy of the screen */
        glActiveTexture_(GL_TEXTURE0_ + 6);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, W, H);
        glDisable(GL_DEPTH_TEST);
        glUseProgram_(ph);
        glDrawArrays(GL_TRIANGLES, 6, 6);
        glFinish();
        QueryPerformanceCounter(&t1);
        double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)fq.QuadPart;
        if (f > 0 && ms < best) best = ms;
        if (f == frames) glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, img); /* the last frame, before presenting */
        SwapBuffers(dc); /* a game's frame boundary: fatgl applies its F6 / F7 / F8 modes here */
    }
    uint64_t h = 1469598103934665603ull;
    double   sum[3] = { 0, 0, 0 };
    for (int i = 0; i < W * H; i++)
        for (int k = 0; k < 3; k++) {
            h = (h ^ img[4 * i + k]) * 1099511628211ull;
            sum[k] += img[4 * i + k];
        }
    printf("scene %dx%d, %d lights: %.2f ms (best of %d)\n", W, H, SCENE_LIGHTS, best, frames);
    printf("mean color %.2f %.2f %.2f\n", sum[0] / (W * H), sum[1] / (W * H), sum[2] / (W * H));
    printf("hash %016llx\n", (unsigned long long)h);
    FILE* o = fopen(out, "wb");
    if (o) {
        fprintf(o, "P6 %d %d 255\n", W, H);
        for (int y = H - 1; y >= 0; y--) /* GL rows are bottom up */
            for (int x = 0; x < W; x++) fwrite(img + ((size_t)y * W + (size_t)x) * 4, 1, 3, o);
        fclose(o);
    }
    return 0;
}
