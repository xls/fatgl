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
#include <stdlib.h>
#include <string.h>

#include "bfg_shaders.h"

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
#define GL_TEXTURE_3D                   0x806F
#define GL_TEXTURE_CUBE_MAP             0x8513
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X  0x8515
#define GL_TEXTURE_2D_ARRAY             0x8C1A
#define GL_TEXTURE_SWIZZLE_RGBA         0x8E46
#define GL_TEXTURE_WRAP_R               0x8072
#define GL_CLAMP_TO_EDGE                0x812F
#define GL_TEXTURE0                     0x84C0
#define GL_FRAMEBUFFER                  0x8D40
#define GL_COLOR_ATTACHMENT0            0x8CE0
#define GL_FRAMEBUFFER_COMPLETE         0x8CD5

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
GLFN(GLint, glGetUniformLocation, (GLuint, const GLchar*))
GLFN(void, glUniform1f, (GLint, GLfloat))
GLFN(void, glActiveTextureARB, (GLenum))
GLFN(void, glGenProgramsARB, (GLsizei, GLuint*))
GLFN(void, glBindProgramARB, (GLenum, GLuint))
GLFN(void, glProgramStringARB, (GLenum, GLenum, GLsizei, const void*))
GLFN(void, glProgramEnvParameter4fARB, (GLenum, GLuint, GLfloat, GLfloat, GLfloat, GLfloat))
GLFN(void, glProgramLocalParameter4fARB, (GLenum, GLuint, GLfloat, GLfloat, GLfloat, GLfloat))
GLFN(void, glMultiTexCoord2fARB, (GLenum, GLfloat, GLfloat))
GLFN(void, glUniform1i, (GLint, GLint))
GLFN(void, glUniform3f, (GLint, GLfloat, GLfloat, GLfloat))
GLFN(void, glTexImage3D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*))
GLFN(void, glGenFramebuffers, (GLsizei, GLuint*))
GLFN(void, glBindFramebuffer, (GLenum, GLuint))
GLFN(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))
GLFN(GLenum, glCheckFramebufferStatus, (GLenum))
GLFN(void, glDeleteFramebuffers, (GLsizei, const GLuint*))
GLFN(void, glGenBuffers, (GLsizei, GLuint*))
GLFN(void, glBindBuffer, (GLenum, GLuint))
GLFN(void, glBufferData, (GLenum, ptrdiff_t, const void*, GLenum))
GLFN(void*, glMapBufferRange, (GLenum, ptrdiff_t, ptrdiff_t, GLbitfield))
GLFN(GLboolean, glUnmapBuffer, (GLenum))
GLFN(void, glDeleteBuffers, (GLsizei, const GLuint*))
GLFN(void, glBindAttribLocation, (GLuint, GLuint, const GLchar*))
GLFN(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*))
GLFN(void, glEnableVertexAttribArray, (GLuint))
GLFN(void, glDisableVertexAttribArray, (GLuint))
GLFN(void, glUniform4fv, (GLint, GLsizei, const GLfloat*))
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

/* Doom 3 BFG's gui shaders (GLSL 1.50 the way GL drivers accept it) */
static void test_bfg_glsl(void)
{
    const char* vs = "#version 150\n"
"#define PC\n"
"float saturate( float v ) { return clamp( v, 0.0, 1.0 ); }\n"
"vec4 saturate( vec4 v ) { return clamp( v, 0.0, 1.0 ); }\n"
"vec4 tex2Dlod( sampler2D sampler, vec4 texcoord ) { return textureLod( sampler, texcoord.xy, texcoord.w ); }\n"
"uniform vec4 _va_[4];\n"
"float dot4 (vec4 a , vec4 b ) {return dot ( a , b ) ; }\n"
"float dot4 (vec2 a , vec4 b ) {return dot ( vec4 ( a , 0 , 1 ) , b ) ; }\n"
"vec4 swizzleColor (vec4 c ) {return c ; }\n"
"in vec4 in_Position;\n"
"in vec2 in_TexCoord;\n"
"in vec4 in_Normal;\n"
"in vec4 in_Tangent;\n"
"in vec4 in_Color;\n"
"in vec4 in_Color2;\n"
"out vec4 gl_Position;\n"
"out vec2 vofi_TexCoord0;\n"
"out vec4 vofi_TexCoord1;\n"
"out vec4 gl_FrontColor;\n"
"void main() {\n"
"    gl_Position . x = dot4 ( in_Position , _va_[0 /* rpMVPmatrixX */] ) ;\n"
"    gl_Position . y = dot4 ( in_Position , _va_[1 /* rpMVPmatrixY */] ) ;\n"
"    gl_Position . z = dot4 ( in_Position , _va_[2 /* rpMVPmatrixZ */] ) ;\n"
"    gl_Position . w = dot4 ( in_Position , _va_[3 /* rpMVPmatrixW */] ) ;\n"
"    vofi_TexCoord0 . xy = in_TexCoord . xy ;\n"
"    vofi_TexCoord1 = ( swizzleColor ( in_Color2 ) * 2 ) - 1 ;\n"
"    gl_FrontColor = swizzleColor ( in_Color ) ;\n"
"}\n"
;
    const char* fs = "#version 150\n"
"#define PC\n"
"void clip( float v ) { if ( v < 0.0 ) { discard; } }\n"
"void clip( vec4 v ) { if ( any( lessThan( v, vec4( 0.0 ) ) ) ) { discard; } }\n"
"float saturate( float v ) { return clamp( v, 0.0, 1.0 ); }\n"
"vec4 tex2D( sampler2D sampler, vec2 texcoord ) { return texture( sampler, texcoord.xy ); }\n"
"vec4 tex2D( sampler2DShadow sampler, vec3 texcoord ) { return vec4( texture( sampler, texcoord.xyz ) ); }\n"
"vec4 tex2D( sampler2D sampler, vec2 texcoord, vec2 dx, vec2 dy ) { return textureGrad( sampler, texcoord.xy, dx, dy ); }\n"
"vec4 texCUBE( samplerCube sampler, vec3 texcoord ) { return texture( sampler, texcoord.xyz ); }\n"
"vec4 texCUBE( samplerCubeShadow sampler, vec4 texcoord ) { return vec4( texture( sampler, texcoord.xyzw ) ); }\n"
"vec4 tex1Dproj( sampler1D sampler, vec2 texcoord ) { return textureProj( sampler, texcoord ); }\n"
"vec4 tex3Dproj( sampler3D sampler, vec4 texcoord ) { return textureProj( sampler, texcoord ); }\n"
"vec4 tex2Dlod( sampler2D sampler, vec4 texcoord ) { return textureLod( sampler, texcoord.xy, texcoord.w ); }\n"
"uniform sampler2D samp0;\n"
"in vec4 gl_FragCoord;\n"
"in vec2 vofi_TexCoord0;\n"
"in vec4 vofi_TexCoord1;\n"
"in vec4 gl_Color;\n"
"out vec4 gl_FragColor;\n"
"void main() {\n"
"    vec4 color = ( tex2D ( samp0 , vofi_TexCoord0 ) * gl_Color ) + vofi_TexCoord1 ;\n"
"    gl_FragColor . xyz = color. xyz * color. w ;\n"
"    gl_FragColor . w = color. w ;\n"
"}\n"
;
    GLuint p = glCreateProgram_();
    glAttachShader_(p, compile(GL_VERTEX_SHADER, vs));
    glAttachShader_(p, compile(GL_FRAGMENT_SHADER, fs));
    glLinkProgram_(p);
    GLint ok = 0;
    glGetProgramiv_(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog_(p, sizeof(log), NULL, log);
        printf("  link log: %s\n", log);
    }
    CHECK(ok, "Doom 3 BFG gui.vfp links");
}

