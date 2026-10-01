/*
 * fatgl - display lists: the recordable commands are stored while a list is
 * compiling and replayed through the GL entry points by glCallList.
 */
#include "fgl.h"

fgl_list* fgl_list_get(fgl_ctx* c, GLuint name, int create)
{
    for (int i = 0; i < c->nlists; i++)
        if (c->lists[i].used && c->lists[i].name == name) return &c->lists[i];
    if (!create) return NULL;
    int slot = -1;
    for (int i = 0; i < c->nlists && slot < 0; i++)
        if (!c->lists[i].used) slot = i;
    if (slot < 0) {
        fgl_list* n = (fgl_list*)realloc(c->lists, ((size_t)c->nlists + 16) * sizeof(fgl_list));
        if (!n) return NULL;
        memset(n + c->nlists, 0, 16 * sizeof(fgl_list));
        c->lists = n, slot = c->nlists, c->nlists += 16;
    }
    fgl_list* l = &c->lists[slot];
    memset(l, 0, sizeof(*l));
    l->used = 1, l->name = name;
    return l;
}

int fgl_record(fgl_ctx* c, int op, GLenum e0, GLenum e1, const float* f, int nf)
{
    fgl_list* l = fgl_list_get(c, c->list_compiling, 1);
    if (l) {
        if (l->nops == l->cap) {
            int     n  = l->cap ? l->cap * 2 : 256;
            fgl_op* no = (fgl_op*)realloc(l->ops, (size_t)n * sizeof(fgl_op));
            if (!no) {
                fgl_error(GL_OUT_OF_MEMORY);
                return c->list_mode == GL_COMPILE;
            }
            l->ops = no, l->cap = n;
        }
        fgl_op* o = &l->ops[l->nops++];
        o->op = op, o->e0 = e0, o->e1 = e1;
        if (nf > 16) nf = 16;
        if (f && nf > 0) memcpy(o->f, f, (size_t)nf * sizeof(float));
    }
    return c->list_mode == GL_COMPILE;
}

static GLuint g_next_list = 1;

GLuint APIENTRY glGenLists(GLsizei range)
{
    fgl_ctx* c = fgl_cur();
    if (!c || range <= 0) return 0;
    GLuint base = g_next_list;
    g_next_list += (GLuint)range;
    return base;
}

void APIENTRY glNewList(GLuint name, GLenum mode)
{
    FGL_CTX_OR_RETURN(c);
    if (name == 0 || (mode != GL_COMPILE && mode != GL_COMPILE_AND_EXECUTE)) {
        fgl_error(name == 0 ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return;
    }
    if (c->list_compiling) {
        fgl_error(GL_INVALID_OPERATION);
        return;
    }
    fgl_list* l = fgl_list_get(c, name, 1);
    if (!l) return;
    l->nops = 0; /* replaces the old contents */
    c->list_compiling = name, c->list_mode = mode;
}

void APIENTRY glEndList(void)
{
    FGL_CTX_OR_RETURN(c);
    if (!c->list_compiling) {
        fgl_error(GL_INVALID_OPERATION);
        return;
    }
    c->list_compiling = 0;
}

GLboolean APIENTRY glIsList(GLuint name)
{
    fgl_ctx* c = fgl_cur();
    return c && fgl_list_get(c, name, 0) ? GL_TRUE : GL_FALSE;
}

void APIENTRY glDeleteLists(GLuint first, GLsizei range)
{
    FGL_CTX_OR_RETURN(c);
    for (GLsizei i = 0; i < range; i++) {
        fgl_list* l = fgl_list_get(c, first + (GLuint)i, 0);
        if (!l) continue;
        free(l->ops);
        memset(l, 0, sizeof(*l));
    }
}

static void fgl_replay(fgl_ctx* c, GLuint name)
{
    fgl_list* l = fgl_list_get(c, name, 0);
    if (!l || c->list_depth >= 64) return;
    c->list_depth++;
    for (int i = 0; i < l->nops; i++) {
        const fgl_op* o = &l->ops[i];
        const float*  f = o->f;
        switch (o->op) {
        case FGL_OP_BEGIN: glBegin(o->e0); break;
        case FGL_OP_END: glEnd(); break;
        case FGL_OP_VERTEX: glVertex4f(f[0], f[1], f[2], f[3]); break;
        case FGL_OP_NORMAL: glNormal3f(f[0], f[1], f[2]); break;
        case FGL_OP_COLOR: glColor4f(f[0], f[1], f[2], f[3]); break;
        case FGL_OP_TEXCOORD: glTexCoord4f(f[0], f[1], f[2], f[3]); break;
        case FGL_OP_MATERIAL: glMaterialfv(o->e0, o->e1, f); break;
        case FGL_OP_LIGHT: glLightfv(o->e0, o->e1, f); break;
        case FGL_OP_LIGHTMODEL: glLightModelfv(o->e0, f); break;
        case FGL_OP_SHADEMODEL: glShadeModel(o->e0); break;
        case FGL_OP_ENABLE: glEnable(o->e0); break;
        case FGL_OP_DISABLE: glDisable(o->e0); break;
        case FGL_OP_MATRIXMODE: glMatrixMode(o->e0); break;
        case FGL_OP_LOADIDENTITY: glLoadIdentity(); break;
        case FGL_OP_LOADMATRIX: glLoadMatrixf(f); break;
        case FGL_OP_MULTMATRIX: glMultMatrixf(f); break;
        case FGL_OP_PUSHMATRIX: glPushMatrix(); break;
        case FGL_OP_POPMATRIX: glPopMatrix(); break;
        case FGL_OP_ROTATE: glRotatef(f[0], f[1], f[2], f[3]); break;
        case FGL_OP_TRANSLATE: glTranslatef(f[0], f[1], f[2]); break;
        case FGL_OP_SCALE: glScalef(f[0], f[1], f[2]); break;
        case FGL_OP_BINDTEXTURE: glBindTexture(o->e0, o->e1); break;
        case FGL_OP_CALLLIST: fgl_replay(c, o->e0); break;
        case FGL_OP_BLENDFUNC: glBlendFunc(o->e0, o->e1); break;
        case FGL_OP_DEPTHFUNC: glDepthFunc(o->e0); break;
        case FGL_OP_FRONTFACE: glFrontFace(o->e0); break;
        case FGL_OP_CULLFACE: glCullFace(o->e0); break;
        case FGL_OP_COLORMATERIAL: glColorMaterial(o->e0, o->e1); break;
        default: break;
        }
    }
    c->list_depth--;
}

void APIENTRY glCallList(GLuint name)
{
    FGL_CTX_OR_RETURN(c);
    if (c->list_compiling && fgl_record(c, FGL_OP_CALLLIST, name, 0, NULL, 0)) return;
    GLuint compiling   = c->list_compiling; /* executing: commands run, not recorded */
    c->list_compiling = 0;
    fgl_replay(c, name);
    c->list_compiling = compiling;
}

void APIENTRY glCallLists(GLsizei n, GLenum type, const void* lists)
{
    for (GLsizei i = 0; i < n; i++) {
        GLuint name = type == GL_UNSIGNED_BYTE ? ((const uint8_t*)lists)[i]
                                               : (type == GL_UNSIGNED_SHORT ? ((const uint16_t*)lists)[i] : ((const uint32_t*)lists)[i]);
        glCallList(name);
    }
}
