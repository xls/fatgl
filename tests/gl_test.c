/* fatgl tests: a hidden window, a context, then GL calls through the
 * opengl32.dll next to the executable (fatgl's) and checks of their results.
 * Run by `meson test`; never shows a window. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef char      GLchar;
typedef ptrdiff_t GLsizeiptr;
#define GL_BGRA                         0x80E1
#define GL_UNSIGNED_SHORT_5_6_5         0x8363
#define GL_UNSIGNED_SHORT_4_4_4_4       0x8033
#define GL_UNSIGNED_INT_8_8_8_8_REV     0x8367
#define GL_HALF_FLOAT                   0x140B
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#define GL_SAMPLES_PASSED               0x8914
#define GL_QUERY_RESULT                 0x8866
#define GL_FRAGMENT_SHADER              0x8B30
#define GL_VERTEX_SHADER                0x8B31
#define GL_COMPILE_STATUS               0x8B81
#define GL_LINK_STATUS                  0x8B82
#define GL_INCR                         0x1E02

static int g_fail, g_pass;
#define CHECK(cond, ...)                                \
    do {                                                \
        if (cond) g_pass++;                             \
        else {                                          \
            g_fail++;                                   \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
        }                                               \
    } while (0)

#define GLFN(ret, name, args) typedef ret(APIENTRY* PFN_##name) args; static PFN_##name name##_;
GLFN(void, glCompressedTexImage2D, (GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*))
GLFN(void, glGenQueries, (GLsizei, GLuint*))
GLFN(void, glBeginQuery, (GLenum, GLuint))
GLFN(void, glEndQuery, (GLenum))
GLFN(void, glGetQueryObjectuiv, (GLuint, GLenum, GLuint*))
GLFN(GLuint, glCreateShader, (GLenum))
GLFN(void, glShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))
GLFN(void, glCompileShader, (GLuint))
GLFN(void, glGetShaderiv, (GLuint, GLenum, GLint*))
GLFN(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))
GLFN(GLuint, glCreateProgram, (void))
GLFN(void, glAttachShader, (GLuint, GLuint))
GLFN(void, glLinkProgram, (GLuint))
GLFN(void, glGetProgramiv, (GLuint, GLenum, GLint*))
GLFN(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))
GLFN(void, glUseProgram, (GLuint))
GLFN(void, glActiveTextureARB, (GLenum))
GLFN(void, glGenProgramsARB, (GLsizei, GLuint*))
GLFN(void, glBindProgramARB, (GLenum, GLuint))
GLFN(void, glProgramStringARB, (GLenum, GLenum, GLsizei, const void*))
GLFN(void, glProgramEnvParameter4fARB, (GLenum, GLuint, GLfloat, GLfloat, GLfloat, GLfloat))
GLFN(void, glProgramLocalParameter4fARB, (GLenum, GLuint, GLfloat, GLfloat, GLfloat, GLfloat))
GLFN(void, glMultiTexCoord2fARB, (GLenum, GLfloat, GLfloat))
#define LOAD(name) name##_ = (PFN_##name)(void*)wglGetProcAddress(#name)

static uint32_t px(int x, int y) /* RGBA bytes of the framebuffer as 0xAABBGGRR */
{
    uint32_t p = 0;
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &p);
    return p;
}

static int close_to(uint32_t a, uint32_t b, int tol)
{
    for (int k = 0; k < 32; k += 8) {
        int d = (int)((a >> k) & 255) - (int)((b >> k) & 255);
        if (d > tol || d < -tol) return 0;
    }
    return 1;
}

static void ortho(void) /* pixel coordinates, y up */
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 64, 0, 64, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void quad(float x0, float y0, float x1, float y1)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0), glVertex2f(x0, y0);
    glTexCoord2f(1, 0), glVertex2f(x1, y0);
    glTexCoord2f(1, 1), glVertex2f(x1, y1);
    glTexCoord2f(0, 1), glVertex2f(x0, y1);
    glEnd();
}

