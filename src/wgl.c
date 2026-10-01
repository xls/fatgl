/*
 * fatgl - WGL: pixel formats, contexts, presentation (opengl32.dll exports).
 *
 * gdi32's ChoosePixelFormat / SetPixelFormat / DescribePixelFormat /
 * SwapBuffers call these when this opengl32.dll is the one loaded (put it
 * next to the application). Rendering goes to fatmap surfaces; SwapBuffers
 * copies the back buffer to the window with SetDIBitsToDevice.
 */
#include "fgl.h"
#include <stdarg.h>
#include <stdio.h>

/* ---- the log: fatgl.log next to the executable (FATGL_LOG=0: off,
 * FATGL_LOG=<file>: elsewhere); the DLL that was loaded, contexts, calls
 * that are not implemented, shader errors ---- */
static FILE*            g_log;
static int              g_log_state; /* 0 not opened yet, 1 open, -1 off */
static CRITICAL_SECTION g_log_lock;

static void fgl_log_open(void)
{
    const char* e = getenv("FATGL_LOG");
    char        path[MAX_PATH], exe[MAX_PATH] = "", dll[MAX_PATH] = "";
    g_log_state = -1;
    if (e && !strcmp(e, "0")) return;
    GetModuleFileNameA(NULL, exe, sizeof(exe));
    HMODULE self = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(void*)&fgl_log_open,
                       &self);
    GetModuleFileNameA(self, dll, sizeof(dll));
    if (e && *e && strcmp(e, "1")) {
        snprintf(path, sizeof(path), "%s", e);
    } else {
        snprintf(path, sizeof(path), "%s", exe);
        char* slash = strrchr(path, 92 /* backslash */);
        snprintf(slash ? slash + 1 : path, sizeof(path) - (size_t)(slash ? slash + 1 - path : 0), "fatgl.log");
    }
    g_log = fopen(path, "w");
    if (!g_log) return;
    InitializeCriticalSection(&g_log_lock);
    g_log_state = 1;
    fprintf(g_log, "fatgl " FGL_VERSION " on fatmap %s (%s, %d bit)\n", fm_version_string(), fm_simd_name(fm_simd_best()), (int)sizeof(void*) * 8);
    fprintf(g_log, "dll: %s\nexe: %s\n", dll, exe);
    fflush(g_log);
}

void fgl_log(const char* fmt, ...)
{
    if (g_log_state == 0) fgl_log_open();
    if (g_log_state != 1) return;
    EnterCriticalSection(&g_log_lock);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
    LeaveCriticalSection(&g_log_lock);
}

/* ---- pixel formats ---- */
typedef struct fgl_pf {
    int double_buffer, depth, stencil, samples;
} fgl_pf;
static const fgl_pf g_pf[] = {
    { 1, 24, 8, 1 }, /* 1: RGBA8, double buffered, depth 24, stencil 8 */
    { 1, 0, 0, 1 },  /* 2: RGBA8, double buffered, no depth */
    { 0, 24, 8, 1 }, /* 3: RGBA8, single buffered, depth 24, stencil 8 */
    { 1, 24, 8, 4 }, /* 4: as 1, 4x MSAA (fatmap's sample patterns) */
    { 1, 24, 8, 8 }, /* 5: as 1, 8x MSAA */
};
#define FGL_NPF ((int)(sizeof(g_pf) / sizeof(g_pf[0])))

int WINAPI wglDescribePixelFormat(HDC hdc, int fmt, UINT bytes, PIXELFORMATDESCRIPTOR* pfd)
{
    (void)hdc;
    if (!pfd) return FGL_NPF;
    if (fmt < 1 || fmt > FGL_NPF || bytes < sizeof(PIXELFORMATDESCRIPTOR)) return 0;
    const fgl_pf* f = &g_pf[fmt - 1];
    memset(pfd, 0, sizeof(*pfd));
    pfd->nSize        = sizeof(*pfd);
    pfd->nVersion     = 1;
    pfd->dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_SUPPORT_GDI * 0 | (f->double_buffer ? PFD_DOUBLEBUFFER : 0);
    pfd->iPixelType   = PFD_TYPE_RGBA;
    pfd->cColorBits   = 32;
    pfd->cRedBits     = 8, pfd->cRedShift = 16;
    pfd->cGreenBits   = 8, pfd->cGreenShift = 8;
    pfd->cBlueBits    = 8, pfd->cBlueShift = 0;
    pfd->cAlphaBits   = 8, pfd->cAlphaShift = 24;
    pfd->cDepthBits   = (BYTE)f->depth;
    pfd->cStencilBits = (BYTE)f->stencil;
    pfd->iLayerType   = PFD_MAIN_PLANE;
    return FGL_NPF;
}

