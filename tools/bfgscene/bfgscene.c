/* Replays a Doom 3 BFG frame baked by bake.py (the game's own geometry,
 * materials, textures, lights, shadow volumes and GLSL) through the system
 * opengl32.dll (fatgl's when it sits next to the executable). The scene file
 * is a GL command stream the way BFG's backend issues it; GL_State below is
 * the port of BFG's (gl_GraphicsAPIWrapper.cpp), so state changes match.
 *   bfgscene <scene.bfgs> [frames] [out.ppm] [--sections] [--prof] [--sample <file>] [--show]
 * Prints the best and median frame times and the image's FNV-1a hash.
 * --sections also times the depth fill, interactions and shader passes
 * (a glFinish between them). --prof prints fatgl's statistics and fatmap's
 * profiler zones; --sample writes instruction pointer samples of the timed
 * frames (tools/bfgscene/profile.py). --show opens the window and renders until it is
 * closed (fatgl's F6 / F7 / F8 / F10 keys work there). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
GLP(void, glBindAttribLocation, (GLuint, GLuint, const GLchar*))
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
GLP(void, glDisableVertexAttribArray, (GLuint))
GLP(void, glActiveTexture, (GLenum))
GLP(void, glCompressedTexImage2D, (GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*))
GLP(void, glGenVertexArrays, (GLsizei, GLuint*))
GLP(void, glBindVertexArray, (GLuint))
GLP(void, glStencilOpSeparate, (GLenum, GLenum, GLenum, GLenum))
GLP(void, glDrawElementsBaseVertex, (GLenum, GLsizei, GLenum, const void*, GLint))
GLP(void, glDepthBoundsEXT, (GLdouble, GLdouble))
#define LD(n) n##_ = (T_##n)(void*)wglGetProcAddress(#n)

#define GL_TEXTURE0_          0x84C0
#define GL_ARRAY_BUF          0x8892
#define GL_ELEM_BUF           0x8893
#define GL_STATIC_DRAW_       0x88E4
#define GL_TEXTURE_CUBE_MAP_  0x8513
#define GL_CUBE_POS_X         0x8515
#define GL_TEXTURE_MAX_LEVEL_ 0x813D
#define GL_TEXTURE_LOD_BIAS_  0x8501
#define GL_MAX_ANISOTROPY_    0x84FE
#define GL_SWIZZLE_R          0x8E42
#define GL_HALF_FLOAT_        0x140B
#define GL_INCR_WRAP_         0x8507
#define GL_DECR_WRAP_         0x8508
#define GL_UNPACK_SWAP_BYTES_ 0x0CF0
#define GL_TEXTURE_BORDER_    0x1004

enum { OP_STATE = 1, OP_CULL, OP_POLYOFS, OP_SCISSOR, OP_CLEAR, OP_PROGRAM, OP_VA, OP_FA, OP_TEX, OP_STENCILSEP, OP_DRAW,
       OP_COPYFB, OP_VIEWPORT, OP_MARK, OP_DBOUNDS };

typedef struct prog {
    GLuint id;
    GLint  va, fa;
} prog;

typedef struct tex {
    GLuint id;
    GLenum target;
} tex;

static uint8_t* g_file;
static size_t   g_size;
static size_t   g_at;
static int      g_w, g_h;
static tex*     g_tex;
static prog*    g_prog;
static uint32_t g_ntex, g_nprog;
static GLuint   g_vb, g_ib;
static uint8_t* g_cmds;
static uint32_t g_ncmds;

static uint32_t rd32(void)
{
    uint32_t v;
    memcpy(&v, g_file + g_at, 4);
    g_at += 4;
    return v;
}

static float rdf(void)
{
    float v;
    memcpy(&v, g_file + g_at, 4);
    g_at += 4;
    return v;
}

/* ------------------------------------------------------------- GL_State (BFG) */

