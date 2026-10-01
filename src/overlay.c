/*
 * fatgl - the performance overlay (F10).
 *
 * A low level keyboard hook on its own thread (started when the DLL loads;
 * hooks cannot be installed from DllMain itself) toggles the overlay while
 * a window of this process has the focus and swallows the key, so the
 * application never sees F10. FATGL_OVERLAY=1 starts with it on,
 * FATGL_HOTKEY=0 installs no hook.
 *
 * At SwapBuffers the overlay is drawn into the back buffer: fatmap's
 * kernel statistics (fm3d_stats deltas: triangles, fragments, tiles, phase
 * times, worker utilization) and fatgl's own counters, averaged over half
 * a second, and a frame time graph. Text comes from stb_easy_font (quads,
 * filled as rectangles).
 */
#include "fgl.h"
#include <stdio.h>

#define STB_EASY_FONT_IMPLEMENTATION
#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-function"
#  pragma GCC diagnostic ignored "-Wsign-compare"
#  pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#  pragma GCC diagnostic ignored "-Wmissing-braces"
#endif
#include "stb_easy_font.h"
#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

static volatile LONG g_overlay = -1; /* -1: not decided yet (FATGL_OVERLAY) */
static HHOOK         g_hook;
static DWORD         g_hook_tid;
static int           g_f10_down; /* swallowed the key down: swallow its key up */

static int fgl_our_foreground(void)
{
    DWORD pid = 0;
    HWND  w   = GetForegroundWindow();
    if (w) GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

static LRESULT CALLBACK fgl_kbd(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION) {
        const KBDLLHOOKSTRUCT* k = (const KBDLLHOOKSTRUCT*)lp;
        if (k->vkCode == VK_F10) {
            int down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN, up = wp == WM_KEYUP || wp == WM_SYSKEYUP;
            if (down && fgl_our_foreground()) {
                if (!g_f10_down) InterlockedExchange(&g_overlay, g_overlay > 0 ? 0 : 1); /* not on auto repeat */
                g_f10_down = 1;
                return 1; /* the application does not see it */
            }
            if (up && g_f10_down) {
                g_f10_down = 0;
                return 1;
            }
        }
    }
    return CallNextHookEx(g_hook, code, wp, lp);
}

static DWORD WINAPI fgl_hook_thread(LPVOID arg)
{
    g_hook = SetWindowsHookExA(WH_KEYBOARD_LL, fgl_kbd, (HINSTANCE)arg, 0);
    if (!g_hook) {
        fgl_log("overlay: no keyboard hook (error %lu); FATGL_OVERLAY=1 shows it anyway\n", GetLastError());
        return 0;
    }
    MSG m;
    while (GetMessageA(&m, NULL, 0, 0) > 0) {
    }
    UnhookWindowsHookEx(g_hook);
    g_hook = NULL;
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        const char* e = getenv("FATGL_HOTKEY");
        if (!e || strcmp(e, "0")) { /* the thread starts after the loader lock is released */
            HANDLE t = CreateThread(NULL, 0, fgl_hook_thread, inst, 0, &g_hook_tid);
            if (t) CloseHandle(t);
        }
    } else if (reason == DLL_PROCESS_DETACH && g_hook_tid) {
        PostThreadMessageA(g_hook_tid, WM_QUIT, 0, 0);
    }
    return TRUE;
}

/* ---- statistics ---- */
#define FGL_HIST 160

typedef struct fgl_overlay {
    fm3d_stats last;      /* fatmap's totals at the previous frame */
    fm3d_stats acc;       /* deltas over the averaging window */
    fgl_counters cnt_acc; /* fatgl's counters over the window */
    uint64_t   t_frame, t_window, ns_window_max;
    int        frames;
    float      hist[FGL_HIST]; /* frame times, ms */
    int        nhist;
    char       text[2048]; /* the last averaged report */
    int        reported;
} fgl_overlay;

static void fgl_stats_sub(fm3d_stats* d, const fm3d_stats* a, const fm3d_stats* b)
{
    const uint64_t* pa = (const uint64_t*)a;
    const uint64_t* pb = (const uint64_t*)b;
    uint64_t*       pd = (uint64_t*)d;
    for (size_t i = 0; i < sizeof(fm3d_stats) / 8; i++) pd[i] = pa[i] - pb[i];
    d->workers = a->workers;
}
static void fgl_stats_add(fm3d_stats* d, const fm3d_stats* s)
{
    const uint64_t* ps = (const uint64_t*)s;
    uint64_t*       pd = (uint64_t*)d;
    for (size_t i = 0; i < sizeof(fm3d_stats) / 8; i++) pd[i] += ps[i];
    d->workers = s->workers;
}

static const char* fgl_k(double v, char* buf, size_t n) /* 1234567 -> 1.23M */
{
    if (v >= 1e6) snprintf(buf, n, "%.2fM", v / 1e6);
    else if (v >= 1e4) snprintf(buf, n, "%.1fk", v / 1e3);
    else snprintf(buf, n, "%.0f", v);
    return buf;
}