static void test_pixels(void)
{
    /* packed 5_6_5 and 4_4_4_4 uploads, read back as RGBA bytes */
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    uint16_t c565[4] = { 0xF800, 0x07E0, 0x001F, 0xFFFF }; /* red, green, blue, white */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, c565);
    uint32_t back[4];
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, back);
    CHECK(back[0] == 0xFF0000FFu && back[1] == 0xFF00FF00u && back[2] == 0xFFFF0000u && back[3] == 0xFFFFFFFFu,
          "5_6_5 upload (%08x %08x %08x %08x)", back[0], back[1], back[2], back[3]);
    uint16_t c4444[1] = { 0xF08F }; /* r 15, g 0, b 8, a 15 */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, c4444);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, back);
    CHECK(back[0] == 0xFF8800FFu, "4_4_4_4 upload (%08x)", back[0]);
    uint32_t bgra = 0x80112233u; /* 8_8_8_8_REV BGRA: b 0x33, g 0x22, r 0x11, a 0x80 */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, &bgra);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, back);
    CHECK(back[0] == 0x80332211u, "BGRA 8_8_8_8_REV upload (%08x)", back[0]);
    uint16_t half[4] = { 0x3C00, 0x3800, 0x0000, 0x3C00 }; /* 1, 0.5, 0, 1 */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_HALF_FLOAT, half);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, back);
    CHECK(close_to(back[0], 0xFF0080FFu, 1), "half float upload (%08x)", back[0]);

    /* DXT1: one block, color0 = red, color1 = blue, every texel index 0 / 1 alternating rows */
    uint8_t dxt1[8] = { 0x00, 0xF8, 0x1F, 0x00, 0x00, 0x55, 0x00, 0x55 };
    glCompressedTexImage2D_(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 4, 4, 0, 8, dxt1);
    uint32_t img[16];
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    CHECK(img[0] == 0xFF0000FFu && img[4] == 0xFFFF0000u && img[8] == 0xFF0000FFu && img[12] == 0xFFFF0000u,
          "DXT1 decode (%08x %08x %08x %08x)", img[0], img[4], img[8], img[12]);
    /* DXT5: alpha endpoints 255 / 0, all indices 1 (alpha 0); colors white */
    uint8_t dxt5[16] = { 255, 0, 0x49, 0x92, 0x24, 0x49, 0x92, 0x24, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0 };
    glCompressedTexImage2D_(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 4, 4, 0, 16, dxt5);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    CHECK(img[0] == 0x00FFFFFFu && img[15] == 0x00FFFFFFu, "DXT5 decode (%08x %08x)", img[0], img[15]);
    glDeleteTextures(1, &t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    /* glReadPixels formats */
    glClearColor(1.0f, 0.5f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    uint16_t r565 = 0;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, 1, 1, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, &r565);
    CHECK(r565 == ((31u << 11) | (32u << 5)), "glReadPixels 5_6_5 (%04x)", r565);
    float rf[4];
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, rf);
    CHECK(fabsf(rf[0] - 1) < 0.01f && fabsf(rf[1] - 0.5f) < 0.01f && rf[2] == 0 && rf[3] == 1, "glReadPixels float");
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
}

static void test_raster_state(void)
{
    ortho();
    /* stencil: mark a square, then draw only where marked */
    glClearColor(0, 0, 0, 1);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    quad(10, 10, 20, 20);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glColor3f(0, 1, 0);
    quad(0, 0, 64, 64);
    glDisable(GL_STENCIL_TEST);
    CHECK(px(15, 15) == 0xFF00FF00u && px(30, 30) == 0xFF000000u, "stencil masking (%08x %08x)", px(15, 15), px(30, 30));

    /* occlusion query: the samples of a 10 x 10 quad */
    GLuint q, n = 0;
    glGenQueries_(1, &q);
    glBeginQuery_(GL_SAMPLES_PASSED, q);
    quad(30, 30, 40, 40);
    glEndQuery_(GL_SAMPLES_PASSED);
    glGetQueryObjectuiv_(q, GL_QUERY_RESULT, &n);
    CHECK(n == 100, "occlusion query (%u)", n);

    /* points and lines */
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(1, 1, 1);
    glPointSize(4);
    glBegin(GL_POINTS);
    glVertex2f(50, 50);
    glEnd();
    glPointSize(1);
    glBegin(GL_LINES);
    glVertex2f(2, 5.5f), glVertex2f(30, 5.5f);
    glEnd();
    CHECK(px(48, 48) == 0xFFFFFFFFu && px(51, 51) == 0xFFFFFFFFu && px(52, 52) == 0xFF000000u, "point size 4");
    CHECK(px(10, 5) == 0xFFFFFFFFu && px(10, 6) == 0xFF000000u && px(10, 4) == 0xFF000000u, "1 px line");
}