#define GLS_SRCBLEND_BITS   7ull
#define GLS_DSTBLEND_BITS   (7ull << 3)
#define GLS_DEPTHMASK       (1ull << 6)
#define GLS_REDMASK         (1ull << 7)
#define GLS_GREENMASK       (1ull << 8)
#define GLS_BLUEMASK        (1ull << 9)
#define GLS_ALPHAMASK       (1ull << 10)
#define GLS_POLYMODE_LINE   (1ull << 11)
#define GLS_POLYGON_OFFSET  (1ull << 12)
#define GLS_DEPTHFUNC_BITS  (3ull << 13)
#define GLS_STENCIL_REF_SH  20
#define GLS_STENCIL_MASK_SH 28
#define GLS_STENCIL_FUNC_SH 36
#define GLS_STENCIL_FAIL_SH 39
#define GLS_STENCIL_ZFAIL_SH 42
#define GLS_STENCIL_PASS_SH 45
#define GLS_STENCIL_FUNC_BITS (7ull << 36)
#define GLS_STENCIL_OP_BITS (511ull << 39)

static uint64_t g_state;
static int      g_cull = -1;
static float    g_ofs_scale, g_ofs_bias;

static void gl_state(uint64_t bits, int force)
{
    uint64_t diff = force ? ~0ull : bits ^ g_state;
    if (!diff) return;
    if (diff & GLS_DEPTHFUNC_BITS) {
        static const GLenum f[4] = { GL_LEQUAL, GL_ALWAYS, GL_GEQUAL, GL_EQUAL };
        glDepthFunc(f[(bits >> 13) & 3]);
    }
    if (diff & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) {
        static const GLenum s[8] = { GL_ONE, GL_ZERO, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA };
        static const GLenum d[8] = { GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA };
        GLenum sf = s[bits & 7], df = d[(bits >> 3) & 7];
        if (sf == GL_ONE && df == GL_ZERO)
            glDisable(GL_BLEND);
        else {
            glEnable(GL_BLEND);
            glBlendFunc(sf, df);
        }
    }
    if (diff & GLS_DEPTHMASK) glDepthMask((bits & GLS_DEPTHMASK) ? GL_FALSE : GL_TRUE);
    if (diff & (GLS_REDMASK | GLS_GREENMASK | GLS_BLUEMASK | GLS_ALPHAMASK))
        glColorMask((bits & GLS_REDMASK) ? GL_FALSE : GL_TRUE, (bits & GLS_GREENMASK) ? GL_FALSE : GL_TRUE,
                    (bits & GLS_BLUEMASK) ? GL_FALSE : GL_TRUE, (bits & GLS_ALPHAMASK) ? GL_FALSE : GL_TRUE);
    if (diff & GLS_POLYMODE_LINE) glPolygonMode(GL_FRONT_AND_BACK, (bits & GLS_POLYMODE_LINE) ? GL_LINE : GL_FILL);
    if (diff & GLS_POLYGON_OFFSET) {
        if (bits & GLS_POLYGON_OFFSET) {
            glPolygonOffset(g_ofs_scale, g_ofs_bias);
            glEnable(GL_POLYGON_OFFSET_FILL);
            glEnable(GL_POLYGON_OFFSET_LINE);
        } else {
            glDisable(GL_POLYGON_OFFSET_FILL);
            glDisable(GL_POLYGON_OFFSET_LINE);
        }
    }
    if (diff & (GLS_STENCIL_FUNC_BITS | GLS_STENCIL_OP_BITS)) {
        if (bits & (GLS_STENCIL_FUNC_BITS | GLS_STENCIL_OP_BITS))
            glEnable(GL_STENCIL_TEST);
        else
            glDisable(GL_STENCIL_TEST);
    }
    if (diff & (GLS_STENCIL_FUNC_BITS | (0xFFull << GLS_STENCIL_REF_SH) | (0xFFull << GLS_STENCIL_MASK_SH))) {
        static const GLenum f[8] = { GL_ALWAYS, GL_LESS, GL_LEQUAL, GL_GREATER, GL_GEQUAL, GL_EQUAL, GL_NOTEQUAL, GL_NEVER };
        glStencilFunc(f[(bits >> GLS_STENCIL_FUNC_SH) & 7], (GLint)((bits >> GLS_STENCIL_REF_SH) & 0xFF), (GLuint)((bits >> GLS_STENCIL_MASK_SH) & 0xFF));
    }
    if (diff & GLS_STENCIL_OP_BITS) {
        static const GLenum o[8] = { GL_KEEP, GL_ZERO, GL_REPLACE, GL_INCR, GL_DECR, GL_INVERT, GL_INCR_WRAP_, GL_DECR_WRAP_ };
        glStencilOp(o[(bits >> GLS_STENCIL_FAIL_SH) & 7], o[(bits >> GLS_STENCIL_ZFAIL_SH) & 7], o[(bits >> GLS_STENCIL_PASS_SH) & 7]);
    }
    g_state = bits;
}

