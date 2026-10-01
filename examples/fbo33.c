/* fatgl example: render to texture (OpenGL 3.3 core). Each frame the cube
 * of cube33 is drawn into a framebuffer object (a color texture + a depth
 * renderbuffer); the window shows a second cube textured with that image,
 * and glBlitFramebuffer copies the image into the lower left corner. */
#include "win.h"
#include <math.h>
#include <stdint.h>
#include <stddef.h>

typedef char      GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
#define GL_ARRAY_BUFFER         0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_UNIFORM_BUFFER       0x8A11
#define GL_STATIC_DRAW          0x88E4
#define GL_DYNAMIC_DRAW         0x88E8
#define GL_FRAGMENT_SHADER      0x8B30
#define GL_VERTEX_SHADER        0x8B31
#define GL_COMPILE_STATUS       0x8B81
#define GL_LINK_STATUS          0x8B82
#define GL_TEXTURE0             0x84C0
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_FRAMEBUFFER          0x8D40
#define GL_RENDERBUFFER         0x8D41
#define GL_READ_FRAMEBUFFER     0x8CA8
#define GL_DRAW_FRAMEBUFFER     0x8CA9
#define GL_COLOR_ATTACHMENT0    0x8CE0
#define GL_DEPTH_ATTACHMENT     0x8D00
#define GL_DEPTH_COMPONENT24    0x81A6
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5

#define GLFN(ret, name, args) typedef ret(APIENTRY* PFN_##name) args; static PFN_##name name##_;
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
GLFN(void, glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*))
GLFN(void, glUniform1i, (GLint, GLint))
GLFN(void, glUniform1f, (GLint, GLfloat))
GLFN(GLuint, glGetUniformBlockIndex, (GLuint, const GLchar*))
GLFN(void, glUniformBlockBinding, (GLuint, GLuint, GLuint))
GLFN(void, glGenVertexArrays, (GLsizei, GLuint*))
GLFN(void, glBindVertexArray, (GLuint))
GLFN(void, glGenBuffers, (GLsizei, GLuint*))
GLFN(void, glBindBuffer, (GLenum, GLuint))
GLFN(void, glBindBufferBase, (GLenum, GLuint, GLuint))
GLFN(void, glBufferData, (GLenum, GLsizeiptr, const void*, GLenum))
GLFN(void, glBufferSubData, (GLenum, GLintptr, GLsizeiptr, const void*))
GLFN(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*))
GLFN(void, glEnableVertexAttribArray, (GLuint))
GLFN(void, glActiveTexture, (GLenum))
GLFN(void, glGenerateMipmap, (GLenum))
GLFN(void, glGenFramebuffers, (GLsizei, GLuint*))
GLFN(void, glBindFramebuffer, (GLenum, GLuint))
GLFN(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))
GLFN(void, glGenRenderbuffers, (GLsizei, GLuint*))
GLFN(void, glBindRenderbuffer, (GLenum, GLuint))
GLFN(void, glRenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei))
GLFN(void, glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))
GLFN(GLenum, glCheckFramebufferStatus, (GLenum))
GLFN(void, glBlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum))
typedef HGLRC(WINAPI* PFN_wglCreateContextAttribsARB)(HDC, HGLRC, const int*);

static void fail(const char* what, const char* msg) { ex_fail(what, msg); }

static void* load(const char* n)
{
    void* p = (void*)wglGetProcAddress(n);
    if (!p) {
        fail("missing entry point", n);
    }
    return p;
}
#define LOAD(name) name##_ = (PFN_##name)load(#name)

static const char* vs_src = "#version 330 core\n"
                            "layout(location = 0) in vec3 aPos;\n"
                            "layout(location = 1) in vec3 aNormal;\n"
                            "layout(location = 2) in vec2 aUv;\n"
                            "uniform mat4 uMvp;\n"
                            "uniform mat4 uModel;\n"
                            "out vec3 vNormal;\n"
                            "out vec2 vUv;\n"
                            "void main() {\n"
                            "    vNormal = mat3(uModel) * aNormal;\n"
                            "    vUv = aUv;\n"
                            "    gl_Position = uMvp * vec4(aPos, 1.0);\n"
                            "}\n";