static GLuint compile(GLenum type, const char* src)
{
    GLuint s = glCreateShader_(type);
    glShaderSource_(s, 1, &src, NULL);
    glCompileShader_(s);
    GLint ok = 0;
    glGetShaderiv_(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog_(s, sizeof(log), NULL, log);
        printf("  shader log: %s\n", log);
    }
    CHECK(ok, "shader compiles");
    return s;
}

/* GL 1.3 multitexture the GoldSrc / Quake way: base texture * lightmap, fog */
static void test_multitexture(void)
{
    ortho();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    GLuint   t[2];
    uint32_t base = 0xFFFFFFFFu, lm[2] = { 0xFF808080u, 0xFF0000FFu }; /* RGBA bytes: white; gray, red */
    glGenTextures(2, t);
    glActiveTextureARB_(0x84C0); /* GL_TEXTURE0 */
    glBindTexture(GL_TEXTURE_2D, t[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &base);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glActiveTextureARB_(0x84C1); /* GL_TEXTURE1: the lightmap */
    glBindTexture(GL_TEXTURE_2D, t[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, lm);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBegin(GL_QUADS);
    glMultiTexCoord2fARB_(0x84C0, 0.5f, 0.5f), glMultiTexCoord2fARB_(0x84C1, 0, 0.5f), glVertex2f(0, 0);
    glMultiTexCoord2fARB_(0x84C0, 0.5f, 0.5f), glMultiTexCoord2fARB_(0x84C1, 1, 0.5f), glVertex2f(64, 0);
    glMultiTexCoord2fARB_(0x84C0, 0.5f, 0.5f), glMultiTexCoord2fARB_(0x84C1, 1, 0.5f), glVertex2f(64, 64);
    glMultiTexCoord2fARB_(0x84C0, 0.5f, 0.5f), glMultiTexCoord2fARB_(0x84C1, 0, 0.5f), glVertex2f(0, 64);
    glEnd();
    CHECK(px(10, 30) == 0xFF808080u && px(50, 30) == 0xFF0000FFu, "texture * lightmap (%08x %08x)", px(10, 30), px(50, 30));
    /* fog on the same quad: linear, half way (ortho: eye distance 1) */
    GLfloat fc[4] = { 0, 0, 1, 1 };
    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 0);
    glFogf(GL_FOG_END, 2);
    glFogfv(GL_FOG_COLOR, fc);
    glBegin(GL_QUADS);
    glMultiTexCoord2fARB_(0x84C0, 0.5f, 0.5f), glMultiTexCoord2fARB_(0x84C1, 0.75f, 0.5f), glVertex2f(0, 0); /* the red texel */
    glVertex2f(64, 0), glVertex2f(64, 64), glVertex2f(0, 64);
    glEnd();
    glDisable(GL_FOG);
    CHECK(close_to(px(10, 30), 0xFF800080u, 2), "linear fog (%08x)", px(10, 30));
    glDisable(GL_TEXTURE_2D);
    glActiveTextureARB_(0x84C0);
    glDisable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glDeleteTextures(2, t);
}

/* ARB_vertex_program / ARB_fragment_program, Doom 3 style */
static void test_arb_programs(void)
{
    const char* vp = "!!ARBvp1.0\n"
                     "OPTION ARB_position_invariant;\n"
                     "PARAM scale = program.env[0];\n"
                     "PARAM tab[3] = { {0, 0, 0, 0}, {0.5, 0.5, 0.5, 1}, {1, 0, 0, 1} };\n"
                     "ADDRESS a0;\n"
                     "TEMP r;\n"
                     "ARL a0.x, scale.w;\n" /* env[0].w = 1: tab[1] */
                     "MUL r, vertex.texcoord[0], scale;\n"
                     "MOV result.texcoord[0], r;\n"
                     "MOV result.color, tab[a0.x];\n"
                     "END\n";
    const char* fp = "!!ARBfp1.0\n"
                     "OPTION ARB_precision_hint_fastest;\n"
                     "PARAM tint = program.local[1];\n"
                     "TEMP t, k, x;\n"
                     "SUB k, fragment.texcoord[0].x, 0.9;\n"
                     "KIL -k;\n" /* discard where s > 0.9 */
                     "SWZ t, fragment.texcoord[0], x, y, 0, 1;\n"
                     "TEX x, fragment.texcoord[0], texture[0], 2D;\n"
                     "MUL x, x, fragment.color;\n" /* white texel * 0.5 */
                     "ADD t, t, tint;\n"
                     "MOV t.z, x.x;\n"
                     "MOV_SAT result.color, t;\n"
                     "END\n";
    GLuint pr[2];
    glGenProgramsARB_(2, pr);
    glBindProgramARB_(0x8620, pr[0]); /* GL_VERTEX_PROGRAM_ARB */
    glProgramStringARB_(0x8620, 0x8875, (GLsizei)strlen(vp), vp);
    GLint pos = 0;
    glGetIntegerv(0x864B, &pos); /* GL_PROGRAM_ERROR_POSITION_ARB */
    CHECK(pos == -1 && glGetError() == GL_NO_ERROR, "vertex program accepted (%d: %s)", pos, (const char*)glGetString(0x8874));
    glBindProgramARB_(0x8804, pr[1]); /* GL_FRAGMENT_PROGRAM_ARB */
    glProgramStringARB_(0x8804, 0x8875, (GLsizei)strlen(fp), fp);
    glGetIntegerv(0x864B, &pos);
    CHECK(pos == -1 && glGetError() == GL_NO_ERROR, "fragment program accepted (%d: %s)", pos, (const char*)glGetString(0x8874));
    glProgramEnvParameter4fARB_(0x8620, 0, 1, 1, 1, 1);
    glProgramLocalParameter4fARB_(0x8804, 1, 0, 0, 0, 0);
    /* a white texture on unit 0 */
    GLuint   tx;
    uint32_t white = 0xFFFFFFFFu;
    glGenTextures(1, &tx);
    glBindTexture(GL_TEXTURE_2D, tx);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    ortho();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(0x8620);
    glEnable(0x8804);
    quad(0, 0, 64, 64);
    glDisable(0x8620);
    glDisable(0x8804);
    uint32_t a = px(16, 48), want = 0xFF000000u | 0x80u << 16 | (uint32_t)(48.5 / 64 * 255 + 0.5) << 8 | (uint32_t)(16.5 / 64 * 255 + 0.5);
    CHECK(close_to(a, want, 2), "ARB programs: texcoords, env / local, ADDRESS, SWZ, TEX (%08x want %08x)", a, want);
    CHECK(px(62, 30) == 0xFF000000u, "ARB KIL (%08x)", px(62, 30));
    /* a broken program: an error position and string */
    const char* bad = "!!ARBfp1.0\nMOV result.color, nothing;\nEND\n";
    glProgramStringARB_(0x8804, 0x8875, (GLsizei)strlen(bad), bad);
    glGetIntegerv(0x864B, &pos);
    const char* es = (const char*)glGetString(0x8874);
    CHECK(pos > 0 && es && strstr(es, "nothing") && glGetError() == GL_INVALID_OPERATION, "ARB error reporting (%d: %s)", pos, es ? es : "");
    glDeleteTextures(1, &tx);
}

static void test_legacy_glsl(void)
{
    /* GLSL 1.10 with the fixed function state */
    const char* vs = "void main() { gl_FrontColor = gl_Color; gl_TexCoord[0] = gl_MultiTexCoord0; gl_Position = ftransform(); }";
    const char* fs = "void main() { gl_FragColor = gl_Color * vec4(gl_TexCoord[0].st, 1.0, 1.0); }";
    GLuint      p  = glCreateProgram_();
    glAttachShader_(p, compile(GL_VERTEX_SHADER, vs));
    glAttachShader_(p, compile(GL_FRAGMENT_SHADER, fs));
    glLinkProgram_(p);
    GLint ok = 0;
    glGetProgramiv_(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog_(p, sizeof(log), NULL, log);
        printf("  link log: %s\n", log);
    }
    CHECK(ok, "GLSL 1.10 program links");
    if (!ok) return;
    ortho();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram_(p);
    glColor3f(1, 1, 1);
    quad(0, 0, 64, 64); /* texcoord = position / 64 */
    glUseProgram_(0);
    uint32_t a = px(16, 48), want = 0xFFFF0000u | (uint32_t)(48.5 / 64 * 255 + 0.5) << 8 | (uint32_t)(16.5 / 64 * 255 + 0.5);
    CHECK(close_to(a, want, 2), "gl_Color, gl_TexCoord, ftransform, gl_FragColor (%08x want %08x)", a, want);
}

static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM wp, LPARAM lp) { return DefWindowProcA(w, m, wp, lp); }

