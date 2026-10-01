/* fatgl example: three meshing gears (display lists, lighting, flat
 * shading, quad strips: the classic OpenGL 1.x demo, written for fatgl) */
#include "win.h"
#include <math.h>

#define PI 3.14159265f

/* a gear in the z = 0 plane: inner radius, outer radius (tooth roots at
 * outer - depth / 2), width along z, teeth */
static void gear(float r0, float r1, float width, int teeth, float depth)
{
    float ra = r1 - depth / 2, rb = r1 + depth / 2, da = 2 * PI / teeth / 4, hw = width / 2;
    glShadeModel(GL_FLAT);
    glNormal3f(0, 0, 1);
    glBegin(GL_QUAD_STRIP); /* front face */
    for (int i = 0; i <= teeth; i++) {
        float a = i * 2 * PI / teeth;
        glVertex3f(r0 * cosf(a), r0 * sinf(a), hw);
        glVertex3f(ra * cosf(a), ra * sinf(a), hw);
        if (i < teeth) {
            glVertex3f(r0 * cosf(a), r0 * sinf(a), hw);
            glVertex3f(ra * cosf(a + 3 * da), ra * sinf(a + 3 * da), hw);
        }
    }
    glEnd();
    glBegin(GL_QUADS); /* front sides of the teeth */
    for (int i = 0; i < teeth; i++) {
        float a = i * 2 * PI / teeth;
        glVertex3f(ra * cosf(a), ra * sinf(a), hw);
        glVertex3f(rb * cosf(a + da), rb * sinf(a + da), hw);
        glVertex3f(rb * cosf(a + 2 * da), rb * sinf(a + 2 * da), hw);
        glVertex3f(ra * cosf(a + 3 * da), ra * sinf(a + 3 * da), hw);
    }
    glEnd();
    glNormal3f(0, 0, -1);
    glBegin(GL_QUAD_STRIP); /* back face */
    for (int i = 0; i <= teeth; i++) {
        float a = i * 2 * PI / teeth;
        glVertex3f(ra * cosf(a), ra * sinf(a), -hw);
        glVertex3f(r0 * cosf(a), r0 * sinf(a), -hw);
        if (i < teeth) {
            glVertex3f(ra * cosf(a + 3 * da), ra * sinf(a + 3 * da), -hw);
            glVertex3f(r0 * cosf(a), r0 * sinf(a), -hw);
        }
    }
    glEnd();
    glBegin(GL_QUADS); /* back sides of the teeth */
    for (int i = 0; i < teeth; i++) {
        float a = i * 2 * PI / teeth;
        glVertex3f(ra * cosf(a + 3 * da), ra * sinf(a + 3 * da), -hw);
        glVertex3f(rb * cosf(a + 2 * da), rb * sinf(a + 2 * da), -hw);
        glVertex3f(rb * cosf(a + da), rb * sinf(a + da), -hw);
        glVertex3f(ra * cosf(a), ra * sinf(a), -hw);
    }
    glEnd();
    glBegin(GL_QUAD_STRIP); /* outward faces of the teeth */
    for (int i = 0; i < teeth; i++) {
        float a = i * 2 * PI / teeth;
        float u = rb * cosf(a + da) - ra * cosf(a), v = rb * sinf(a + da) - ra * sinf(a), l = sqrtf(u * u + v * v);
        glVertex3f(ra * cosf(a), ra * sinf(a), hw);
        glVertex3f(ra * cosf(a), ra * sinf(a), -hw);
        glNormal3f(v / l, -u / l, 0);
        glVertex3f(rb * cosf(a + da), rb * sinf(a + da), hw);
        glVertex3f(rb * cosf(a + da), rb * sinf(a + da), -hw);
        glNormal3f(cosf(a), sinf(a), 0);
        glVertex3f(rb * cosf(a + 2 * da), rb * sinf(a + 2 * da), hw);
        glVertex3f(rb * cosf(a + 2 * da), rb * sinf(a + 2 * da), -hw);
        u = ra * cosf(a + 3 * da) - rb * cosf(a + 2 * da), v = ra * sinf(a + 3 * da) - rb * sinf(a + 2 * da);
        glNormal3f(v, -u, 0);
        glVertex3f(ra * cosf(a + 3 * da), ra * sinf(a + 3 * da), hw);
        glVertex3f(ra * cosf(a + 3 * da), ra * sinf(a + 3 * da), -hw);
        glNormal3f(cosf(a), sinf(a), 0);
    }
    glVertex3f(ra, 0, hw);
    glVertex3f(ra, 0, -hw);
    glEnd();
    glShadeModel(GL_SMOOTH);
    glBegin(GL_QUAD_STRIP); /* the inside cylinder */
    for (int i = 0; i <= teeth; i++) {
        float a = i * 2 * PI / teeth;
        glNormal3f(-cosf(a), -sinf(a), 0);
        glVertex3f(r0 * cosf(a), r0 * sinf(a), -hw);
        glVertex3f(r0 * cosf(a), r0 * sinf(a), hw);
    }
    glEnd();
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)inst, (void)prev, (void)cmd, (void)show;
    HDC dc = ex_open("fatgl gears");
    if (!dc) return 1;
    static const GLfloat pos[4] = { 5, 5, 10, 0 }, red[4] = { 0.8f, 0.1f, 0, 1 }, green[4] = { 0, 0.8f, 0.2f, 1 },
                         blue[4] = { 0.2f, 0.2f, 1, 1 };
    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glEnable(GL_CULL_FACE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_DEPTH_TEST);
    GLuint g1 = glGenLists(3), g2 = g1 + 1, g3 = g1 + 2;
    glNewList(g1, GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, red);
    gear(1.0f, 4.0f, 1.0f, 20, 0.7f);
    glEndList();
    glNewList(g2, GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, green);
    gear(0.5f, 2.0f, 2.0f, 10, 0.7f);
    glEndList();
    glNewList(g3, GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, blue);
    gear(1.3f, 2.0f, 0.5f, 10, 0.7f);
    glEndList();
    while (ex_poll()) {
        float t = (float)ex_time(), angle = t * 70.0f;
        glViewport(0, 0, g_w, g_h);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        float h = (float)g_h / (float)(g_w > 0 ? g_w : 1);
        glFrustum(-1, 1, -h, h, 5, 60);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(0, 0, -40);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glPushMatrix();
        glRotatef(20, 1, 0, 0);
        glRotatef(30 + 10 * sinf(t * 0.5f), 0, 1, 0);
        glPushMatrix();
        glTranslatef(-3, -2, 0);
        glRotatef(angle, 0, 0, 1);
        glCallList(g1);
        glPopMatrix();
        glPushMatrix();
        glTranslatef(3.1f, -2, 0);
        glRotatef(-2 * angle - 9, 0, 0, 1);
        glCallList(g2);
        glPopMatrix();
        glPushMatrix();
        glTranslatef(-3.1f, 4.2f, 0);
        glRotatef(-2 * angle - 25, 0, 0, 1);
        glCallList(g3);
        glPopMatrix();
        glPopMatrix();
        SwapBuffers(dc);
        ex_fps(dc, "gears");
    }
    return 0;
}