int WINAPI wglChoosePixelFormat(HDC hdc, const PIXELFORMATDESCRIPTOR* pfd)
{
    (void)hdc;
    if (!pfd) return 0;
    if (!(pfd->dwFlags & PFD_DOUBLEBUFFER) && !(pfd->dwFlags & PFD_DOUBLEBUFFER_DONTCARE)) return 3;
    return pfd->cDepthBits == 0 && pfd->cStencilBits == 0 ? 2 : 1;
}

static const char* g_pf_prop = "fatgl.pixelformat";

BOOL WINAPI wglSetPixelFormat(HDC hdc, int fmt, const PIXELFORMATDESCRIPTOR* pfd)
{
    (void)pfd;
    HWND w = WindowFromDC(hdc);
    if (fmt < 1 || fmt > FGL_NPF || !w) return FALSE;
    if (GetPropA(w, g_pf_prop)) return (int)(INT_PTR)GetPropA(w, g_pf_prop) == fmt; /* once per window */
    return SetPropA(w, g_pf_prop, (HANDLE)(INT_PTR)fmt);
}

int WINAPI wglGetPixelFormat(HDC hdc)
{
    HWND w = WindowFromDC(hdc);
    return w ? (int)(INT_PTR)GetPropA(w, g_pf_prop) : 0;
}

/* ---- contexts ---- */
static _Thread_local fgl_ctx* t_cur;
static _Thread_local HDC      t_dc;

fgl_ctx* fgl_cur(void) { return t_cur; }

static void fgl_client_size(HDC hdc, int* w, int* h)
{
    RECT r = { 0, 0, 1, 1 };
    HWND wnd = WindowFromDC(hdc);
    if (wnd) GetClientRect(wnd, &r);
    *w = r.right - r.left > 0 ? r.right - r.left : 1;
    *h = r.bottom - r.top > 0 ? r.bottom - r.top : 1;
}

static HGLRC fgl_create(HDC hdc, int major, int minor, int core)
{
    fgl_ctx* c = (fgl_ctx*)calloc(1, sizeof(fgl_ctx));
    if (!c) return NULL;
    c->hdc   = hdc;
    c->major = major, c->minor = minor, c->core = core;
    fgl_ctx_init(c);
    int w, h;
    fgl_client_size(hdc, &w, &h);
    fgl_log("context %p: requested %d.%d %s, window %d x %d%s\n", (void*)c, major, minor, core ? "core" : "compatibility", w, h,
            c->c3 ? "" : " FAILED");
    if (!c->c3) {
        free(c);
        return NULL;
    }
    return (HGLRC)c;
}

HGLRC WINAPI wglCreateContext(HDC hdc) { return fgl_create(hdc, 0, 0, 0); }
HGLRC WINAPI wglCreateLayerContext(HDC hdc, int layer) { return layer == 0 ? wglCreateContext(hdc) : NULL; }

BOOL WINAPI wglDeleteContext(HGLRC rc)
{
    fgl_ctx* c = (fgl_ctx*)rc;
    if (!c) return FALSE;
    if (t_cur == c) t_cur = NULL, t_dc = NULL;
    fgl_ctx_free(c);
    free(c);
    return TRUE;
}

BOOL WINAPI wglMakeCurrent(HDC hdc, HGLRC rc)
{
    fgl_ctx* c = (fgl_ctx*)rc;
    if (t_cur && t_cur != c) fgl_flush(t_cur);
    t_cur = c, t_dc = c ? hdc : NULL;
    if (!c) return TRUE;
    c->hdc = hdc;
    int fmt = wglGetPixelFormat(hdc), ms = fmt >= 1 && fmt <= FGL_NPF ? g_pf[fmt - 1].samples : 1;
    if (ms != c->samples) c->samples = ms, c->tgt_color = NULL; /* the window's MSAA: rebound on the next draw */
    int w, h;
    fgl_client_size(hdc, &w, &h);
    fgl_resize(c, w, h);
    if (!c->made_current) { /* the first time: the viewport and scissor box cover the window */
        c->made_current = 1;
        c->viewport[0] = c->viewport[1] = 0, c->viewport[2] = w, c->viewport[3] = h;
        c->scissor[0] = c->scissor[1] = 0, c->scissor[2] = w, c->scissor[3] = h;
    }
    return TRUE;
}