/* textureProj (Doom 3 BFG's tex2Dproj): q = 2 halves the coordinates */
static void test_texture_proj(void)
{
    const char* vs = "#version 150\n"
                     "in vec4 pos;\n"
                     "out vec2 uv;\n"
                     "void main() { uv = pos.xy * 0.5 + 0.5; gl_Position = pos; }\n";
    const char* fs = "#version 150\n"
                     "uniform sampler2D tex;\n"
                     "in vec2 uv;\n"
                     "out vec4 col;\n"
                     "void main() { col = textureProj(tex, vec3(uv * 2.0, 2.0)) - texture(tex, uv) + vec4(0.5); }\n";
    GLuint p = glCreateProgram_();
    glAttachShader_(p, compile(GL_VERTEX_SHADER, vs));
    glAttachShader_(p, compile(GL_FRAGMENT_SHADER, fs));
    glLinkProgram_(p);
    GLint ok = 0;
    glGetProgramiv_(p, GL_LINK_STATUS, &ok);
    CHECK(ok, "textureProj program links");
    if (!ok) return;
    uint32_t img[4] = { 0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u, 0xFFFFFFFFu };
    GLuint   t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram_(p);
    glMatrixMode(GL_PROJECTION), glLoadIdentity(), glMatrixMode(GL_MODELVIEW), glLoadIdentity();
    glBegin(GL_QUADS);
    glVertex2f(-1, -1), glVertex2f(1, -1), glVertex2f(1, 1), glVertex2f(-1, 1);
    glEnd();
    glUseProgram_(0);
    CHECK(close_to(px(10, 10), 0x80808080u, 1) && close_to(px(50, 50), 0x80808080u, 1), "textureProj = texture at q (%08x %08x)",
          px(10, 10), px(50, 50));
    glDeleteTextures(1, &t);
}

/* gl_FragDepth (Doom 3 BFG's zcullReconstruct): the shader's depth decides */
static void test_frag_depth(void)
{
    const char* vs = "#version 150\n"
                     "in vec4 pos;\n"
                     "void main() { gl_Position = vec4(pos.xy, 0.8, 1.0); }\n"; /* behind the plain quad */
    const char* fs = "#version 150\n"
                     "uniform float d;\n"
                     "out vec4 col;\n"
                     "void main() { gl_FragDepth = d; col = vec4(0.0, 1.0, 0.0, 1.0); }\n";
    GLuint p = glCreateProgram_();
    glAttachShader_(p, compile(GL_VERTEX_SHADER, vs));
    glAttachShader_(p, compile(GL_FRAGMENT_SHADER, fs));
    glLinkProgram_(p);
    GLint ok = 0;
    glGetProgramiv_(p, GL_LINK_STATUS, &ok);
    CHECK(ok, "gl_FragDepth program links");
    if (!ok) return;
    glMatrixMode(GL_PROJECTION), glLoadIdentity(), glMatrixMode(GL_MODELVIEW), glLoadIdentity();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    for (int pass = 0; pass < 2; pass++) {
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glColor3f(1, 0, 0);
        glBegin(GL_QUADS); /* red at window depth 0.5 */
        glVertex3f(-1, -1, 0), glVertex3f(1, -1, 0), glVertex3f(1, 1, 0), glVertex3f(-1, 1, 0);
        glEnd();
        glUseProgram_(p);
        GLint loc = glGetUniformLocation_(p, "d");
        glUniform1f_(loc, pass ? 0.75f : 0.25f);
        glBegin(GL_QUADS);
        glVertex2f(-1, -1), glVertex2f(1, -1), glVertex2f(1, 1), glVertex2f(-1, 1);
        glEnd();
        glUseProgram_(0);
        uint32_t c = px(32, 32);
        CHECK(c == (pass ? 0xFF0000FFu : 0xFF00FF00u), "gl_FragDepth %s (%08x)", pass ? "0.75 is hidden" : "0.25 is in front", c);
    }
    glDisable(GL_DEPTH_TEST);
}

