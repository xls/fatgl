/* fatgl examples: a plain Win32 window with a WGL context (any opengl32.dll) */
#ifndef FGL_EXAMPLE_WIN_H
#define FGL_EXAMPLE_WIN_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef GL_BGR_EXT
#  define GL_BGR_EXT 0x80E0
#endif
#ifndef GL_PACK_ALIGNMENT
#  define GL_PACK_ALIGNMENT 0x0D05
#endif

static int         g_w = 960, g_h = 600, g_running = 1, g_frame;
static const char* g_shot; /* --shot file.bmp: save frame 30 and quit */

static LRESULT CALLBACK ex_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_SIZE: g_w = LOWORD(lp), g_h = HIWORD(lp); return 0;
    case WM_KEYDOWN: if (wp == VK_ESCAPE) PostQuitMessage(0); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: return DefWindowProcA(w, m, wp, lp);
    }
}

/* a window with a double buffered RGBA / depth pixel format and a current context */
static HDC ex_open(const char* title)
{
    for (int i = 1; i + 1 < __argc; i++)
        if (!strcmp(__argv[i], "--shot")) g_shot = __argv[i + 1];
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.style         = CS_OWNDC;
    wc.lpfnWndProc   = ex_proc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    wc.lpszClassName = "fatgl_example";
    RegisterClassA(&wc);
    RECT r = { 0, 0, g_w, g_h };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND wnd = CreateWindowA("fatgl_example", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                             r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    HDC dc = GetDC(wnd);
    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize      = sizeof(pfd);
    pfd.nVersion   = 1;
    pfd.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    int fmt = ChoosePixelFormat(dc, &pfd);
    if (!fmt || !SetPixelFormat(dc, fmt, &pfd)) {
        MessageBoxA(wnd, "no pixel format", title, MB_OK);
        return NULL;
    }
    HGLRC rc = wglCreateContext(dc);
    if (!rc || !wglMakeCurrent(dc, rc)) {
        MessageBoxA(wnd, "no OpenGL context", title, MB_OK);
        return NULL;
    }
    return dc;
}

static void ex_save_bmp(const char* path)
{
    int            w = g_w, h = g_h, stride = w * 3;
    unsigned char* px = (unsigned char*)malloc((size_t)stride * (size_t)h);
    if (!px) return;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_BGR_EXT, GL_UNSIGNED_BYTE, px);
    FILE* f = fopen(path, "wb");
    if (f) {
        int               pad = (4 - stride % 4) % 4, size = (stride + pad) * h;
        BITMAPFILEHEADER  fh  = { 0x4d42, (DWORD)(54 + size), 0, 0, 54 };
        BITMAPINFOHEADER  ih  = { sizeof(ih), w, h, 1, 24, BI_RGB, (DWORD)size, 2835, 2835, 0, 0 };
        static const char zero[4];
        fwrite(&fh, sizeof(fh), 1, f), fwrite(&ih, sizeof(ih), 1, f);
        for (int y = 0; y < h; y++) fwrite(px + (size_t)y * stride, 1, (size_t)stride, f), fwrite(zero, 1, (size_t)pad, f);
        fclose(f);
    }
    free(px);
}

/* pump messages; 0 once the window closed (or the --shot frame is saved) */
static int ex_poll(void)
{
    if (g_shot && ++g_frame > 30) {
        ex_save_bmp(g_shot);
        return 0;
    }
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) g_running = 0;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return g_running;
}

/* frames per second in the title, twice a second */
static void ex_fps(HDC dc, const char* name)
{
    static LARGE_INTEGER t0, f;
    static int           frames;
    LARGE_INTEGER        t;
    if (!f.QuadPart) QueryPerformanceFrequency(&f), QueryPerformanceCounter(&t0);
    frames++;
    QueryPerformanceCounter(&t);
    double s = (double)(t.QuadPart - t0.QuadPart) / (double)f.QuadPart;
    if (s < 0.5) return;
    char title[256];
    snprintf(title, sizeof(title), "%s | %s | %s | %.0f fps", name, (const char*)glGetString(GL_RENDERER),
             (const char*)glGetString(GL_VERSION), frames / s);
    SetWindowTextA(WindowFromDC(dc), title);
    frames = 0;
    t0     = t;
}

static double ex_time(void)
{
    static LARGE_INTEGER f, t0;
    LARGE_INTEGER        t;
    if (!f.QuadPart) QueryPerformanceFrequency(&f), QueryPerformanceCounter(&t0);
    QueryPerformanceCounter(&t);
    return (double)(t.QuadPart - t0.QuadPart) / (double)f.QuadPart;
}
#endif
