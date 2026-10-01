/* Doom 3 BFG style light interactions through the system opengl32.dll (fatgl's
 * when it sits next to the executable): 1280 x 720, a 64 x 36 grid, 4 additive
 * light passes with 5 trilinear textures each. bench_interaction.py renders the
 * same scene on Mesa llvmpipe. Prints the best frame time and writes
 * bench_interaction.ppm.
 *   bench_interaction <dir with interaction.vert / .frag> [frames]
 * FATGL_THREADS=1 for one render thread. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench_scene.h"

#define GL_TEXTURE0 0x84C0
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
GLP(void, glUniform4fv, (GLint, GLsizei, const float*))
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
#define LD(n) n##_ = (T_##n)(void*)wglGetProcAddress(#n)

static char* slurp(const char* dir, const char* name)
{
    char path[512];
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

static GLuint shader(GLenum type, const char* src)
{
    GLuint s = glCreateShader_(type);
    glShaderSource_(s, 1, &src, NULL);
    glCompileShader_(s);
    GLint ok = 0;
    glGetShaderiv_(s, 0x8B81, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog_(s, sizeof(log), NULL, log);
        printf("compile: %s\n", log);
    }
    return s;
}

static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM wp, LPARAM lp) { return DefWindowProcA(w, m, wp, lp); }

int main(int argc, char** argv)
{
    const char* dir    = argc > 1 ? argv[1] : ".";
    int         frames = argc > 2 ? atoi(argv[2]) : 10;
    WNDCLASSA   wc     = { 0 };
    wc.lpfnWndProc = proc, wc.hInstance = GetModuleHandleA(NULL), wc.lpszClassName = "bench_interaction";
    RegisterClassA(&wc);
    HWND win = CreateWindowA("bench_interaction", "bench", WS_POPUP, 0, 0, SCENE_W, SCENE_H, NULL, NULL, wc.hInstance, NULL); /* never shown */
    HDC  dc  = GetDC(win);
    PIXELFORMATDESCRIPTOR pfd = { 0 };
    pfd.nSize = sizeof(pfd), pfd.nVersion = 1, pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.cColorBits = 32, pfd.cDepthBits = 24;
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
    HGLRC rc = wglCreateContext(dc);
    wglMakeCurrent(dc, rc);
    printf("renderer: %s\n", (const char*)glGetString(GL_RENDERER));
    LD(glCreateShader), LD(glShaderSource), LD(glCompileShader), LD(glGetShaderiv), LD(glGetShaderInfoLog), LD(glCreateProgram);
    LD(glAttachShader), LD(glLinkProgram), LD(glGetProgramiv), LD(glGetProgramInfoLog), LD(glUseProgram), LD(glGetUniformLocation);
    LD(glUniform4fv), LD(glUniform1i), LD(glGenBuffers), LD(glBindBuffer), LD(glBufferData), LD(glVertexAttribPointer);
    LD(glEnableVertexAttribArray), LD(glActiveTexture), LD(glGenerateMipmap), LD(glGenVertexArrays), LD(glBindVertexArray);
    char *vs = slurp(dir, "interaction.vert"), *fs = slurp(dir, "interaction.frag");
    if (!vs || !fs) {
        printf("interaction.vert / .frag not found in %s\n", dir);
        return 1;
    }
    GLuint p = glCreateProgram_();
    glAttachShader_(p, shader(0x8B31, vs));
    glAttachShader_(p, shader(0x8B30, fs));
    glLinkProgram_(p);
    GLint ok = 0;
    glGetProgramiv_(p, 0x8B82, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog_(p, sizeof(log), NULL, log);
        printf("link: %s\n", log);
        return 1;
    }
    /* textures (bench_scene.h: the same bytes bench_interaction.py makes) */
    GLuint tex[5];
    glGenTextures(5, tex);
    for (int i = 0; i < 5; i++) {
        int      w = scene_tex_w(i), h = scene_tex_h(i);
        uint8_t* img = (uint8_t*)malloc((size_t)w * (size_t)h * 4);
        scene_texture(i, img);
        glActiveTexture_(GL_TEXTURE0 + (GLenum)i);
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
    /* the grid */
    static scene_vert verts[SCENE_NV];
    static uint32_t   idx[SCENE_NI];
    scene_mesh(verts, idx);
    GLuint vao, vb, ib;
    glGenVertexArrays_(1, &vao);
    glBindVertexArray_(vao);
    glGenBuffers_(1, &vb), glGenBuffers_(1, &ib);
    glBindBuffer_(0x8892, vb);
    glBufferData_(0x8892, sizeof(verts), verts, 0x88E4);
    glBindBuffer_(0x8893, ib);
    glBufferData_(0x8893, sizeof(idx), idx, 0x88E4);
    glVertexAttribPointer_(0, 4, GL_FLOAT, GL_FALSE, sizeof(scene_vert), (const void*)offsetof(scene_vert, xyzw));
    glVertexAttribPointer_(8, 2, GL_FLOAT, GL_FALSE, sizeof(scene_vert), (const void*)offsetof(scene_vert, st));
    glVertexAttribPointer_(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(scene_vert), (const void*)offsetof(scene_vert, normal));
    glVertexAttribPointer_(9, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(scene_vert), (const void*)offsetof(scene_vert, tangent));
    glVertexAttribPointer_(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(scene_vert), (const void*)offsetof(scene_vert, color));
    static const GLuint en[5] = { 0, 8, 2, 9, 3 };
    for (int i = 0; i < 5; i++) glEnableVertexAttribArray_(en[i]);
    glUseProgram_(p);
    for (int i = 0; i < 5; i++) {
        char nm[8];
        snprintf(nm, sizeof(nm), "samp%d", i);
        glUniform1i_(glGetUniformLocation_(p, nm), i);
    }
    GLint lva = glGetUniformLocation_(p, "_va_"), lfa = glGetUniformLocation_(p, "_fa_");
    glViewport(0, 0, SCENE_W, SCENE_H);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    double best = 1e30;
    LARGE_INTEGER fq;
    QueryPerformanceFrequency(&fq);
    for (int f = 0; f < frames + 1; f++) { /* frame 0: warm up */
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        for (int l = 0; l < SCENE_LIGHTS; l++) {
            float va[18][4], fa[2][4];
            scene_uniforms(l, va, fa);
            glUniform4fv_(lva, 18, &va[0][0]);
            glUniform4fv_(lfa, 2, &fa[0][0]);
            glDrawElements(GL_TRIANGLES, SCENE_NI, GL_UNSIGNED_INT, NULL);
        }
        glFinish();
        QueryPerformanceCounter(&t1);
        double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)fq.QuadPart;
        if (f > 0 && ms < best) best = ms;
    }
    static uint8_t img[SCENE_W * SCENE_H * 4];
    glReadPixels(0, 0, SCENE_W, SCENE_H, GL_RGBA, GL_UNSIGNED_BYTE, img);
    double sum[3] = { 0, 0, 0 };
    for (int i = 0; i < SCENE_W * SCENE_H; i++)
        for (int k = 0; k < 3; k++) sum[k] += img[4 * i + k];
    printf("interaction %dx%d, %d lights: %.2f ms (best of %d)\n", SCENE_W, SCENE_H, SCENE_LIGHTS, best, frames);
    printf("mean color %.2f %.2f %.2f\n", sum[0] / (SCENE_W * SCENE_H), sum[1] / (SCENE_W * SCENE_H), sum[2] / (SCENE_W * SCENE_H));
    FILE* o = fopen("bench_interaction.ppm", "wb");
    if (o) {
        fprintf(o, "P6 %d %d 255\n", SCENE_W, SCENE_H);
        for (int y = SCENE_H - 1; y >= 0; y--) /* GL rows are bottom up */
            for (int x = 0; x < SCENE_W; x++) fwrite(img + ((size_t)y * SCENE_W + (size_t)x) * 4, 1, 3, o);
        fclose(o);
    }
    return 0;
}