/* a vertex shader alone (Doom 3 BFG's shadow.vp): stencil only, colors masked */
static void test_vertex_only(void)
{
    const char* vs = "#version 150\n"
                     "in vec4 pos;\n"
                     "void main() { gl_Position = vec4(pos.xy * 0.5, 0.0, 1.0); }\n";
    GLuint p = glCreateProgram_();
    glAttachShader_(p, compile(GL_VERTEX_SHADER, vs));
    glLinkProgram_(p);
    GLint ok = 0;
    glGetProgramiv_(p, GL_LINK_STATUS, &ok);
    CHECK(ok, "a vertex shader alone links");
    if (!ok) return;
    glMatrixMode(GL_PROJECTION), glLoadIdentity(), glMatrixMode(GL_MODELVIEW), glLoadIdentity();
    glClearColor(0, 0, 0, 1);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glUseProgram_(p);
    glBegin(GL_QUADS); /* the middle half of the window */
    glVertex2f(-1, -1), glVertex2f(1, -1), glVertex2f(1, 1), glVertex2f(-1, 1);
    glEnd();
    glUseProgram_(0);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glColor3f(0, 1, 0);
    glBegin(GL_QUADS);
    glVertex2f(-1, -1), glVertex2f(1, -1), glVertex2f(1, 1), glVertex2f(-1, 1);
    glEnd();
    glDisable(GL_STENCIL_TEST);
    CHECK(px(32, 32) == 0xFF00FF00u && px(4, 4) == 0xFF000000u, "vertex shader only: stencil (%08x %08x)", px(32, 32), px(4, 4));
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

/* a full window quad through a program with one fragment shader; returns the program */
static GLuint fs_program(const char* fs)
{
    const char* vs = "#version 150\n"
                     "in vec4 pos;\n"
                     "void main() { gl_Position = pos; }\n";
    GLuint p = glCreateProgram_();
    glAttachShader_(p, compile(GL_VERTEX_SHADER, vs));
    glAttachShader_(p, compile(GL_FRAGMENT_SHADER, fs));
    glLinkProgram_(p);
    GLint ok = 0;
    glGetProgramiv_(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog_(p, sizeof(log), NULL, log);
        printf("  link log: %s\n", log);
        return 0;
    }
    return p;
}

static void full_quad(void)
{
    glMatrixMode(GL_PROJECTION), glLoadIdentity(), glMatrixMode(GL_MODELVIEW), glLoadIdentity();
    glBegin(GL_QUADS);
    glVertex2f(-1, -1), glVertex2f(1, -1), glVertex2f(1, 1), glVertex2f(-1, 1);
    glEnd();
}

static void nearest(GLenum target)
{
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

/* cube maps, 2D arrays, 3D textures (GLSL and ARB programs), straight alpha, swizzles, cube faces as render targets */
static void test_texture_targets(void)
{
    /* faces: +X red, -X green, +Y blue, -Y yellow, +Z cyan, -Z magenta (RGBA bytes as 0xAABBGGRR) */
    static const uint32_t fc[6] = { 0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u, 0xFF00FFFFu, 0xFFFFFF00u, 0xFFFF00FFu };
    static const float    dir[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    GLuint                tx[5];
    glGenTextures(5, tx);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tx[0]);
    for (int f = 0; f < 6; f++) {
        uint32_t img[4] = { fc[f], fc[f], fc[f], fc[f] };
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)f, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    }
    nearest(GL_TEXTURE_CUBE_MAP);
    GLint bound = 0;
    glGetIntegerv(0x8514, &bound); /* GL_TEXTURE_BINDING_CUBE_MAP */
    CHECK(bound == (GLint)tx[0] && glGetError() == GL_NO_ERROR, "cube map faces uploaded (binding %d)", bound);
    GLuint p = fs_program("#version 150\n"
                          "uniform samplerCube cube;\n"
                          "uniform vec3 dir;\n"
                          "out vec4 col;\n"
                          "void main() { col = texture(cube, dir); }\n");
    CHECK(p != 0, "samplerCube program links");
    if (p) {
        glUseProgram_(p);
        int bad = 0;
        for (int f = 0; f < 6; f++) {
            glUniform3f_(glGetUniformLocation_(p, "dir"), dir[f][0], dir[f][1], dir[f][2]);
            full_quad();
            if (px(32, 32) != fc[f]) bad++, printf("  face %d: %08x want %08x\n", f, px(32, 32), fc[f]);
        }
        glUseProgram_(0);
        CHECK(bad == 0, "samplerCube picks the faces (%d wrong)", bad);
    }
    /* the same cube map through an ARB fragment program (TEX ... CUBE) */
    const char* fp = "!!ARBfp1.0\n"
                     "TEX result.color, fragment.texcoord[0], texture[0], CUBE;\n"
                     "END\n";
    GLuint pr;
    glGenProgramsARB_(1, &pr);
    glBindProgramARB_(0x8804, pr);
    glProgramStringARB_(0x8804, 0x8875, (GLsizei)strlen(fp), fp);
    glEnable(0x8804);
    glMatrixMode(GL_PROJECTION), glLoadIdentity(), glMatrixMode(GL_MODELVIEW), glLoadIdentity();
    glBegin(GL_QUADS);
    glTexCoord3f(0, -1, 0), glVertex2f(-1, -1), glVertex2f(1, -1), glVertex2f(1, 1), glVertex2f(-1, 1);
    glEnd();
    glDisable(0x8804);
    CHECK(px(32, 32) == fc[3], "ARB TEX CUBE (%08x)", px(32, 32));
    /* a cube face as a render target: +Z cleared to white, then sampled */
    GLuint fb;
    glGenFramebuffers_(1, &fb);
    glBindFramebuffer_(GL_FRAMEBUFFER, fb);
    glFramebufferTexture2D_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + 4, tx[0], 0);
    GLenum st = glCheckFramebufferStatus_(GL_FRAMEBUFFER);
    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer_(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers_(1, &fb);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (p) {
        glUseProgram_(p);
        glUniform3f_(glGetUniformLocation_(p, "dir"), 0, 0, 1);
        full_quad();
        uint32_t a = px(32, 32);
        glUniform3f_(glGetUniformLocation_(p, "dir"), 0, 0, -1);
        full_quad();
        glUseProgram_(0);
        CHECK(st == GL_FRAMEBUFFER_COMPLETE && a == 0xFFFFFFFFu && px(32, 32) == fc[5], "render into a cube face (%04x %08x %08x)", st, a,
              px(32, 32));
    }
    /* a 2D array (3 layers) on unit 1 */
    uint32_t layers[3][4];
    for (int l = 0; l < 3; l++)
        for (int i = 0; i < 4; i++) layers[l][i] = fc[l];
    glActiveTextureARB_(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D_ARRAY, tx[1]);
    glTexImage3D_(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, 2, 2, 3, 0, GL_RGBA, GL_UNSIGNED_BYTE, layers);
    nearest(GL_TEXTURE_2D_ARRAY);
    glActiveTextureARB_(GL_TEXTURE0);
    GLuint pa = fs_program("#version 150\n"
                           "uniform sampler2DArray arr;\n"
                           "uniform vec3 dir;\n"
                           "out vec4 col;\n"
                           "void main() { col = texture(arr, vec3(0.5, 0.5, dir.x)); }\n");
    CHECK(pa != 0, "sampler2DArray program links");
    if (pa) {
        glUseProgram_(pa);
        glUniform1i_(glGetUniformLocation_(pa, "arr"), 1);
        int bad = 0;
        for (int l = 0; l < 3; l++) {
            glUniform3f_(glGetUniformLocation_(pa, "dir"), (float)l, 0, 0);
            full_quad();
            if (px(32, 32) != fc[l]) bad++, printf("  layer %d: %08x want %08x\n", l, px(32, 32), fc[l]);
        }
        glUseProgram_(0);
        CHECK(bad == 0, "sampler2DArray layers (%d wrong)", bad);
    }
    /* a 3D texture: 1 x 1 x 2, red then blue, linear between the slices */
    uint32_t vol[2] = { 0xFF0000FFu, 0xFFFF0000u };
    glBindTexture(GL_TEXTURE_3D, tx[2]);
    glTexImage3D_(GL_TEXTURE_3D, 0, GL_RGBA, 1, 1, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, vol);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    GLuint p3 = fs_program("#version 150\n"
                           "uniform sampler3D vol;\n"
                           "uniform vec3 dir;\n"
                           "out vec4 col;\n"
                           "void main() { col = texture(vol, vec3(0.5, 0.5, dir.x)); }\n");
    CHECK(p3 != 0, "sampler3D program links");
    if (p3) {
        glUseProgram_(p3);
        uint32_t got[3];
        for (int i = 0; i < 3; i++) {
            glUniform3f_(glGetUniformLocation_(p3, "dir"), 0.25f + 0.25f * (float)i, 0, 0);
            full_quad();
            got[i] = px(32, 32);
        }
        glUseProgram_(0);
        CHECK(close_to(got[0], vol[0], 1) && close_to(got[1], 0xFF800080u, 2) && close_to(got[2], vol[1], 1), "sampler3D slices (%08x %08x %08x)",
              got[0], got[1], got[2]);
    }
    /* straight alpha: shaders get the texel as stored (not un-premultiplied), and swizzles reorder it */
    uint32_t half = 0x40804020u; /* r 0x20 g 0x40 b 0x80 a 0x40 */
    glBindTexture(GL_TEXTURE_2D, tx[3]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &half);
    nearest(GL_TEXTURE_2D);
    GLuint p2 = fs_program("#version 150\n"
                           "uniform sampler2D tex;\n"
                           "out vec4 col;\n"
                           "void main() { col = texture(tex, vec2(0.5)); }\n");
    if (p2) {
        glUseProgram_(p2);
        full_quad();
        uint32_t a = px(32, 32);
        GLint    sw[4] = { GL_ALPHA, GL_BLUE, GL_GREEN, GL_ONE };
        glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, sw);
        full_quad();
        glUseProgram_(0);
        CHECK(close_to(a, half, 1), "shader texels keep straight alpha (%08x)", a);
        CHECK(close_to(px(32, 32), 0xFF408040u, 1), "GL_TEXTURE_SWIZZLE_RGBA (%08x)", px(32, 32));
    }
    glDeleteTextures(5, tx);
    CHECK(glGetError() == GL_NO_ERROR, "texture targets: no GL error");
}

