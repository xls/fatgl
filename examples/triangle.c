/* fatgl example: a spinning triangle with per vertex colors (OpenGL 1.1) */
#include "win.h"

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)inst, (void)prev, (void)cmd, (void)show;
    HDC dc = ex_open("fatgl triangle");
    if (!dc) return 1;
    while (ex_poll()) {
        glViewport(0, 0, g_w, g_h);
        glClearColor(0.08f, 0.09f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        float a = (float)g_w / (float)(g_h > 0 ? g_h : 1);
        glOrtho(-a, a, -1, 1, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glRotatef((float)(ex_time() * 45.0), 0, 0, 1);
        glBegin(GL_TRIANGLES);
        glColor3f(1, 0.2f, 0.2f), glVertex2f(0.0f, 0.8f);
        glColor3f(0.2f, 1, 0.2f), glVertex2f(-0.7f, -0.5f);
        glColor3f(0.2f, 0.4f, 1), glVertex2f(0.7f, -0.5f);
        glEnd();
        SwapBuffers(dc);
        ex_fps(dc, "triangle");
    }
    return 0;
}