static void gl_cull(int ct)
{
    if (g_cull == ct) return;
    if (ct == 2)
        glDisable(GL_CULL_FACE);
    else {
        if (g_cull == 2 || g_cull < 0) glEnable(GL_CULL_FACE);
        glCullFace(ct == 1 ? GL_BACK : GL_FRONT); /* CT_FRONT_SIDED culls GL_FRONT (BFG's winding) */
    }
    g_cull = ct;
}

/* GL_SetDefaultState */
static void gl_default_state(void)
{
    glClearDepth(1.0);
    gl_state(0, 1);
    glCullFace(GL_FRONT_AND_BACK);
    glEnable(GL_CULL_FACE);
    g_cull = -1;
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBlendFunc(GL_ONE, GL_ZERO);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_POLYGON_OFFSET_LINE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, g_w, g_h);
    g_state = 0;
    gl_state(0, 1);
}

/* ------------------------------------------------------------- loading */

static GLuint shader(GLenum type, const char* src, const char* what)
{
    GLuint s = glCreateShader_(type);
    glShaderSource_(s, 1, &src, NULL);
    glCompileShader_(s);
    GLint ok = 0;
    glGetShaderiv_(s, 0x8B81, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog_(s, sizeof(log), NULL, log);
        printf("compile %s: %s\n", what, log);
        exit(1);
    }
    return s;
}

static void load_programs(void)
{
    g_prog = (prog*)calloc(g_nprog, sizeof(prog));
    for (uint32_t i = 0; i < g_nprog; i++) {
        char*    src[2];
        uint32_t len[2];
        for (int k = 0; k < 2; k++) {
            len[k] = rd32();
            src[k] = (char*)malloc(len[k] + 1);
            memcpy(src[k], g_file + g_at, len[k]);
            src[k][len[k]] = 0;
            g_at += len[k];
        }
        GLuint p = glCreateProgram_();
        glAttachShader_(p, shader(0x8B31, src[0], "vertex"));
        if (len[1]) glAttachShader_(p, shader(0x8B30, src[1], "fragment"));
        /* RenderProgs_GLSL.cpp binds the attributes by name */
        glBindAttribLocation_(p, 0, "in_Position");
        glBindAttribLocation_(p, 2, "in_Normal");
        glBindAttribLocation_(p, 3, "in_Color");
        glBindAttribLocation_(p, 4, "in_Color2");
        glBindAttribLocation_(p, 8, "in_TexCoord");
        glBindAttribLocation_(p, 9, "in_Tangent");
        glLinkProgram_(p);
        GLint ok = 0;
        glGetProgramiv_(p, 0x8B82, &ok);
        if (!ok) {
            char log[4096];
            glGetProgramInfoLog_(p, sizeof(log), NULL, log);
            printf("link: %s\n", log);
            exit(1);
        }
        glUseProgram_(p);
        g_prog[i].id = p;
        g_prog[i].va = glGetUniformLocation_(p, "_va_");
        g_prog[i].fa = glGetUniformLocation_(p, "_fa_");
        for (int s = 0; s < 16; s++) {
            char nm[8];
            snprintf(nm, sizeof(nm), "samp%d", s);
            GLint l = glGetUniformLocation_(p, nm);
            if (l >= 0) glUniform1i_(l, s);
        }
        free(src[0]), free(src[1]);
    }
    glUseProgram_(0);
}