BOOL WINAPI wglMakeContextCurrentARB(HDC draw, HDC read, HGLRC rc)
{
    (void)read;
    return wglMakeCurrent(draw, rc);
}

HGLRC WINAPI wglGetCurrentContext(void) { return (HGLRC)t_cur; }
HDC WINAPI   wglGetCurrentDC(void) { return t_dc; }
HDC WINAPI   wglGetCurrentReadDCARB(void) { return t_dc; }

BOOL WINAPI wglShareLists(HGLRC a, HGLRC b)
{
    (void)a, (void)b;
    fgl_unimplemented("wglShareLists");
    return TRUE; /* separate objects; enough for single context apps */
}

BOOL WINAPI wglCopyContext(HGLRC src, HGLRC dst, UINT mask)
{
    (void)src, (void)dst, (void)mask;
    return FALSE;
}

/* ---- presentation ---- */
BOOL WINAPI wglSwapBuffers(HDC hdc)
{
    fgl_ctx* c = t_cur;
    if (!c) return FALSE;
    fgl_flush(c);
    fgl_overlay_frame(c);
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = c->color->stride / 4; /* rows padded by fatmap */
    bi.bmiHeader.biHeight      = c->fbh;               /* bottom up rows, as GL draws them */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(hdc, 0, 0, (DWORD)c->fbw, (DWORD)c->fbh, 0, 0, 0, (UINT)c->fbh, c->color->data, &bi, DIB_RGB_COLORS);
    int w, h; /* follow the window size */
    fgl_client_size(hdc, &w, &h);
    if (w != c->fbw || h != c->fbh) fgl_resize(c, w, h);
    return TRUE;
}

BOOL WINAPI wglSwapLayerBuffers(HDC hdc, UINT planes)
{
    return (planes & WGL_SWAP_MAIN_PLANE) ? wglSwapBuffers(hdc) : FALSE;
}

DWORD WINAPI wglSwapMultipleBuffers(UINT n, const WGLSWAP* s)
{
    for (UINT i = 0; i < n; i++) wglSwapBuffers(s[i].hdc);
    return n;
}

static int g_swap_interval = 1;
BOOL WINAPI wglSwapIntervalEXT(int interval)
{
    g_swap_interval = interval; /* no vsync wait: SetDIBitsToDevice presents immediately */
    return TRUE;
}
int WINAPI wglGetSwapIntervalEXT(void) { return g_swap_interval; }

/* ---- entry points ---- */
static PROC fgl_find(const char* name)
{
    int lo = 0, hi = fgl_nprocs - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2, d = strcmp(name, fgl_procs[m].name);
        if (!d) return fgl_procs[m].proc;
        if (d < 0) hi = m - 1;
        else lo = m + 1;
    }
    return NULL;
}

PROC WINAPI wglGetProcAddress(LPCSTR name) { return name ? fgl_find(name) : NULL; }
PROC WINAPI wglGetDefaultProcAddress(LPCSTR name) { return wglGetProcAddress(name); }

static const char g_wgl_ext[] = "WGL_ARB_extensions_string WGL_EXT_extensions_string WGL_ARB_create_context "
                                "WGL_ARB_create_context_profile WGL_ARB_pixel_format WGL_ARB_make_current_read "
                                "WGL_EXT_swap_control";
const char* WINAPI wglGetExtensionsStringARB(HDC hdc)
{
    (void)hdc;
    return g_wgl_ext;
}
const char* WINAPI wglGetExtensionsStringEXT(void) { return g_wgl_ext; }

#define WGL_CONTEXT_MAJOR_VERSION_ARB    0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB    0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB     0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x0001

HGLRC WINAPI wglCreateContextAttribsARB(HDC hdc, HGLRC share, const int* a)
{
    (void)share;
    int major = 1, minor = 0, profile = 0;
    for (; a && a[0]; a += 2) {
        if (a[0] == WGL_CONTEXT_MAJOR_VERSION_ARB) major = a[1];
        if (a[0] == WGL_CONTEXT_MINOR_VERSION_ARB) minor = a[1];
        if (a[0] == WGL_CONTEXT_PROFILE_MASK_ARB) profile = a[1];
    }
    if (major > 3 || (major == 3 && minor > 3)) { /* ERROR_INVALID_VERSION_ARB */
        SetLastError(0xC0072095);
        return NULL;
    }
    return fgl_create(hdc, major, minor, (profile & WGL_CONTEXT_CORE_PROFILE_BIT_ARB) && major >= 3);
}