static void fgl_overlay_report(fgl_ctx* c, fgl_overlay* o, double secs)
{
    double            f  = o->frames > 0 ? (double)o->frames : 1.0;
    const fm3d_stats* s  = &o->acc;
    const fgl_counters* k = &o->cnt_acc;
    double            ms = secs * 1000.0 / f;
    double            nsk = (double)(s->ns_vertex + s->ns_setup + s->ns_raster);
    double            util = nsk > 0 && s->workers ? (double)s->ns_busy / (nsk * (double)s->workers) * 100.0 : 0.0;
    double            px   = (double)c->fbw * (double)c->fbh;
    char              a[32], b[32], d[32], e[32];
    int               n    = 0;
    n += snprintf(o->text + n, sizeof(o->text) - (size_t)n, "fatgl  fatmap %s  %s  %d threads  %d x %d  (F10)\n", fm_version_string(),
                  fm_simd_name(fm_simd_best()), (int)(s->workers ? s->workers : 1), c->fbw, c->fbh);
    n += snprintf(o->text + n, sizeof(o->text) - (size_t)n, "%.1f fps   %.2f ms   max %.2f ms\n", ms > 0 ? 1000.0 / ms : 0.0, ms,
                  (double)o->ns_window_max / 1e6);
    n += snprintf(o->text + n, sizeof(o->text) - (size_t)n, "draws %s  (fixed %s, shader %s)   flushes %.1f\n",
                  fgl_k((double)s->draws / f, a, sizeof(a)), fgl_k((double)k->draws_fixed / f, b, sizeof(b)),
                  fgl_k((double)k->draws_prog / f, d, sizeof(d)), (double)s->flushes / f);
    n += snprintf(o->text + n, sizeof(o->text) - (size_t)n, "tris in %s  drawn %s  clipped %s  culled %s\n",
                  fgl_k((double)s->triangles_in / f, a, sizeof(a)), fgl_k((double)s->triangles_drawn / f, b, sizeof(b)),
                  fgl_k((double)s->triangles_clipped / f, d, sizeof(d)), fgl_k((double)s->triangles_culled / f, e, sizeof(e)));
    n += snprintf(o->text + n, sizeof(o->text) - (size_t)n, "pixels in %s  shaded %s  overdraw %.2f  hi-z rejects %s\n",
                  fgl_k((double)s->fragments_in / f, a, sizeof(a)), fgl_k((double)s->fragments_shaded / f, b, sizeof(b)),
                  px > 0 ? (double)s->fragments_shaded / f / px : 0.0, fgl_k((double)s->hiz_rejected / f, d, sizeof(d)));
    n += snprintf(o->text + n, sizeof(o->text) - (size_t)n, "tiles %s  bins %s   fill %.1f Mpix/s\n", fgl_k((double)s->tiles / f, a, sizeof(a)),
                  fgl_k((double)s->tile_items / f, b, sizeof(b)), secs > 0 ? (double)s->fragments_shaded / secs / 1e6 : 0.0);
    n += snprintf(o->text + n, sizeof(o->text) - (size_t)n, "ms/frame  vertex %.2f  setup %.2f  raster %.2f  other %.2f\n",
                  (double)s->ns_vertex / f / 1e6, (double)s->ns_setup / f / 1e6, (double)s->ns_raster / f / 1e6,
                  ms - nsk / f / 1e6 > 0 ? ms - nsk / f / 1e6 : 0.0);
    n += snprintf(o->text + n, sizeof(o->text) - (size_t)n, "threads busy %.0f%%\n", util);
    n += snprintf(o->text + n, sizeof(o->text) - (size_t)n, "textures built %.1f   uploaded %.2f MB\n", (double)k->tex_builds / f,
                  (double)k->upload_bytes / f / 1048576.0);
    (void)n;
}

/* ---- drawing into the back buffer (rows bottom up) ---- */
static void fgl_rect(fm_surface* s, int x0, int y0, int x1, int y1, uint32_t color, int darken)
{
    x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > s->width ? s->width : x1, y1 = y1 > s->height ? s->height : y1;
    for (int y = y0; y < y1; y++) {
        uint32_t* r = fm_surface_row32(s, s->height - 1 - y); /* y counts from the top */
        if (darken)
            for (int x = x0; x < x1; x++) r[x] = 0xFF000000u | ((r[x] >> 2) & 0x3F3F3Fu);
        else
            for (int x = x0; x < x1; x++) r[x] = color;
    }
}

static void fgl_text_line(fm_surface* s, int x, int y, int scale, const char* text, uint32_t color)
{
    static char quads[64 * 1024]; /* ~ 10 quads of 64 bytes per character: one line at a time */
    int         nq = stb_easy_font_print(0, 0, (char*)text, NULL, quads, (int)sizeof(quads));
    for (int q = 0; q < nq; q++) {
        const float* v  = (const float*)(quads + q * 64); /* 4 vertices: x y z color */
        float        qx0 = v[0], qy0 = v[1], qx1 = v[8], qy1 = v[9];
        fgl_rect(s, x + (int)(qx0 * (float)scale), y + (int)(qy0 * (float)scale), x + (int)(qx1 * (float)scale),
                 y + (int)(qy1 * (float)scale), color, 0);
    }
}