static void load_textures(void)
{
    g_tex = (tex*)calloc(g_ntex, sizeof(tex));
    for (uint32_t i = 0; i < g_ntex; i++) {
        GLenum   target = rd32(), ifmt = rd32(), dfmt = rd32(), dtype = rd32();
        uint32_t compressed = rd32(), swap = rd32();
        uint32_t w = rd32(), h = rd32(), levels = rd32(), nimg = rd32();
        GLint    swz[4];
        for (int k = 0; k < 4; k++) swz[k] = (GLint)rd32();
        GLenum minf = rd32(), magf = rd32(), wrap = rd32();
        float  border[4];
        for (int k = 0; k < 4; k++) border[k] = rdf();
        float lodbias = rdf(), aniso = rdf();
        (void)w, (void)h;
        GLuint id;
        glGenTextures(1, &id);
        glBindTexture(target, id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (swap) glPixelStorei(GL_UNPACK_SWAP_BYTES_, GL_TRUE);
        for (uint32_t k = 0; k < nimg; k++) {
            uint32_t level = rd32(), z = rd32(), iw = rd32(), ih = rd32(), size = rd32();
            GLenum   t = target == GL_TEXTURE_CUBE_MAP_ ? GL_CUBE_POS_X + z : target;
            if (compressed)
                glCompressedTexImage2D_(t, (GLint)level, ifmt, (GLsizei)iw, (GLsizei)ih, 0, (GLsizei)size, g_file + g_at);
            else
                glTexImage2D(t, (GLint)level, (GLint)ifmt, (GLsizei)iw, (GLsizei)ih, 0, dfmt, dtype, g_file + g_at);
            g_at += size;
        }
        if (swap) glPixelStorei(GL_UNPACK_SWAP_BYTES_, GL_FALSE);
        glTexParameteri(target, GL_TEXTURE_MAX_LEVEL_, (GLint)levels - 1);
        for (int k = 0; k < 4; k++) glTexParameteri(target, GL_SWIZZLE_R + (GLenum)k, swz[k]);
        glTexParameterf(target, GL_TEXTURE_MIN_FILTER, (GLfloat)minf);
        glTexParameterf(target, GL_TEXTURE_MAG_FILTER, (GLfloat)magf);
        glTexParameterf(target, GL_MAX_ANISOTROPY_, aniso);
        glTexParameterf(target, GL_TEXTURE_LOD_BIAS_, lodbias);
        if (wrap == 0x812D) glTexParameterfv(target, GL_TEXTURE_BORDER_, border);
        glTexParameterf(target, GL_TEXTURE_WRAP_S, (GLfloat)wrap);
        glTexParameterf(target, GL_TEXTURE_WRAP_T, (GLfloat)wrap);
        g_tex[i].id = id, g_tex[i].target = target;
    }
}

/* ------------------------------------------------------------- the frame */

static int    g_layout = -1;
static double g_qpf;

static double now_ms(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1000.0 / g_qpf;
}

/* ---- --sample <file>: a sampling profiler. A thread stops every other
 * thread every quarter millisecond and records its instruction pointer; the
 * file lists module relative addresses (tools/bfgscene/profile.py names
 * them: fatgl's functions, JIT code, waits; " M": the main thread's) ---- */
#include <mmsystem.h>
#include <tlhelp32.h>
#define MAX_SAMPLES (1 << 22)
static uint64_t*     g_samples;
static uint8_t*      g_on_main; /* the sample is the main thread's (the GL calls) */
static volatile LONG g_nsamples, g_sampling;

static DWORD WINAPI sampler(void* arg)
{
    DWORD  self = GetCurrentThreadId(), main_id = (DWORD)(uintptr_t)arg;
    HANDLE th[256];
    int    nth = 0, is_main[256];
    double last = -1e9;
    timeBeginPeriod(1);
    while (g_sampling) {
        if (now_ms() - last > 100) { /* the thread list again (pools start lazily) */
            for (int i = 0; i < nth; i++) CloseHandle(th[i]);
            nth = 0;
            HANDLE        snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            THREADENTRY32 te;
            memset(&te, 0, sizeof(te));
            te.dwSize = sizeof(te);
            for (BOOL ok = Thread32First(snap, &te); ok && nth < 256; ok = Thread32Next(snap, &te))
                if (te.th32OwnerProcessID == GetCurrentProcessId() && te.th32ThreadID != self) {
                    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
                    if (h) is_main[nth] = te.th32ThreadID == main_id, th[nth++] = h;
                }
            CloseHandle(snap);
            last = now_ms();
        }
        for (int i = 0; i < nth; i++) {
            if (SuspendThread(th[i]) == (DWORD)-1) continue;
            CONTEXT cx;
            cx.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(th[i], &cx)) {
                LONG k = InterlockedIncrement(&g_nsamples) - 1;
#ifdef _WIN64
                if (k < MAX_SAMPLES) g_samples[k] = cx.Rip, g_on_main[k] = (uint8_t)is_main[i];
#else
                if (k < MAX_SAMPLES) g_samples[k] = cx.Eip, g_on_main[k] = (uint8_t)is_main[i];
#endif
            }
            ResumeThread(th[i]);
        }
        double t = now_ms(); /* about 4 kHz: Sleep(1) is too coarse */
        while (now_ms() - t < 0.25) SwitchToThread();
    }
    for (int i = 0; i < nth; i++) CloseHandle(th[i]);
    timeEndPeriod(1);
    return 0;
}