/* WGL_ARB_pixel_format */
#define WGL_NUMBER_PIXEL_FORMATS_ARB 0x2000
#define WGL_DRAW_TO_WINDOW_ARB       0x2001
#define WGL_DRAW_TO_BITMAP_ARB       0x2002
#define WGL_ACCELERATION_ARB         0x2003
#define WGL_SUPPORT_GDI_ARB          0x200F
#define WGL_SUPPORT_OPENGL_ARB       0x2010
#define WGL_DOUBLE_BUFFER_ARB        0x2011
#define WGL_STEREO_ARB               0x2012
#define WGL_PIXEL_TYPE_ARB           0x2013
#define WGL_COLOR_BITS_ARB           0x2014
#define WGL_RED_BITS_ARB             0x2015
#define WGL_GREEN_BITS_ARB           0x2017
#define WGL_BLUE_BITS_ARB            0x2019
#define WGL_ALPHA_BITS_ARB           0x201B
#define WGL_DEPTH_BITS_ARB           0x2022
#define WGL_STENCIL_BITS_ARB         0x2023
#define WGL_FULL_ACCELERATION_ARB    0x2027
#define WGL_TYPE_RGBA_ARB            0x202B
#define WGL_SAMPLE_BUFFERS_ARB       0x2041
#define WGL_SAMPLES_ARB              0x2042

static int fgl_pf_attrib(int fmt, int a)
{
    const fgl_pf* f = &g_pf[fmt - 1];
    switch (a) {
    case WGL_NUMBER_PIXEL_FORMATS_ARB: return FGL_NPF;
    case WGL_DRAW_TO_WINDOW_ARB: case WGL_SUPPORT_OPENGL_ARB: return 1;
    case WGL_ACCELERATION_ARB: return WGL_FULL_ACCELERATION_ARB;
    case WGL_DOUBLE_BUFFER_ARB: return f->double_buffer;
    case WGL_PIXEL_TYPE_ARB: return WGL_TYPE_RGBA_ARB;
    case WGL_COLOR_BITS_ARB: return 32;
    case WGL_RED_BITS_ARB: case WGL_GREEN_BITS_ARB: case WGL_BLUE_BITS_ARB: case WGL_ALPHA_BITS_ARB: return 8;
    case WGL_DEPTH_BITS_ARB: return f->depth;
    case WGL_STENCIL_BITS_ARB: return f->stencil;
    case WGL_SAMPLE_BUFFERS_ARB: return f->samples > 1;
    case WGL_SAMPLES_ARB: return f->samples > 1 ? f->samples : 0;
    default: return 0; /* no bitmaps, stereo, sRGB */
    }
}

BOOL WINAPI wglGetPixelFormatAttribivARB(HDC hdc, int fmt, int layer, UINT n, const int* attr, int* values)
{
    (void)hdc, (void)layer;
    if ((fmt < 1 || fmt > FGL_NPF) && !(n == 1 && attr[0] == WGL_NUMBER_PIXEL_FORMATS_ARB)) return FALSE;
    for (UINT i = 0; i < n; i++) values[i] = attr[i] == WGL_NUMBER_PIXEL_FORMATS_ARB ? FGL_NPF : fgl_pf_attrib(fmt, attr[i]);
    return TRUE;
}

BOOL WINAPI wglGetPixelFormatAttribfvARB(HDC hdc, int fmt, int layer, UINT n, const int* attr, FLOAT* values)
{
    int iv[64];
    if (n > 64 || !wglGetPixelFormatAttribivARB(hdc, fmt, layer, n, attr, iv)) return FALSE;
    for (UINT i = 0; i < n; i++) values[i] = (FLOAT)iv[i];
    return TRUE;
}