static void fgl_text(fm_surface* s, int x, int y, int scale, const char* text, uint32_t color)
{
    char line[256];
    while (*text) {
        size_t n = strcspn(text, "\n");
        n        = n < sizeof(line) - 1 ? n : sizeof(line) - 1;
        memcpy(line, text, n), line[n] = 0;
        fgl_text_line(s, x, y, scale, line, color);
        y += 12 * scale;
        text += strcspn(text, "\n");
        if (*text == '\n') text++;
    }
}

void fgl_overlay_frame(fgl_ctx* c)
{
    if (g_overlay < 0) {
        const char* e = getenv("FATGL_OVERLAY");
        InterlockedExchange(&g_overlay, e && strcmp(e, "0") ? 1 : 0);
    }
    fgl_overlay* o = (fgl_overlay*)c->overlay;
    if (!g_overlay) {
        if (o) o->frames = -1; /* start over when shown again */
        memset(&c->cnt, 0, sizeof(c->cnt));
        return;
    }
    if (!o) {
        o = (fgl_overlay*)calloc(1, sizeof(fgl_overlay));
        if (!o) return;
        c->overlay = o;
        o->frames  = -1;
    }
    uint64_t   now = fm_time_ns();
    fm3d_stats st  = fm3d_get_stats(c->c3), d;
    if (o->frames < 0) { /* first frame: a baseline */
        o->last = st, o->t_frame = o->t_window = now, o->frames = 0;
        memset(&o->acc, 0, sizeof(o->acc));
        memset(&o->cnt_acc, 0, sizeof(o->cnt_acc));
        memset(&c->cnt, 0, sizeof(c->cnt));
        snprintf(o->text, sizeof(o->text), "fatgl overlay (F10)\n");
        return;
    }
    fgl_stats_sub(&d, &st, &o->last);
    o->last = st;
    fgl_stats_add(&o->acc, &d);
    uint64_t* ka = (uint64_t*)&o->cnt_acc;
    uint64_t* kc = (uint64_t*)&c->cnt;
    for (size_t i = 0; i < sizeof(fgl_counters) / 8; i++) ka[i] += kc[i];
    memset(&c->cnt, 0, sizeof(c->cnt));
    uint64_t ft = now - o->t_frame;
    o->t_frame  = now;
    o->ns_window_max = ft > o->ns_window_max ? ft : o->ns_window_max;
    memmove(o->hist, o->hist + 1, sizeof(o->hist) - sizeof(o->hist[0]));
    o->hist[FGL_HIST - 1] = (float)((double)ft / 1e6);
    o->nhist              = o->nhist < FGL_HIST ? o->nhist + 1 : FGL_HIST;
    o->frames++;
    if (now - o->t_window >= 500000000ull || (!o->reported && o->frames >= 5)) { /* every half second (the first soon) */
        o->reported = 1;
        fgl_overlay_report(c, o, (double)(now - o->t_window) / 1e9);
        memset(&o->acc, 0, sizeof(o->acc));
        memset(&o->cnt_acc, 0, sizeof(o->cnt_acc));
        o->frames = 0, o->t_window = now, o->ns_window_max = 0;
    }

    /* the panel, the text, the frame time graph (16.7 ms line) */
    fm_surface* s     = c->color;
    int         scale = s->height >= 1000 ? 2 : 1;
    int         w     = stb_easy_font_width(o->text) * scale + 16 * scale, lines = 1;
    for (const char* p = o->text; *p; p++) lines += *p == '\n';
    int th = stb_easy_font_height(o->text) * scale, gh = 40 * scale, h = th + gh + 16 * scale;
    if (w < FGL_HIST * scale + 16 * scale) w = FGL_HIST * scale + 16 * scale;
    fgl_rect(s, 8, 8, 8 + w, 8 + h, 0, 1);
    fgl_text(s, 8 + 8 * scale, 8 + 6 * scale, scale, o->text, 0xFFE8E8E8u);
    int gx = 8 + 8 * scale, gy = 8 + th + 10 * scale; /* graph: top gy, height gh */
    float full = 33.3f;                               /* ms at the top of the graph */
    for (int i = 0; i < o->nhist; i++) {
        float    v   = o->hist[FGL_HIST - o->nhist + i];
        int      bar = (int)(v / full * (float)gh);
        bar          = bar > gh ? gh : (bar < 1 ? 1 : bar);
        uint32_t col = v <= 16.7f ? 0xFF50D060u : (v <= 33.3f ? 0xFFE0C040u : 0xFFE05040u);
        int      x   = gx + (FGL_HIST - o->nhist + i) * scale;
        fgl_rect(s, x, gy + gh - bar, x + scale, gy + gh, col, 0);
    }
    fgl_rect(s, gx, gy + gh - (int)(16.7f / full * (float)gh), gx + FGL_HIST * scale, gy + gh - (int)(16.7f / full * (float)gh) + 1,
             0xFF808080u, 0);
    (void)lines;
}

void fgl_overlay_free(fgl_ctx* c)
{
    free(c->overlay);
    c->overlay = NULL;
}