static void sample_write(const char* path)
{
    FILE* f = fopen(path, "w");
    if (!f) return;
    LONG n = g_nsamples < MAX_SAMPLES ? g_nsamples : MAX_SAMPLES;
    /* module of each address: the last lookup is cached (samples cluster) */
    HMODULE lm = NULL;
    char    lname[MAX_PATH] = "";
    for (LONG i = 0; i < n; i++) {
        uint64_t a = g_samples[i];
        HMODULE  m = NULL;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(uintptr_t)a, &m))
            m = NULL;
        if (m != lm) {
            lm = m;
            lname[0] = 0;
            if (m) GetModuleFileNameA(m, lname, sizeof(lname));
        }
        if (m)
            fprintf(f, "%s %llx%s\n", lname, (unsigned long long)(a - (uint64_t)(uintptr_t)m), g_on_main[i] ? " M" : "");
        else
            fprintf(f, "- %llx%s\n", (unsigned long long)a, g_on_main[i] ? " M" : "");
    }
    fclose(f);
    printf("%ld samples in %s\n", (long)n, path);
}

#define NSECT 8
static const char* g_sect_name[NSECT];
static double      g_sect_ms[NSECT];
static int         g_nsect;

static void layout(int l)
{
    if (l == g_layout) return;
    if (l == 0) {
        for (GLuint a = 0; a < 10; a++)
            if (a == 0 || a == 2 || a == 3 || a == 4 || a == 8 || a == 9) glEnableVertexAttribArray_(a);
        glVertexAttribPointer_(0, 3, GL_FLOAT, GL_FALSE, 32, (const void*)0);
        glVertexAttribPointer_(8, 2, GL_HALF_FLOAT_, GL_FALSE, 32, (const void*)12);
        glVertexAttribPointer_(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, (const void*)16);
        glVertexAttribPointer_(9, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, (const void*)20);
        glVertexAttribPointer_(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, (const void*)24);
        glVertexAttribPointer_(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, (const void*)28);
    } else {
        glEnableVertexAttribArray_(0);
        glDisableVertexAttribArray_(2), glDisableVertexAttribArray_(3), glDisableVertexAttribArray_(4);
        glDisableVertexAttribArray_(8), glDisableVertexAttribArray_(9);
        glVertexAttribPointer_(0, 4, GL_FLOAT, GL_FALSE, 16, (const void*)0);
    }
    g_layout = l;
}