BOOL WINAPI wglChoosePixelFormatARB(HDC hdc, const int* ia, const FLOAT* fa, UINT max, int* formats, UINT* n)
{
    (void)hdc, (void)fa;
    int want_depth = -1, want_double = -1, ok = 1, buffers = 0, samples = 0;
    for (; ia && ia[0]; ia += 2) {
        if (ia[0] == WGL_DEPTH_BITS_ARB) want_depth = ia[1];
        if (ia[0] == WGL_DOUBLE_BUFFER_ARB) want_double = ia[1];
        if (ia[0] == WGL_DRAW_TO_BITMAP_ARB && ia[1]) ok = 0;
        if (ia[0] == WGL_STEREO_ARB && ia[1]) ok = 0;
        if (ia[0] == WGL_SAMPLE_BUFFERS_ARB) buffers = ia[1];
        if (ia[0] == WGL_SAMPLES_ARB) samples = ia[1];
    }
    *n = 0;
    if (!ok || max == 0) return TRUE;
    if (buffers && samples > 1) /* MSAA: 4x for up to 4 samples, 8x above (16x asks get 8x, best effort) */
        formats[0] = samples <= 4 ? 4 : 5;
    else
        formats[0] = want_double == 0 ? 3 : (want_depth == 0 ? 2 : 1);
    *n = 1;
    return TRUE;
}

/* ---- layers, fonts: not supported ---- */
BOOL WINAPI wglDescribeLayerPlane(HDC hdc, int fmt, int layer, UINT n, LPLAYERPLANEDESCRIPTOR d)
{
    (void)hdc, (void)fmt, (void)layer, (void)n, (void)d;
    return FALSE;
}
int WINAPI wglSetLayerPaletteEntries(HDC hdc, int layer, int start, int n, const COLORREF* c)
{
    (void)hdc, (void)layer, (void)start, (void)n, (void)c;
    return 0;
}
int WINAPI wglGetLayerPaletteEntries(HDC hdc, int layer, int start, int n, COLORREF* c)
{
    (void)hdc, (void)layer, (void)start, (void)n, (void)c;
    return 0;
}
BOOL WINAPI wglRealizeLayerPalette(HDC hdc, int layer, BOOL realize)
{
    (void)hdc, (void)layer, (void)realize;
    return FALSE;
}
BOOL WINAPI wglUseFontBitmapsA(HDC hdc, DWORD first, DWORD count, DWORD base)
{
    (void)hdc, (void)first, (void)count, (void)base;
    fgl_unimplemented("wglUseFontBitmapsA");
    return FALSE;
}
BOOL WINAPI wglUseFontBitmapsW(HDC hdc, DWORD first, DWORD count, DWORD base)
{
    (void)hdc, (void)first, (void)count, (void)base;
    fgl_unimplemented("wglUseFontBitmapsW");
    return FALSE;
}
BOOL WINAPI wglUseFontOutlinesA(HDC hdc, DWORD first, DWORD count, DWORD base, FLOAT dev, FLOAT ext, int fmt, LPGLYPHMETRICSFLOAT gm)
{
    (void)hdc, (void)first, (void)count, (void)base, (void)dev, (void)ext, (void)fmt, (void)gm;
    fgl_unimplemented("wglUseFontOutlinesA");
    return FALSE;
}
BOOL WINAPI wglUseFontOutlinesW(HDC hdc, DWORD first, DWORD count, DWORD base, FLOAT dev, FLOAT ext, int fmt, LPGLYPHMETRICSFLOAT gm)
{
    (void)hdc, (void)first, (void)count, (void)base, (void)dev, (void)ext, (void)fmt, (void)gm;
    fgl_unimplemented("wglUseFontOutlinesW");
    return FALSE;
}

/* ---- diagnostics ---- */
void fgl_unimplemented(const char* name)
{
    static char seen[512][112];
    static int  nseen;
    fgl_error(GL_INVALID_OPERATION);
    for (int i = 0; i < nseen; i++)
        if (!strcmp(seen[i], name)) return;
    if (nseen < 512) snprintf(seen[nseen++], sizeof(seen[0]), "%s", name);
    char msg[160];
    snprintf(msg, sizeof(msg), "fatgl: %s is not implemented yet\n", name);
    OutputDebugStringA(msg);
    fgl_log("not implemented: %s\n", name);
    fgl_ctx* c = fgl_cur();
    if (c && c->dbg_cb) fgl_debug(c, GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_OTHER, GL_DEBUG_SEVERITY_MEDIUM, msg);
    if (getenv("FATGL_VERBOSE")) fputs(msg, stderr);
}