/* glPolygonMode: edges (GL_LINE) and vertices (GL_POINT) instead of filled polygons */
static void test_polygon_mode(void)
{
    ortho();
    glDisable(GL_TEXTURE_2D);
    for (int pass = 0; pass < 2; pass++) {
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glPolygonMode(GL_FRONT_AND_BACK, pass ? GL_POINT : GL_LINE);
        glColor3f(0, 1, 0);
        glBegin(GL_QUADS);
        glVertex2f(8.5f, 8.5f), glVertex2f(55.5f, 8.5f), glVertex2f(55.5f, 55.5f), glVertex2f(8.5f, 55.5f);
        glEnd();
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        uint32_t e = px(8, 32), v = px(8, 8), m = px(32, 32), d = px(32, 31);
        if (pass == 0) CHECK(e == 0xFF00FF00u && m == 0xFF000000u && d == 0xFF000000u, "GL_LINE: edges only (%08x %08x %08x)", e, m, d);
        else CHECK(v == 0xFF00FF00u && e == 0xFF000000u && m == 0xFF000000u, "GL_POINT: vertices only (%08x %08x %08x)", v, e, m);
    }
    GLint pm[2] = { 0, 0 };
    glGetIntegerv(0x0B40, pm); /* GL_POLYGON_MODE */
    CHECK(pm[0] == GL_FILL && pm[1] == GL_FILL, "GL_POLYGON_MODE query");
    glColor3f(1, 1, 1);
}