static void frame(int sections)
{
    const uint8_t* p   = g_cmds;
    const uint8_t* end = g_cmds + g_ncmds;
    uint32_t       cur = 0;
    int            sect = -1;
    double         t0 = 0;
    glViewport(0, 0, g_w, g_h);
    gl_default_state();
    g_layout = -1;
    while (p < end) {
        uint32_t op;
        memcpy(&op, p, 4);
        p += 4;
        switch (op) {
        case OP_STATE: {
            uint64_t b;
            memcpy(&b, p, 8);
            p += 8;
            gl_state(b, 0);
        } break;
        case OP_CULL: {
            int32_t c;
            memcpy(&c, p, 4);
            p += 4;
            gl_cull(c);
        } break;
        case OP_POLYOFS: {
            memcpy(&g_ofs_scale, p, 4), memcpy(&g_ofs_bias, p + 4, 4);
            p += 8;
            if (g_state & GLS_POLYGON_OFFSET) glPolygonOffset(g_ofs_scale, g_ofs_bias);
        } break;
        case OP_DBOUNDS: { /* GL_DepthBoundsTest */
            float b[2];
            memcpy(b, p, 8);
            p += 8;
            if (!glDepthBoundsEXT_) break;
            if (b[0] == 0.0f && b[1] == 0.0f) {
                glDisable(0x8890 /* GL_DEPTH_BOUNDS_TEST_EXT */);
            } else {
                glEnable(0x8890);
                glDepthBoundsEXT_(b[0], b[1]);
            }
        } break;
        case OP_SCISSOR: {
            int32_t r[4];
            memcpy(r, p, 16);
            p += 16;
            glScissor(r[0], r[1], r[2], r[3]);
        } break;
        case OP_CLEAR: {
            uint32_t f, s;
            float    c[4];
            memcpy(&f, p, 4), memcpy(&s, p + 4, 4), memcpy(c, p + 8, 16);
            p += 24;
            GLbitfield m = 0;
            if (f & 1) glClearColor(c[0], c[1], c[2], c[3]), m |= GL_COLOR_BUFFER_BIT;
            if (f & 2) m |= GL_DEPTH_BUFFER_BIT;
            if (f & 4) glClearStencil((GLint)s), m |= GL_STENCIL_BUFFER_BIT;
            glClear(m);
        } break;
        case OP_PROGRAM: {
            memcpy(&cur, p, 4);
            p += 4;
            glUseProgram_(g_prog[cur].id);
        } break;
        case OP_VA:
        case OP_FA: {
            uint32_t n;
            memcpy(&n, p, 4);
            p += 4;
            GLint loc = op == OP_VA ? g_prog[cur].va : g_prog[cur].fa;
            if (loc >= 0) glUniform4fv_(loc, (GLsizei)n, (const GLfloat*)p);
            p += n * 16;
        } break;
        case OP_TEX: {
            uint32_t u;
            int32_t  t;
            memcpy(&u, p, 4), memcpy(&t, p + 4, 4);
            p += 8;
            glActiveTexture_(GL_TEXTURE0_ + u);
            if (t >= 0)
                glBindTexture(g_tex[t].target, g_tex[t].id);
            else
                glBindTexture(GL_TEXTURE_2D, 0);
        } break;
        case OP_STENCILSEP: {
            uint32_t v[4];
            memcpy(v, p, 16);
            p += 16;
            glStencilOpSeparate_(v[0], v[1], v[2], v[3]);
        } break;
        case OP_DRAW: {
            uint32_t v[4];
            memcpy(v, p, 16);
            p += 16;
            layout((int)v[0]);
            glDrawElementsBaseVertex_(GL_TRIANGLES, (GLsizei)v[3], GL_UNSIGNED_SHORT, (const void*)(uintptr_t)v[2], (GLint)(v[1] / (v[0] ? 16 : 32)));
        } break;
        case OP_MARK: {
            uint32_t n;
            memcpy(&n, p, 4);
            const char* name = (const char*)p + 4;
            p += 4 + n;
            if (sections) {
                if (strncmp(name, "light ", 6) == 0) break; /* lights fold into "interactions" */
                glFinish();
                double t = now_ms();
                if (sect >= 0) g_sect_ms[sect] += t - t0;
                t0 = t;
                for (sect = 0; sect < g_nsect; sect++)
                    if (strncmp(g_sect_name[sect], name, n) == 0 && g_sect_name[sect][n] == 0) break;
                if (sect == g_nsect && g_nsect < NSECT) {
                    char* s = (char*)malloc(n + 1);
                    memcpy(s, name, n), s[n] = 0;
                    g_sect_name[g_nsect++] = s;
                }
            }
        } break;
        default:
            printf("bad op %u\n", op);
            exit(1);
        }
    }
}

static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_CLOSE) PostQuitMessage(0);
    return DefWindowProcA(w, m, wp, lp);
}

static int cmpd(const void* a, const void* b)
{
    double x = *(const double*)a, y = *(const double*)b;
    return x < y ? -1 : x > y;
}