static const char* fs_src = "#version 330 core\n"
                            "in vec3 vNormal;\n"
                            "in vec2 vUv;\n"
                            "uniform sampler2D uTex;\n"
                            "uniform float uTime;\n"
                            "layout(std140) uniform Light { vec4 dir; vec4 color; vec4 ambient; };\n"
                            "out vec4 fragColor;\n"
                            "float rim(vec3 n) { return pow(1.0 - abs(n.z), 3.0); }\n"
                            "void main() {\n"
                            "    vec3 n = normalize(vNormal);\n"
                            "    float d = max(dot(n, normalize(dir.xyz)), 0.0);\n"
                            "    vec3 tex = texture(uTex, vUv).rgb;\n"
                            "    vec3 c = tex * (ambient.rgb + color.rgb * d) + rim(n) * vec3(0.2, 0.4, 0.9) * (0.6 + 0.4 * sin(uTime * 3.0));\n"
                            "    fragColor = vec4(c, 1.0);\n"
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
        fail("shader", log);
    }
    return s;
}

/* column major 4x4 */
static void mul(float* r, const float* a, const float* b)
{
    float t[16];
    for (int c = 0; c < 4; c++)
        for (int k = 0; k < 4; k++) t[c * 4 + k] = a[k] * b[c * 4] + a[4 + k] * b[c * 4 + 1] + a[8 + k] * b[c * 4 + 2] + a[12 + k] * b[c * 4 + 3];
    memcpy(r, t, sizeof(t));
}
static void rot(float* m, float a, float x, float y, float z)
{
    float c = cosf(a), s = sinf(a), C = 1 - c;
    float r[16] = { x * x * C + c, y * x * C + z * s, z * x * C - y * s, 0, x * y * C - z * s, y * y * C + c, z * y * C + x * s, 0,
                    x * z * C + y * s, y * z * C - x * s, z * z * C + c, 0, 0, 0, 0, 1 };
    memcpy(m, r, sizeof(r));
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)inst, (void)prev, (void)cmd, (void)show;
    HDC dc = ex_open("fatgl render to texture (OpenGL 3.3 core)"); /* a legacy context first, for the ARB entry point */
    if (!dc) return 1;
    PFN_wglCreateContextAttribsARB create = (PFN_wglCreateContextAttribsARB)(void*)wglGetProcAddress("wglCreateContextAttribsARB");
    const int attribs[] = { 0x2091, 3, 0x2092, 3, 0x9126, 1, 0 }; /* 3.3, core profile */
    HGLRC     legacy = wglGetCurrentContext(), core = create ? create(dc, NULL, attribs) : NULL;
    if (!core) {
        fail("context", "no OpenGL 3.3 core context");
    }
    wglMakeCurrent(dc, core);
    wglDeleteContext(legacy);
    LOAD(glCreateShader), LOAD(glShaderSource), LOAD(glCompileShader), LOAD(glGetShaderiv), LOAD(glGetShaderInfoLog);
    LOAD(glCreateProgram), LOAD(glAttachShader), LOAD(glLinkProgram), LOAD(glGetProgramiv), LOAD(glGetProgramInfoLog);
    LOAD(glUseProgram), LOAD(glGetUniformLocation), LOAD(glUniformMatrix4fv), LOAD(glUniform1i), LOAD(glUniform1f);
    LOAD(glGetUniformBlockIndex), LOAD(glUniformBlockBinding), LOAD(glGenVertexArrays), LOAD(glBindVertexArray);
    LOAD(glGenBuffers), LOAD(glBindBuffer), LOAD(glBindBufferBase), LOAD(glBufferData), LOAD(glBufferSubData);
    LOAD(glVertexAttribPointer), LOAD(glEnableVertexAttribArray), LOAD(glActiveTexture), LOAD(glGenerateMipmap);
    LOAD(glGenFramebuffers), LOAD(glBindFramebuffer), LOAD(glFramebufferTexture2D), LOAD(glGenRenderbuffers);
    LOAD(glBindRenderbuffer), LOAD(glRenderbufferStorage), LOAD(glFramebufferRenderbuffer), LOAD(glCheckFramebufferStatus);
    LOAD(glBlitFramebuffer);

    /* program */
    GLuint prog = glCreateProgram_();
    glAttachShader_(prog, shader(GL_VERTEX_SHADER, vs_src));
    glAttachShader_(prog, shader(GL_FRAGMENT_SHADER, fs_src));
    glLinkProgram_(prog);
    GLint ok = 0;
    glGetProgramiv_(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog_(prog, sizeof(log), NULL, log);
        fail("link", log);
    }

    /* a cube: 24 vertices (position, normal, uv), 36 indices */
    float    v[24 * 8];
    uint16_t idx[36];
    static const float n6[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (int f = 0; f < 6; f++) {
        const float* n = n6[f];
        float        u[3] = { n[1] + n[2] != 0 ? 1.0f : 0.0f, 0, n[0] != 0 ? 1.0f : 0.0f }, w[3];
        if (n[2] != 0) u[0] = 1, u[2] = 0;
        w[0] = n[1] * u[2] - n[2] * u[1], w[1] = n[2] * u[0] - n[0] * u[2], w[2] = n[0] * u[1] - n[1] * u[0];
        for (int k = 0; k < 4; k++) {
            float  a = (k == 1 || k == 2) ? 1.0f : -1.0f, b = k >= 2 ? 1.0f : -1.0f;
            float* o = v + (f * 4 + k) * 8;
            for (int j = 0; j < 3; j++) o[j] = n[j] + a * u[j] + b * w[j], o[3 + j] = n[j];
            o[6] = (a + 1) * 0.5f, o[7] = (b + 1) * 0.5f;
        }
        uint16_t q[6] = { 0, 1, 2, 0, 2, 3 };
        for (int k = 0; k < 6; k++) idx[f * 6 + k] = (uint16_t)(f * 4 + q[k]);
    }
    GLuint vao, vbo, ebo, ubo, tex;
    glGenVertexArrays_(1, &vao);
    glBindVertexArray_(vao);
    glGenBuffers_(1, &vbo);
    glBindBuffer_(GL_ARRAY_BUFFER, vbo);
    glBufferData_(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glGenBuffers_(1, &ebo);
    glBindBuffer_(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData_(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
    glVertexAttribPointer_(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0);
    glVertexAttribPointer_(1, 3, GL_FLOAT, GL_FALSE, 32, (void*)12);
    glVertexAttribPointer_(2, 2, GL_FLOAT, GL_FALSE, 32, (void*)24);
    for (int i = 0; i < 3; i++) glEnableVertexAttribArray_((GLuint)i);

    /* uniform block (std140: three vec4) at binding point 2 */
    float light[12] = { 0.4f, 0.7f, 0.8f, 0, 1.0f, 0.95f, 0.85f, 1, 0.18f, 0.18f, 0.22f, 1 };
    glGenBuffers_(1, &ubo);
    glBindBuffer_(GL_UNIFORM_BUFFER, ubo);
    glBufferData_(GL_UNIFORM_BUFFER, sizeof(light), light, GL_DYNAMIC_DRAW);
    glBindBufferBase_(GL_UNIFORM_BUFFER, 2, ubo);
    glUniformBlockBinding_(prog, glGetUniformBlockIndex_(prog, "Light"), 2);

    /* a checkerboard with a border, mipmapped, on texture unit 3 */
    static uint8_t img[128 * 128 * 4];
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++) {
            uint8_t* p    = img + (y * 128 + x) * 4;
            int      edge = x < 6 || y < 6 || x >= 122 || y >= 122, ch = ((x >> 4) ^ (y >> 4)) & 1;
            p[0] = edge ? 40 : (ch ? 240 : 230), p[1] = edge ? 40 : (ch ? 180 : 90), p[2] = edge ? 50 : (ch ? 60 : 50), p[3] = 255;
        }
    glActiveTexture_(GL_TEXTURE0 + 3);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap_(GL_TEXTURE_2D);

    /* the render target: a 256 x 256 texture on unit 4 + a depth renderbuffer */
    enum { RT = 256 };
    GLuint fbo, rtex, rdepth;
    glActiveTexture_(GL_TEXTURE0 + 4);
    glGenTextures(1, &rtex);
    glBindTexture(GL_TEXTURE_2D, rtex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, RT, RT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenRenderbuffers_(1, &rdepth);
    glBindRenderbuffer_(GL_RENDERBUFFER, rdepth);
    glRenderbufferStorage_(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, RT, RT);
    glGenFramebuffers_(1, &fbo);
    glBindFramebuffer_(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rtex, 0);
    glFramebufferRenderbuffer_(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rdepth);
    if (glCheckFramebufferStatus_(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) fail("framebuffer", "not complete");
    glBindFramebuffer_(GL_FRAMEBUFFER, 0);

    glUseProgram_(prog);
    GLint ltex = glGetUniformLocation_(prog, "uTex");
    GLint lmvp = glGetUniformLocation_(prog, "uMvp"), lmodel = glGetUniformLocation_(prog, "uModel"), ltime = glGetUniformLocation_(prog, "uTime");
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    while (ex_poll()) {
        float t = (float)ex_time();
        for (int pass = 0; pass < 2; pass++) { /* 0: the cube into the texture, 1: a cube with that texture */
            int   w = pass ? g_w : RT, h = pass ? g_h : RT;
            float a = (float)w / (float)(h > 0 ? h : 1), s = pass ? -0.6f : 1.0f;
            float model[16], r2[16], view[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -5, 1 }, proj[16], mvp[16];
            rot(model, t * 0.7f * s, 0, 1, 0);
            rot(r2, 0.5f + 0.3f * sinf(t * 0.4f), 1, 0, 0);
            mul(model, r2, model);
            float fz = 1.0f / tanf(0.5f * 0.9f), nz = 0.1f, fa = 50.0f;
            float p[16] = { fz / a, 0, 0, 0, 0, fz, 0, 0, 0, 0, (fa + nz) / (nz - fa), -1, 0, 0, 2 * fa * nz / (nz - fa), 0 };
            memcpy(proj, p, sizeof(p));
            mul(mvp, view, model);
            mul(mvp, proj, mvp);
            glBindFramebuffer_(GL_FRAMEBUFFER, pass ? 0 : fbo);
            glViewport(0, 0, w, h);
            if (pass) glClearColor(0.07f, 0.08f, 0.11f, 1);
            else glClearColor(0.25f, 0.45f, 0.75f, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            if (pass) { /* mipmaps of what was just drawn */
                glActiveTexture_(GL_TEXTURE0 + 4);
                glGenerateMipmap_(GL_TEXTURE_2D);
            }
            glUniform1i_(ltex, pass ? 4 : 3);
            glUniformMatrix4fv_(lmvp, 1, GL_FALSE, mvp);
            glUniformMatrix4fv_(lmodel, 1, GL_FALSE, model);
            glUniform1f_(ltime, t);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, (void*)0);
        }
        /* the texture image, 1:1, into the lower left corner of the window */
        glBindFramebuffer_(GL_READ_FRAMEBUFFER, fbo);
        glBindFramebuffer_(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer_(0, 0, RT, RT, 8, 8, 8 + RT / 2, 8 + RT / 2, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer_(GL_FRAMEBUFFER, 0);
        SwapBuffers(dc);
        ex_fps(dc, "fbo33");
    }
    return 0;
}