/* mapped buffers are aligned like a driver's: Doom 3 BFG streams vertices into them with movntdq */
static void test_map_alignment(void)
{
    GLuint b[4];
    glGenBuffers_(4, b);
    int bad = 0;
    for (int i = 0; i < 4; i++) {
        glBindBuffer_(0x8892, b[i]); /* GL_ARRAY_BUFFER */
        glBufferData_(0x8892, 1000 + 24 * i, NULL, 0x88E8); /* GL_DYNAMIC_DRAW */
        void* p = glMapBufferRange_(0x8892, 0, 1000, 0x0002 | 0x0020); /* GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT */
        if (!p || ((uintptr_t)p & 63)) bad++;
        glUnmapBuffer_(0x8892);
    }
    GLint al = 0;
    glGetIntegerv(0x90BC, &al); /* GL_MIN_MAP_BUFFER_ALIGNMENT */
    glBindBuffer_(0x8892, 0);
    glDeleteBuffers_(4, b);
    CHECK(bad == 0 && al == 64, "mapped buffers are 64 byte aligned (%d not, %d)", bad, al);
}

static float h2f(uint16_t h) /* IEEE half to float (normal numbers) */
{
    uint32_t e = (h >> 10) & 31, m = h & 1023, b = (uint32_t)(h & 0x8000) << 16 | (e ? (e + 112) << 23 | m << 13 : 0);
    float    f;
    memcpy(&f, &b, 4);
    return f;
}
static uint16_t f2h(float f) /* float to IEEE half (normal numbers, truncated) */
{
    uint32_t b;
    memcpy(&b, &f, 4);
    uint32_t e = (b >> 23) & 255;
    return (uint16_t)((b >> 16) & 0x8000) | (uint16_t)(e < 113 ? 0 : ((e - 112) << 10 | (b >> 13 & 1023)));
}
static float dot3f(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static float dot4f(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]; }
static void  norm3(float* v)
{
    float l = sqrtf(dot3f(v, v));
    for (int k = 0; k < 3; k++) v[k] /= l;
}

/* Doom 3 BFG's light interaction (bump, falloff, projection, YCoCg diffuse,
 * specular) with its vertex layout (idDrawVert: float position, half
 * texcoords, byte normal / tangent / color) against the same math in C */