int main(int argc, char** argv)
{
    const char* path = NULL;
    const char* out = "bfgscene.ppm";
    const char* sample = NULL;
    int         frames = 10, sections = 0, show = 0, prof = 0, npos = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--sections"))
            sections = 1;
        else if (!strcmp(argv[i], "--prof"))
            prof = 1;
        else if (!strcmp(argv[i], "--sample") && i + 1 < argc)
            sample = argv[++i];
        else if (!strcmp(argv[i], "--show"))
            show = 1;
        else if (npos == 0)
            path = argv[i], npos++;
        else if (npos == 1)
            frames = atoi(argv[i]), npos++;
        else
            out = argv[i], npos++;
    }
    if (!path) {
        printf("bfgscene <scene.bfgs> [frames] [out.ppm] [--sections] [--prof] [--sample <file>] [--show]\n");
        return 1;
    }
    FILE* f = fopen(path, "rb");
    if (!f) {
        printf("%s: not found\n", path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    g_size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    g_file = (uint8_t*)malloc(g_size);
    if (fread(g_file, 1, g_size, f) != g_size) return 1;
    fclose(f);
    if (memcmp(g_file, "BFGS", 4) != 0) {
        printf("%s: not a scene\n", path);
        return 1;
    }
    g_at = 4;
    uint32_t version = rd32();
    if (version != 1 && version != 2) { /* 2: depth bounds */
        printf("%s: unknown version\n", path);
        return 1;
    }
    g_w = (int)rd32(), g_h = (int)rd32();
    g_ntex = rd32(), g_nprog = rd32(), g_ncmds = rd32();

    LARGE_INTEGER fq;
    QueryPerformanceFrequency(&fq);
    g_qpf = (double)fq.QuadPart;
    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = proc, wc.hInstance = GetModuleHandleA(NULL), wc.lpszClassName = "fatgl_bfgscene";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);
    RECT r = { 0, 0, g_w, g_h };
    DWORD style = show ? WS_OVERLAPPEDWINDOW : WS_POPUP;
    AdjustWindowRect(&r, style, FALSE);
    HWND win = CreateWindowA("fatgl_bfgscene", path, style, 40, 40, r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    HDC  dc  = GetDC(win);
    PIXELFORMATDESCRIPTOR pfd = { 0 };
    pfd.nSize = sizeof(pfd), pfd.nVersion = 1, pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.cColorBits = 32, pfd.cDepthBits = 24, pfd.cStencilBits = 8;
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
    HGLRC rc = wglCreateContext(dc);
    wglMakeCurrent(dc, rc);
    printf("renderer: %s\n", (const char*)glGetString(GL_RENDERER));
    LD(glCreateShader), LD(glShaderSource), LD(glCompileShader), LD(glGetShaderiv), LD(glGetShaderInfoLog), LD(glCreateProgram);
    LD(glAttachShader), LD(glBindAttribLocation), LD(glLinkProgram), LD(glGetProgramiv), LD(glGetProgramInfoLog), LD(glUseProgram);
    LD(glGetUniformLocation), LD(glUniform4fv), LD(glUniform1i), LD(glGenBuffers), LD(glBindBuffer), LD(glBufferData);
    LD(glVertexAttribPointer), LD(glEnableVertexAttribArray), LD(glDisableVertexAttribArray), LD(glActiveTexture);
    LD(glCompressedTexImage2D), LD(glGenVertexArrays), LD(glBindVertexArray), LD(glStencilOpSeparate), LD(glDrawElementsBaseVertex);
    LD(glDepthBoundsEXT); /* GL_EXT_depth_bounds_test: NULL without it (BFG then skips the test) */

    double tl = now_ms();
    load_textures();
    load_programs();
    uint32_t nvb = rd32(), nib = rd32();
    GLuint   vao;
    glGenVertexArrays_(1, &vao);
    glBindVertexArray_(vao);
    glGenBuffers_(1, &g_vb), glGenBuffers_(1, &g_ib);
    glBindBuffer_(GL_ARRAY_BUF, g_vb);
    glBufferData_(GL_ARRAY_BUF, (GLsizeiptr)nvb, g_file + g_at, GL_STATIC_DRAW_);
    g_at += nvb;
    glBindBuffer_(GL_ELEM_BUF, g_ib);
    glBufferData_(GL_ELEM_BUF, (GLsizeiptr)nib, g_file + g_at, GL_STATIC_DRAW_);
    g_at += nib;
    g_cmds = g_file + g_at;
    if (g_at + g_ncmds != g_size) {
        printf("%s: truncated\n", path);
        return 1;
    }
    printf("loaded %u textures, %u programs, %.1f MB of geometry in %.0f ms\n", g_ntex, g_nprog, (nvb + nib) / 1e6, now_ms() - tl);

    if (show) {
        ShowWindow(win, SW_SHOW);
        double last = now_ms(), acc = 0;
        int    n = 0;
        for (;;) {
            MSG msg;
            while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) return 0;
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
            double t = now_ms();
            frame(0);
            glFinish();
            acc += now_ms() - t;
            SwapBuffers(dc);
            if (++n == 20 || now_ms() - last > 1000) {
                char title[256];
                snprintf(title, sizeof(title), "%s  %.2f ms", path, acc / n);
                SetWindowTextA(win, title);
                acc = 0, n = 0, last = now_ms();
            }
        }
    }

    /* --prof: fatgl's statistics and fatmap's profiler zones over the timed frames */
    typedef int(WINAPI * report_fn)(char*, int, int);
    report_fn report = prof ? (report_fn)(void*)wglGetProcAddress("fglReportFATGL") : NULL;
    if (prof && !report) printf("--prof: not fatgl's opengl32.dll\n");
    HANDLE  hs = NULL;
    double* ms = (double*)malloc(sizeof(double) * (size_t)(frames > 0 ? frames : 1));
    double* issue = (double*)malloc(sizeof(double) * (size_t)(frames > 0 ? frames : 1)); /* in the GL calls (fatgl: recording) */
    for (int i = 0; i < frames + 1; i++) { /* frame 0 warms up (JIT compiles, texture caches) */
        double t = now_ms();
        frame(0);
        double ti = now_ms();
        glFinish();
        if (i > 0) ms[i - 1] = now_ms() - t, issue[i - 1] = ti - t;
        if (i == 0 && report) report(NULL, 0, 0);
        if (i == 0 && sample) {
            g_samples  = (uint64_t*)malloc(sizeof(uint64_t) * MAX_SAMPLES);
            g_on_main  = (uint8_t*)malloc(MAX_SAMPLES);
            g_sampling = 1;
            hs         = CreateThread(NULL, 0, sampler, (void*)(uintptr_t)GetCurrentThreadId(), 0, NULL);
            SetThreadPriority(hs, THREAD_PRIORITY_TIME_CRITICAL);
        }
    }
    if (hs) {
        g_sampling = 0;
        WaitForSingleObject(hs, INFINITE);
        sample_write(sample);
    }
    static char rep[65536];
    if (report) report(rep, sizeof(rep), frames);
    if (sections) {
        frame(1);
        glFinish();
    }
    qsort(ms, (size_t)frames, sizeof(double), cmpd);
    qsort(issue, (size_t)frames, sizeof(double), cmpd);
    static uint8_t img[4096 * 4096 * 4];
    glReadPixels(0, 0, g_w, g_h, GL_RGBA, GL_UNSIGNED_BYTE, img);
    uint64_t h = 1469598103934665603ull;
    double   sum[3] = { 0, 0, 0 };
    for (int y = 0; y < g_h; y++)
        for (int x = 0; x < g_w; x++)
            for (int k = 0; k < 3; k++) {
                uint8_t c = img[((size_t)y * (size_t)g_w + (size_t)x) * 4 + (size_t)k];
                h = (h ^ c) * 1099511628211ull;
                sum[k] += c;
            }
    printf("bfgscene %dx%d: %.2f ms best, %.2f ms median of %d\n", g_w, g_h, ms[0], ms[frames / 2], frames);
    printf("issue %.2f ms median (the GL calls; the rest is glFinish)\n", issue[frames / 2]);
    if (sections)
        for (int i = 0; i < g_nsect; i++)
            if (strcmp(g_sect_name[i], "end")) printf("  %-16s %7.2f ms\n", g_sect_name[i], g_sect_ms[i]);
    printf("mean color %.2f %.2f %.2f\n", sum[0] / (g_w * g_h), sum[1] / (g_w * g_h), sum[2] / (g_w * g_h));
    printf("hash %016llx\n", (unsigned long long)h);
    for (char* l = rep; report && *l;) { /* "prof " lines */
        char* e = strchr(l, '\n');
        if (e) *e = 0;
        printf("prof %s\n", l);
        if (!e) break;
        l = e + 1;
    }
    FILE* o = fopen(out, "wb");
    if (o) {
        fprintf(o, "P6 %d %d 255\n", g_w, g_h);
        for (int y = g_h - 1; y >= 0; y--)
            for (int x = 0; x < g_w; x++) fwrite(img + ((size_t)y * (size_t)g_w + (size_t)x) * 4, 1, 3, o);
        fclose(o);
    }
    return 0;
}