int main(void)
{
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = proc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.lpszClassName = "fatgl_test";
    RegisterClassA(&wc);
    HWND win = CreateWindowA("fatgl_test", "fatgl test", WS_POPUP, 0, 0, 64, 64, NULL, NULL, wc.hInstance, NULL); /* never shown */
    HDC  dc  = GetDC(win);
    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd), pfd.nVersion = 1, pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA, pfd.cColorBits = 32, pfd.cDepthBits = 24, pfd.cStencilBits = 8;
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
    HGLRC rc = wglCreateContext(dc);
    wglMakeCurrent(dc, rc);
    const char* r = (const char*)glGetString(GL_RENDERER);
    printf("fatgl test, renderer %s\n", r ? r : "?");
    CHECK(r && strstr(r, "fatgl"), "fatgl's opengl32.dll is the one loaded");
    glViewport(0, 0, 64, 64);
    LOAD(glCompressedTexImage2D), LOAD(glGenQueries), LOAD(glBeginQuery), LOAD(glEndQuery), LOAD(glGetQueryObjectuiv);
    LOAD(glCreateShader), LOAD(glShaderSource), LOAD(glCompileShader), LOAD(glGetShaderiv), LOAD(glGetShaderInfoLog);
    LOAD(glCreateProgram), LOAD(glAttachShader), LOAD(glLinkProgram), LOAD(glGetProgramiv), LOAD(glGetProgramInfoLog);
    LOAD(glUseProgram), LOAD(glActiveTextureARB), LOAD(glMultiTexCoord2fARB);
    LOAD(glGenProgramsARB), LOAD(glBindProgramARB), LOAD(glProgramStringARB), LOAD(glProgramEnvParameter4fARB);
    LOAD(glProgramLocalParameter4fARB);
    const char* ext = (const char*)glGetString(GL_EXTENSIONS);
    CHECK(ext && strstr(ext, "GL_ARB_multitexture") && strstr(ext, "GL_EXT_texture_compression_s3tc"), "GL_EXTENSIONS");
    CHECK(glActiveTextureARB_ && glMultiTexCoord2fARB_, "ARB multitexture entry points");
    test_pixels();
    test_raster_state();
    test_multitexture();
    test_arb_programs();
    test_legacy_glsl();
    CHECK(glGetError() == GL_NO_ERROR, "no GL error at the end (%04x)", glGetError());
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(rc);
    ReleaseDC(win, dc);
    DestroyWindow(win);
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