static void test_bfg_interaction(void)
{
    GLuint p = glCreateProgram_();
    glAttachShader_(p, compile(GL_VERTEX_SHADER, bfg_interaction_vertex));
    glAttachShader_(p, compile(GL_FRAGMENT_SHADER, bfg_interaction_fragment));
    /* the game's attribute indices (PC_ATTRIB_INDEX_*) */
    glBindAttribLocation_(p, 0, "in_Position");
    glBindAttribLocation_(p, 2, "in_Normal");
    glBindAttribLocation_(p, 3, "in_Color");
    glBindAttribLocation_(p, 4, "in_Color2");
    glBindAttribLocation_(p, 8, "in_TexCoord");
    glBindAttribLocation_(p, 9, "in_Tangent");
    glLinkProgram_(p);
    GLint ok = 0;
    glGetProgramiv_(p, GL_LINK_STATUS, &ok);
    CHECK(ok, "BFG interaction program links");
    if (!ok) return;
    /* 1 x 1 textures on units 0 .. 4: bump (normal in a and g), falloff, projection, YCoCg diffuse, specular */
    static const uint8_t tex[5][4] = { { 0x90, 0xA0, 0x00, 0x70 }, { 0xE0, 0xE0, 0xE0, 0xFF }, { 0xFF, 0xC0, 0x80, 0xFF },
                                       { 0x60, 0x90, 0x00, 0xB0 }, { 0x40, 0x50, 0x60, 0xFF } };
    GLuint t[5];
    glGenTextures(5, t);
    for (int i = 0; i < 5; i++) {
        glActiveTextureARB_(GL_TEXTURE0 + (GLenum)i);
        glBindTexture(GL_TEXTURE_2D, t[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex[i]);
        nearest(GL_TEXTURE_2D);
    }
    glActiveTextureARB_(GL_TEXTURE0);
    /* a slanted triangle pair over the window: idDrawVert, 32 bytes */
    typedef struct {
        float    xyz[3];
        uint16_t st[2];
        uint8_t  normal[4], tangent[4], color[4], color2[4];
    } drawvert;
    drawvert v[4];
    static const float pos[4][3] = { { -1, -1, 0.2f }, { 1, -1, 0.4f }, { 1, 1, 0.6f }, { -1, 1, 0.4f } };
    for (int i = 0; i < 4; i++) {
        memcpy(v[i].xyz, pos[i], 12);
        v[i].st[0] = f2h(0.25f), v[i].st[1] = f2h(0.75f);
        static const uint8_t n[4] = { 0xA0, 0x60, 0xE8, 0 }, tg[4] = { 0xF0, 0x90, 0x70, 0xFF }, col[4] = { 0xFF, 0xF0, 0xE0, 0xFF };
        memcpy(v[i].normal, n, 4), memcpy(v[i].tangent, tg, 4), memcpy(v[i].color, col, 4), memset(v[i].color2, 0, 4);
    }
    GLuint vb;
    glGenBuffers_(1, &vb);
    glBindBuffer_(0x8892, vb);
    glBufferData_(0x8892, sizeof(v), v, 0x88E8);
    glVertexAttribPointer_(0, 3, GL_FLOAT, GL_FALSE, 32, (const void*)0);
    glVertexAttribPointer_(8, 2, GL_HALF_FLOAT, GL_TRUE, 32, (const void*)12);
    glVertexAttribPointer_(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, (const void*)16);
    glVertexAttribPointer_(9, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, (const void*)20);
    glVertexAttribPointer_(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, (const void*)24);
    static const GLuint en[5] = { 0, 2, 3, 8, 9 };
    for (int i = 0; i < 5; i++) glEnableVertexAttribArray_(en[i]);
    /* uniforms: light / view origins, light and texture matrices, color modulate / add, MVP */
    float va[18][4] = {
        { 0.5f, -0.3f, 2.0f, 1.0f }, { -0.2f, 0.4f, 3.0f, 1.0f },                      /* light origin, view origin */
        { 0.2f, 0.1f, 0.0f, 0.5f }, { 0.0f, 0.3f, 0.1f, 0.5f }, { 0.0f, 0.0f, 0.1f, 1.0f }, /* projection S T Q */
        { 0.3f, 0.0f, 0.2f, 0.5f },                                                    /* falloff S */
        { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, /* bump, diffuse, specular */
        { 1, 1, 1, 1 }, { 0, 0, 0, 0 },                                                /* vertex color modulate, add */
        { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 0.5f, 0 }, { 0, 0, 0, 1 } };            /* MVP rows */
    float fa[2][4] = { { 1.2f, 1.1f, 1.0f, 1 }, { 0.9f, 1.0f, 1.1f, 1 } }; /* diffuse, specular modifiers */
    glUseProgram_(p);
    glUniform4fv_(glGetUniformLocation_(p, "_va_"), 18, &va[0][0]);
    glUniform4fv_(glGetUniformLocation_(p, "_fa_"), 2, &fa[0][0]);
    for (int i = 0; i < 5; i++) {
        char nm[8];
        snprintf(nm, sizeof(nm), "samp%d", i);
        glUniform1i_(glGetUniformLocation_(p, nm), i);
    }
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
    glUseProgram_(0);
    for (int i = 0; i < 5; i++) glDisableVertexAttribArray_(en[i]);
    glBindBuffer_(0x8892, 0);
    glDeleteBuffers_(1, &vb);
    /* the reference at a few pixels (the vertex attributes are the same everywhere; the position varies) */
    int bad = 0;
    for (int k = 0; k < 4; k++) {
        int   x = 8 + 16 * k, y = 12 + 13 * k;
        float fx = ((float)x + 0.5f) / 32.0f - 1.0f, fy = ((float)y + 0.5f) / 32.0f - 1.0f;
        /* the plane of the quad (bilinear in x, y: z = 0.4 + 0.1 x + 0.1 y) */
        float P[4] = { fx, fy, 0.4f + 0.1f * fx + 0.1f * fy, 1.0f };
        float N[3], T[4], B[3];
        for (int j = 0; j < 3; j++) N[j] = (float)v[0].normal[j] / 255.0f * 2.0f - 1.0f;
        for (int j = 0; j < 4; j++) T[j] = (float)v[0].tangent[j] / 255.0f * 2.0f - 1.0f;
        B[0] = (N[1] * T[2] - N[2] * T[1]) * T[3], B[1] = (N[2] * T[0] - N[0] * T[2]) * T[3], B[2] = (N[0] * T[1] - N[1] * T[0]) * T[3];
        float L[4], tl[3], hv[3] = { 0, 0, 0 };
        for (int j = 0; j < 4; j++) L[j] = va[0][j] - P[j];
        tl[0] = dot3f(T, L), tl[1] = dot3f(B, L), tl[2] = dot3f(N, L); /* linear in the position: exact per pixel */
        /* the half angle is normalized per vertex, then interpolated (triangles 0 1 2 and 0 2 3) */
        int   tri[3] = { 0, 1, 2 };
        float wb[3];
        if (fy <= fx) wb[2] = (fy + 1) * 0.5f, wb[1] = (fx - fy) * 0.5f;
        else tri[1] = 2, tri[2] = 3, wb[1] = (fx + 1) * 0.5f, wb[2] = (fy - fx) * 0.5f;
        wb[0] = 1.0f - wb[1] - wb[2];
        for (int c3 = 0; c3 < 3; c3++) {
            const float* Q = pos[tri[c3]];
            float        Lv[4] = { va[0][0] - Q[0], va[0][1] - Q[1], va[0][2] - Q[2], 0 }, Vv[4] = { va[1][0] - Q[0], va[1][1] - Q[1], va[1][2] - Q[2], 0 };
            float        ll = sqrtf(dot4f(Lv, Lv)), vl = sqrtf(dot4f(Vv, Vv)), H[3];
            for (int j = 0; j < 3; j++) H[j] = Lv[j] / ll + Vv[j] / vl;
            hv[0] += wb[c3] * dot3f(T, H), hv[1] += wb[c3] * dot3f(B, H), hv[2] += wb[c3] * dot3f(N, H);
        }
        /* fragment */
        float bump[4], ycc[4], spec[4], prj[4], fo[4];
        for (int j = 0; j < 4; j++)
            bump[j] = tex[0][j] / 255.0f, fo[j] = tex[1][j] / 255.0f, prj[j] = tex[2][j] / 255.0f, ycc[j] = tex[3][j] / 255.0f,
            spec[j] = tex[4][j] / 255.0f;
        norm3(tl);
        ycc[2] = 1.0f / (ycc[2] * 31.875f + 1.0f);
        ycc[0] *= ycc[2], ycc[1] *= ycc[2];
        float diff[3] = { ycc[0] - ycc[1] + ycc[3], ycc[1] - 0.50196078f * ycc[2] + ycc[3], -ycc[0] - ycc[1] + 1.00392156f * ycc[2] + ycc[3] };
        float ln[3]   = { bump[3] - 0.5f, bump[1] - 0.5f, 0 };
        ln[2]         = sqrtf(fabsf(ln[0] * ln[0] + ln[1] * ln[1] - 0.25f));
        norm3(ln);
        norm3(hv);
        float hdn = dot3f(hv, ln), sp = powf(hdn, 10.0f), lc = dot3f(tl, ln);
        float out[3], col[3] = { 1.0f, 0xF0 / 255.0f, 0xE0 / 255.0f };
        for (int j = 0; j < 3; j++) {
            float o = (diff[j] * fa[0][j] + spec[j] * sp * fa[1][j]) * lc * prj[j] * fo[j] * col[j];
            out[j]  = o < 0 ? 0 : (o > 1 ? 1 : o);
        }
        uint32_t want = 0xFF000000u | (uint32_t)(out[2] * 255 + 0.5f) << 16 | (uint32_t)(out[1] * 255 + 0.5f) << 8 | (uint32_t)(out[0] * 255 + 0.5f);
        uint32_t got  = px(x, y);
        if (!close_to(got, want, 3)) bad++, printf("  pixel %d,%d: %08x want %08x\n", x, y, got, want);
    }
    CHECK(bad == 0, "BFG interaction matches the reference (%d wrong)", bad);
    glDeleteTextures(5, t);
    (void)h2f;
}

/* GL_CLAMP_TO_BORDER (Doom 3's zero clamped light images) and the channels of
 * legacy internal formats (GL_INTENSITY8 from luminance data, GL_RGB from RGBA data) */
static void test_border_and_formats(void)
{
    GLuint p = fs_program("#version 150\n"
                          "uniform sampler2D tex;\n"
                          "uniform vec3 dir;\n"
                          "out vec4 col;\n"
                          "void main() { col = texture(tex, dir.xy); }\n");
    CHECK(p != 0, "sampling program links");
    if (!p) return;
    uint32_t white[4] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
    GLuint   t[3];
    glGenTextures(3, t);
    glBindTexture(GL_TEXTURE_2D, t[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    nearest(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812D); /* GL_CLAMP_TO_BORDER */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812D);
    glUseProgram_(p);
    GLint dir = glGetUniformLocation_(p, "dir");
    glUniform3f_(dir, 0.5f, 0.5f, 0);
    full_quad();
    uint32_t in = px(32, 32);
    glUniform3f_(dir, 1.5f, 0.5f, 0);
    full_quad();
    uint32_t out = px(32, 32);
    CHECK(in == 0xFFFFFFFFu && (out & 0x00FFFFFFu) == 0, "GL_CLAMP_TO_BORDER: inside %08x, outside black %08x", in, out);
    uint8_t lum = 0x60;
    glBindTexture(GL_TEXTURE_2D, t[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, 0x804B /* GL_INTENSITY8 */, 1, 1, 0, 0x1909 /* GL_LUMINANCE */, GL_UNSIGNED_BYTE, &lum);
    nearest(GL_TEXTURE_2D);
    glUniform3f_(dir, 0.5f, 0.5f, 0);
    full_quad();
    uint32_t it = px(32, 32);
    uint32_t half = 0x40804020u;
    glBindTexture(GL_TEXTURE_2D, t[2]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &half);
    nearest(GL_TEXTURE_2D);
    full_quad();
    uint32_t rgb = px(32, 32);
    glUseProgram_(0);
    /* Doom 3 BFG's light images: big endian RGB565 with GL_UNPACK_SWAP_BYTES */
    uint16_t c565 = (uint16_t)(20u << 11 | 40u << 5 | 10u);
    uint8_t  be[2] = { (uint8_t)(c565 >> 8), (uint8_t)c565 };
    glPixelStorei(0x0CF0 /* GL_UNPACK_SWAP_BYTES */, GL_TRUE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, be);
    glPixelStorei(0x0CF0, GL_FALSE);
    glUseProgram_(p);
    full_quad();
    glUseProgram_(0);
    uint32_t sw = px(32, 32), want565 = 0xFF000000u | (10u * 255 / 31) << 16 | (40u * 255 / 63) << 8 | (20u * 255 / 31);
    CHECK(close_to(sw, want565, 1), "GL_UNPACK_SWAP_BYTES RGB565 (%08x want %08x)", sw, want565);
    /* glCopyTexImage2D(GL_DEPTH_COMPONENT): the depth buffer (Doom 3 BFG's _currentDepth) */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glMatrixMode(GL_PROJECTION), glLoadIdentity(), glMatrixMode(GL_MODELVIEW), glLoadIdentity();
    glBegin(GL_QUADS); /* window depth 0.25 */
    glVertex3f(-1, -1, -0.5f), glVertex3f(1, -1, -0.5f), glVertex3f(1, 1, -0.5f), glVertex3f(-1, 1, -0.5f);
    glEnd();
    glDisable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, 0x1902 /* GL_DEPTH_COMPONENT */, 0, 0, 64, 64, 0);
    glUseProgram_(p);
    glUniform3f_(dir, 0.5f, 0.5f, 0);
    full_quad();
    glUseProgram_(0);
    uint32_t dc = px(32, 32);
    CHECK(close_to(dc, 0xFF404040u, 2) && glGetError() == GL_NO_ERROR, "glCopyTexImage2D of the depth buffer (%08x)", dc);
    CHECK(it == 0x60606060u, "GL_INTENSITY8 from luminance (%08x)", it);
    CHECK(rgb == 0xFF804020u, "GL_RGB keeps no alpha (%08x)", rgb);
    glDeleteTextures(3, t);
}

/* bilinear magnification of a gray ramp keeps r == g == b (Doom 3's falloff and
 * spot light images are tiny and magnified; plain, projective and clamped to border) */
static void test_bilinear_gray(void)
{
    static const char* fs[3] = { "#version 150\n"
                                 "uniform sampler2D tex;\n"
                                 "out vec4 col;\n"
                                 "void main() { col = texture(tex, vec2(gl_FragCoord.x / 64.0 * 1.4 - 0.2, 0.3)); }\n",
                                 "#version 150\n"
                                 "uniform sampler2D tex;\n"
                                 "out vec4 col;\n"
                                 "void main() { float q = 0.5 + gl_FragCoord.y / 64.0; col = textureProj(tex, vec3((gl_FragCoord.x / 64.0) * q, 0.5 * q, q)); }\n",
                                 "#version 150\n"
                                 "uniform sampler2D tex;\n"
                                 "out vec4 col;\n"
                                 "void main() { col = texture(tex, vec2(gl_FragCoord.x / 64.0 * 1.4 - 0.2, gl_FragCoord.y / 64.0 * 1.4 - 0.2)); }\n" };
    static const GLenum wraps[3] = { GL_CLAMP_TO_EDGE, 0x812D, GL_REPEAT };
    uint8_t ramp[4 * 4 * 4];
    for (int i = 0; i < 16; i++) {
        uint8_t v = (uint8_t)(i * 17);
        ramp[4 * i] = ramp[4 * i + 1] = ramp[4 * i + 2] = v, ramp[4 * i + 3] = 255;
    }
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, ramp);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    for (int f = 0; f < 3; f++) {
        GLuint p = fs_program(fs[f]);
        CHECK(p != 0, "bilinear program %d links", f);
        if (!p) continue;
        for (int w = 0; w < 3; w++)
            for (int m = 0; m < 2; m++) {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, m ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)wraps[w]);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)wraps[w]);
                glUseProgram_(p);
                full_quad();
                glUseProgram_(0);
                static uint32_t img[64 * 64];
                glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, img);
                int bad = 0, bx = 0, by = 0;
                for (int i = 0; i < 64 * 64; i++) {
                    int r = (int)(img[i] & 255), g = (int)((img[i] >> 8) & 255), b = (int)((img[i] >> 16) & 255);
                    if (abs(r - g) > 1 || abs(b - g) > 1) {
                        if (!bad) bx = i % 64, by = i / 64;
                        bad++;
                    }
                }
                CHECK(bad == 0, "bilinear gray stays gray (shader %d, wrap %04x, %s): %d pixels off, first %d,%d = %08x", f, wraps[w],
                      m ? "trilinear" : "linear", bad, bx, by, img[by * 64 + bx]);
            }
    }
    glDeleteTextures(1, &t);
}

