/* fatgl example: OpenGL 2.0 era code, the way older games are written:
 * immediate mode geometry, glLightfv / glMaterialfv, a texture, and a GLSL
 * 1.10 program that reads the fixed function state (gl_Vertex, gl_Normal,
 * gl_MultiTexCoord0, gl_NormalMatrix, gl_LightSource[0],
 * gl_FrontMaterial, gl_TexCoord[0], texture2D, ftransform, gl_FragColor).
 * The left torus uses the shader (per pixel lighting), the right one the
 * fixed function pipeline (per vertex lighting) with the same state. */
#include "win.h"
#include <math.h>

typedef char GLchar;
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER   0x8B31
#define GL_COMPILE_STATUS  0x8B81
#define GL_LINK_STATUS     0x8B82

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
GLFN(void, glUniform1i, (GLint, GLint))

static void* load(const char* n)
{
    void* p = (void*)wglGetProcAddress(n);
    if (!p) ex_fail("missing entry point", n);
    return p;
}
#define LOAD(name) name##_ = (PFN_##name)load(#name)

/* no #version: GLSL 1.10 */
static const char* vs_src = "varying vec3 N;\n"
                            "varying vec3 P;\n"
                            "void main() {\n"
                            "    N = gl_NormalMatrix * gl_Normal;\n"
                            "    P = vec3(gl_ModelViewMatrix * gl_Vertex);\n"
                            "    gl_TexCoord[0] = gl_MultiTexCoord0;\n"
                            "    gl_Position = ftransform();\n"
                            "}\n";
static const char* fs_src = "uniform sampler2D tex;\n"
                            "varying vec3 N;\n"
                            "varying vec3 P;\n"
                            "void main() {\n"
                            "    vec3 n = normalize(N);\n"
                            "    vec3 l = normalize(gl_LightSource[0].position.xyz - P);\n"
                            "    vec3 h = normalize(l - normalize(P));\n"
                            "    float d = max(dot(n, l), 0.0);\n"
                            "    float s = pow(max(dot(n, h), 0.0), gl_FrontMaterial.shininess);\n"
                            "    vec4 t = texture2D(tex, gl_TexCoord[0].st);\n"
                            "    vec3 c = t.rgb * (gl_LightModel.ambient.rgb * gl_FrontMaterial.ambient.rgb\n"
                            "                    + gl_LightSource[0].diffuse.rgb * gl_FrontMaterial.diffuse.rgb * d)\n"
                            "           + gl_LightSource[0].specular.rgb * gl_FrontMaterial.specular.rgb * s;\n"
                            "    gl_FragColor = vec4(c, 1.0);\n"
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

static void torus(float R, float r, int nu, int nv)
{
    for (int i = 0; i < nu; i++) {
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= nv; j++) {
            for (int k = 0; k <= 1; k++) {
                float u = (float)(i + k) / (float)nu * 6.2831853f, v = (float)j / (float)nv * 6.2831853f;
                float cx = cosf(u), sx = sinf(u), cv = cosf(v), sv = sinf(v);
                glNormal3f(cx * cv, sx * cv, sv);
                glTexCoord2f((float)(i + k) / (float)nu * 4.0f, (float)j / (float)nv);
                glVertex3f((R + r * cv) * cx, (R + r * cv) * sx, r * sv);
            }
        }
        glEnd();
    }
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)inst, (void)prev, (void)cmd, (void)show;
    HDC dc = ex_open("fatgl legacy GLSL (OpenGL 2.0 style)");
    if (!dc) return 1;
    LOAD(glCreateShader), LOAD(glShaderSource), LOAD(glCompileShader), LOAD(glGetShaderiv), LOAD(glGetShaderInfoLog);
    LOAD(glCreateProgram), LOAD(glAttachShader), LOAD(glLinkProgram), LOAD(glGetProgramiv), LOAD(glGetProgramInfoLog);
    LOAD(glUseProgram), LOAD(glGetUniformLocation), LOAD(glUniform1i);
    GLuint prog = glCreateProgram_();
    glAttachShader_(prog, shader(GL_VERTEX_SHADER, vs_src));
    glAttachShader_(prog, shader(GL_FRAGMENT_SHADER, fs_src));
    glLinkProgram_(prog);
    GLint ok = 0;
    glGetProgramiv_(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog_(prog, sizeof(log), NULL, log);
        ex_fail("link", log);
    }

    static unsigned char img[64 * 64 * 4];
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            unsigned char* p = img + (y * 64 + x) * 4;
            int            ch = ((x >> 3) ^ (y >> 3)) & 1;
            p[0] = ch ? 250 : 90, p[1] = ch ? 220 : 140, p[2] = ch ? 120 : 230, p[3] = 255;
        }
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    GLfloat lpos[4] = { 2, 3, 4, 1 }, lamb[4] = { 0.25f, 0.25f, 0.3f, 1 }, white[4] = { 1, 1, 1, 1 };
    GLfloat mamb[4] = { 1, 1, 1, 1 }, mspec[4] = { 0.6f, 0.6f, 0.6f, 1 };
    while (ex_poll()) {
        float t = (float)ex_time(), a = (float)g_w / (float)(g_h > 0 ? g_h : 1);
        glViewport(0, 0, g_w, g_h);
        glClearColor(0.06f, 0.07f, 0.1f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glFrustum(-0.1 * a, 0.1 * a, -0.1, 0.1, 0.2, 50);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glLightfv(GL_LIGHT0, GL_POSITION, lpos); /* eye space */
        glLightfv(GL_LIGHT0, GL_DIFFUSE, white);
        glLightfv(GL_LIGHT0, GL_SPECULAR, white);
        GLfloat lm[4] = { lamb[0], lamb[1], lamb[2], lamb[3] };
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, lm);
        glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mamb);
        glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, white);
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mspec);
        glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 40);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glEnable(GL_TEXTURE_2D);
        for (int side = 0; side < 2; side++) {
            glPushMatrix();
            glTranslatef(side ? 1.6f : -1.6f, 0, -6);
            glRotatef(t * 30 + (float)side * 20, 1, 0.3f, 0);
            glRotatef(t * 45, 0, 0, 1);
            if (side == 0) {
                glUseProgram_(prog);
                glUniform1i_(glGetUniformLocation_(prog, "tex"), 0);
            } else {
                glUseProgram_(0);
                glEnable(GL_LIGHTING);
                glEnable(GL_LIGHT0);
            }
            torus(1.0f, 0.4f, 48, 24);
            glDisable(GL_LIGHTING);
            glPopMatrix();
        }
        glUseProgram_(0);
        SwapBuffers(dc);
        ex_fps(dc, "legacy");
    }
    return 0;
}