/* glCopyTexImage2D of the window sampled at gl_FragCoord / size reproduces it
 * (Doom 3 BFG's _currentRender: glass, heat haze, post processing) */
static void test_screen_copy(void)
{
    GLuint p = fs_program("#version 150\n"
                          "uniform sampler2D tex;\n"
                          "out vec4 col;\n"
                          "void main() { col = texture(tex, gl_FragCoord.xy * vec2(1.0 / 64.0)); }\n");
    CHECK(p != 0, "screen copy program links");
    if (!p) return;
    ortho();
    glDisable(GL_TEXTURE_2D);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(1, 0, 0), quad(0, 0, 32, 32);    /* red: bottom left */
    glColor3f(0, 1, 0), quad(32, 0, 64, 32);   /* green: bottom right */
    glColor3f(0, 0, 1), quad(0, 32, 32, 64);   /* blue: top left */
    glColor3f(1, 1, 1), quad(32, 32, 64, 64);  /* white: top right */
    glColor3f(1, 1, 1);
    static uint32_t before[64 * 64], after[64 * 64];
    glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, before);
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, 0x8058 /* GL_RGBA8 */, 0, 0, 64, 64, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram_(p);
    full_quad();
    glUseProgram_(0);
    glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, after);
    int bad = 0, first = -1;
    for (int i = 0; i < 64 * 64; i++)
        if (!close_to(before[i], after[i], 2)) bad++, first = first < 0 ? i : first;
    CHECK(bad == 0, "screen copy sampled at gl_FragCoord matches (%d off; first %d,%d: %08x was %08x)", bad, first % 64, first / 64,
          first >= 0 ? after[first] : 0, first >= 0 ? before[first] : 0);
    glDeleteTextures(1, &t);
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
    LOAD(glGetUniformLocation), LOAD(glUniform1f);
    LOAD(glUseProgram), LOAD(glActiveTextureARB), LOAD(glMultiTexCoord2fARB);
    LOAD(glGenProgramsARB), LOAD(glBindProgramARB), LOAD(glProgramStringARB), LOAD(glProgramEnvParameter4fARB);
    LOAD(glProgramLocalParameter4fARB);
    LOAD(glUniform1i), LOAD(glUniform3f), LOAD(glTexImage3D), LOAD(glGenFramebuffers), LOAD(glBindFramebuffer);
    LOAD(glFramebufferTexture2D), LOAD(glCheckFramebufferStatus), LOAD(glDeleteFramebuffers);
    LOAD(glBindAttribLocation), LOAD(glVertexAttribPointer), LOAD(glEnableVertexAttribArray), LOAD(glDisableVertexAttribArray);
    LOAD(glUniform4fv);
    LOAD(glGenBuffers), LOAD(glBindBuffer), LOAD(glBufferData), LOAD(glMapBufferRange), LOAD(glUnmapBuffer), LOAD(glDeleteBuffers);
    const char* ext = (const char*)glGetString(GL_EXTENSIONS);
    CHECK(ext && strstr(ext, "GL_ARB_multitexture") && strstr(ext, "GL_EXT_texture_compression_s3tc"), "GL_EXTENSIONS");
    CHECK(glActiveTextureARB_ && glMultiTexCoord2fARB_, "ARB multitexture entry points");
    test_pixels();
    test_raster_state();
    test_multitexture();
    test_arb_programs();
    test_legacy_glsl();
    test_bfg_glsl();
    test_texture_proj();
    test_frag_depth();
    test_vertex_only();
    test_texture_targets();
    test_polygon_mode();
    test_map_alignment();
    test_bfg_interaction();
    test_border_and_formats();
    test_bilinear_gray();
    test_screen_copy();
    CHECK(glGetError() == GL_NO_ERROR, "no GL error at the end (%04x)", glGetError());
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(rc);
    ReleaseDC(win, dc);
    DestroyWindow(win);
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
