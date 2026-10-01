/*
 * fatgl - framebuffer and renderbuffer objects.
 *
 * Every framebuffer, the window's included, is rendered with fatmap's lower
 * left origin (row 0 at the bottom), so textures drawn through a
 * framebuffer come out the way GL samples them. fatmap draws into one color
 * target: COLOR_ATTACHMENT0. Framebuffers without a color attachment (depth
 * only passes) get a dummy color target with color writes off.
 */
#include "fgl.h"

/* ---- renderbuffers ---- */
static fgl_rbo* fgl_rbo_get(fgl_ctx* c, GLuint name, int create)
{
    for (int i = 0; i < c->nrbos; i++)
        if (c->rbos[i].used && c->rbos[i].name == name) return &c->rbos[i];
    if (!create || !name) return NULL;
    int slot = -1;
    for (int i = 0; i < c->nrbos && slot < 0; i++)
        if (!c->rbos[i].used) slot = i;
    if (slot < 0) {
        fgl_rbo* n = (fgl_rbo*)realloc(c->rbos, ((size_t)c->nrbos + 8) * sizeof(fgl_rbo));
        if (!n) return NULL;
        memset(n + c->nrbos, 0, 8 * sizeof(fgl_rbo));
        c->rbos = n, slot = c->nrbos, c->nrbos += 8;
    }
    fgl_rbo* r = &c->rbos[slot];
    memset(r, 0, sizeof(*r));
    r->used = 1, r->name = name, r->ifmt = GL_RGBA4;
    return r;
}

static GLuint g_next_rbo = 1, g_next_fbo = 1;

void APIENTRY glGenRenderbuffers(GLsizei n, GLuint* out)
{
    FGL_CTX_OR_RETURN(c);
    for (GLsizei i = 0; i < n; i++) {
        while (fgl_rbo_get(c, g_next_rbo, 0)) g_next_rbo++;
        out[i] = g_next_rbo++;
        fgl_rbo_get(c, out[i], 1);
    }
}

static void fgl_detach_all(fgl_ctx* c, GLenum type, GLuint name);

void APIENTRY glDeleteRenderbuffers(GLsizei n, const GLuint* names)
{
    FGL_CTX_OR_RETURN(c);
    for (GLsizei i = 0; i < n; i++) {
        fgl_rbo* r = names[i] ? fgl_rbo_get(c, names[i], 0) : NULL;
        if (!r) continue;
        fgl_detach_all(c, GL_RENDERBUFFER, names[i]);
        fgl_surface_gone(c, r->s);
        fm_surface_destroy(r->s);
        memset(r, 0, sizeof(*r));
        if (c->renderbuffer == names[i]) c->renderbuffer = 0;
    }
}

GLboolean APIENTRY glIsRenderbuffer(GLuint name)
{
    fgl_ctx* c = fgl_cur();
    return c && name && fgl_rbo_get(c, name, 0) ? GL_TRUE : GL_FALSE;
}

void APIENTRY glBindRenderbuffer(GLenum target, GLuint name)
{
    FGL_CTX_OR_RETURN(c);
    if (target != GL_RENDERBUFFER) {
        fgl_error(GL_INVALID_ENUM);
        return;
    }
    if (name) fgl_rbo_get(c, name, 1);
    c->renderbuffer = name;
}

/* the fatmap surface format of an internal format: 0 color, else a depth / stencil format */
static int fgl_ifmt_depth(GLenum ifmt, fm_format* f)
{
    switch (ifmt) {
    case GL_DEPTH_COMPONENT16: *f = FM_FORMAT_D16; return 1;
    case GL_DEPTH_COMPONENT32F: case GL_DEPTH_COMPONENT32: *f = FM_FORMAT_D32F; return 1;
    case GL_DEPTH_COMPONENT: case GL_DEPTH_COMPONENT24: case GL_DEPTH_STENCIL: case GL_DEPTH24_STENCIL8:
    case GL_DEPTH32F_STENCIL8: *f = FM_FORMAT_D24S8; return 1;
    case GL_STENCIL_INDEX: case GL_STENCIL_INDEX1: case GL_STENCIL_INDEX4: case GL_STENCIL_INDEX8: case GL_STENCIL_INDEX16:
        *f = FM_FORMAT_A8;
        return 1;
    default: *f = FM_FORMAT_ARGB32; return 0;
    }
}

void APIENTRY glRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum ifmt, GLsizei w, GLsizei h)
{
    FGL_CTX_OR_RETURN(c);
    (void)samples; /* single sampled: blits resolve nothing */
    fgl_rbo* r = target == GL_RENDERBUFFER ? fgl_rbo_get(c, c->renderbuffer, 0) : NULL;
    if (!r) {
        fgl_error(target == GL_RENDERBUFFER ? GL_INVALID_OPERATION : GL_INVALID_ENUM);
        return;
    }
    if (w < 0 || h < 0 || w > 8192 || h > 8192) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    fm_format f;
    fgl_ifmt_depth(ifmt, &f);
    fgl_surface_gone(c, r->s);
    fm_surface_destroy(r->s);
    r->s    = w > 0 && h > 0 ? fm_surface_create(w, h, f) : NULL;
    r->ifmt = ifmt;
    if (r->s && f == FM_FORMAT_D24S8) /* depth 1, stencil 0 until cleared, like a fresh GL buffer reads */
        for (int y = 0; y < h; y++) {
            uint32_t* d = fm_surface_row32(r->s, y);
            for (int x = 0; x < w; x++) d[x] = 0xFFFFFFu;
        }
}

void APIENTRY glRenderbufferStorage(GLenum target, GLenum ifmt, GLsizei w, GLsizei h)
{
    glRenderbufferStorageMultisample(target, 0, ifmt, w, h);
}

void APIENTRY glGetRenderbufferParameteriv(GLenum target, GLenum p, GLint* v)
{
    FGL_CTX_OR_RETURN(c);
    fgl_rbo* r = target == GL_RENDERBUFFER ? fgl_rbo_get(c, c->renderbuffer, 0) : NULL;
    if (!r) {
        fgl_error(GL_INVALID_OPERATION);
        return;
    }
    fm_format f;
    int       dep = fgl_ifmt_depth(r->ifmt, &f);
    switch (p) {
    case GL_RENDERBUFFER_WIDTH: *v = r->s ? r->s->width : 0; break;
    case GL_RENDERBUFFER_HEIGHT: *v = r->s ? r->s->height : 0; break;
    case GL_RENDERBUFFER_INTERNAL_FORMAT: *v = (GLint)r->ifmt; break;
    case GL_RENDERBUFFER_SAMPLES: *v = 0; break;
    case GL_RENDERBUFFER_RED_SIZE: case GL_RENDERBUFFER_GREEN_SIZE: case GL_RENDERBUFFER_BLUE_SIZE:
    case GL_RENDERBUFFER_ALPHA_SIZE: *v = dep ? 0 : 8; break;
    case GL_RENDERBUFFER_DEPTH_SIZE: *v = !dep || f == FM_FORMAT_A8 ? 0 : (f == FM_FORMAT_D16 ? 16 : (f == FM_FORMAT_D32F ? 32 : 24)); break;
    case GL_RENDERBUFFER_STENCIL_SIZE: *v = f == FM_FORMAT_D24S8 || f == FM_FORMAT_A8 ? 8 : 0; break;
    default: fgl_error(GL_INVALID_ENUM); break;
    }
}

/* ---- framebuffers ---- */
static fgl_fbo* fgl_fbo_get(fgl_ctx* c, GLuint name, int create)
{
    for (int i = 0; i < c->nfbos; i++)
        if (c->fbos[i].used && c->fbos[i].name == name) return &c->fbos[i];
    if (!create || !name) return NULL;
    int slot = -1;
    for (int i = 0; i < c->nfbos && slot < 0; i++)
        if (!c->fbos[i].used) slot = i;
    if (slot < 0) {
        fgl_fbo* n = (fgl_fbo*)realloc(c->fbos, ((size_t)c->nfbos + 8) * sizeof(fgl_fbo));
        if (!n) return NULL;
        memset(n + c->nfbos, 0, 8 * sizeof(fgl_fbo));
        c->fbos = n, slot = c->nfbos, c->nfbos += 8;
    }
    fgl_fbo* f = &c->fbos[slot];
    memset(f, 0, sizeof(*f));
    f->used = 1, f->name = name;
    f->draw_buf[0] = GL_COLOR_ATTACHMENT0, f->read_buf = GL_COLOR_ATTACHMENT0;
    return f;
}

void APIENTRY glGenFramebuffers(GLsizei n, GLuint* out)
{
    FGL_CTX_OR_RETURN(c);
    for (GLsizei i = 0; i < n; i++) {
        while (fgl_fbo_get(c, g_next_fbo, 0)) g_next_fbo++;
        out[i] = g_next_fbo++;
        fgl_fbo_get(c, out[i], 1);
    }
}

void APIENTRY glDeleteFramebuffers(GLsizei n, const GLuint* names)
{
    FGL_CTX_OR_RETURN(c);
    for (GLsizei i = 0; i < n; i++) {
        fgl_fbo* f = names[i] ? fgl_fbo_get(c, names[i], 0) : NULL;
        if (!f) continue;
        if (c->draw_fbo == names[i]) c->draw_fbo = 0, c->tgt_color = NULL;
        if (c->read_fbo == names[i]) c->read_fbo = 0;
        fgl_surface_gone(c, f->dummy);
        fm_surface_destroy(f->dummy);
        memset(f, 0, sizeof(*f));
    }
}

GLboolean APIENTRY glIsFramebuffer(GLuint name)
{
    fgl_ctx* c = fgl_cur();
    return c && name && fgl_fbo_get(c, name, 0) ? GL_TRUE : GL_FALSE;
}

void APIENTRY glBindFramebuffer(GLenum target, GLuint name)
{
    FGL_CTX_OR_RETURN(c);
    if (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER && target != GL_READ_FRAMEBUFFER) {
        fgl_error(GL_INVALID_ENUM);
        return;
    }
    if (name && !fgl_fbo_get(c, name, 1)) {
        fgl_error(GL_OUT_OF_MEMORY);
        return;
    }
    if (target != GL_READ_FRAMEBUFFER && c->draw_fbo != name) c->draw_fbo = name, c->tgt_color = NULL; /* rebind on the next draw */
    if (target != GL_DRAW_FRAMEBUFFER) c->read_fbo = name;
}

static fgl_fbo* fgl_target_fbo(fgl_ctx* c, GLenum target)
{
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) return fgl_fbo_get(c, c->draw_fbo, 0);
    if (target == GL_READ_FRAMEBUFFER) return fgl_fbo_get(c, c->read_fbo, 0);
    return NULL;
}

/* the attachment points an attachment enum names (depth + stencil for GL_DEPTH_STENCIL_ATTACHMENT) */
static int fgl_attach_points(fgl_fbo* f, GLenum a, fgl_attach** p)
{
    if (a >= GL_COLOR_ATTACHMENT0 && a < GL_COLOR_ATTACHMENT0 + FGL_COLOR_ATTACHMENTS) {
        p[0] = &f->color[a - GL_COLOR_ATTACHMENT0];
        return 1;
    }
    if (a == GL_DEPTH_ATTACHMENT) return p[0] = &f->depth, 1;
    if (a == GL_STENCIL_ATTACHMENT) return p[0] = &f->stencil, 1;
    if (a == GL_DEPTH_STENCIL_ATTACHMENT) return p[0] = &f->depth, p[1] = &f->stencil, 2;
    return 0;
}

static void fgl_attach_to(fgl_ctx* c, GLenum target, GLenum attachment, GLenum type, GLuint name, GLint level)
{
    fgl_fbo* f = fgl_target_fbo(c, target);
    if (!f) {
        fgl_error(target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER ? GL_INVALID_OPERATION
                                                                                                         : GL_INVALID_ENUM);
        return;
    }
    fgl_attach* p[2];
    int         n = fgl_attach_points(f, attachment, p);
    if (!n) {
        fgl_error(GL_INVALID_ENUM);
        return;
    }
    if (type == GL_TEXTURE && name && !fgl_texture(c, name, 0)) {
        fgl_error(GL_INVALID_OPERATION);
        return;
    }
    if (type == GL_RENDERBUFFER && name && !fgl_rbo_get(c, name, 0)) {
        fgl_error(GL_INVALID_OPERATION);
        return;
    }
    if (type == GL_TEXTURE && level != 0 && name) fgl_unimplemented("glFramebufferTexture (levels other than 0)");
    for (int i = 0; i < n; i++) {
        p[i]->type  = name ? type : GL_NONE;
        p[i]->name  = name;
        p[i]->level = level;
    }
    if (f->name == c->draw_fbo) c->tgt_color = NULL;
}

void APIENTRY glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint tex, GLint level)
{
    FGL_CTX_OR_RETURN(c);
    if (tex && textarget != GL_TEXTURE_2D) {
        fgl_unimplemented("glFramebufferTexture2D (targets other than GL_TEXTURE_2D)");
        return;
    }
    fgl_attach_to(c, target, attachment, GL_TEXTURE, tex, level);
}

void APIENTRY glFramebufferTexture(GLenum target, GLenum attachment, GLuint tex, GLint level)
{
    FGL_CTX_OR_RETURN(c);
    fgl_attach_to(c, target, attachment, GL_TEXTURE, tex, level);
}

void APIENTRY glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum rbtarget, GLuint rb)
{
    FGL_CTX_OR_RETURN(c);
    if (rbtarget != GL_RENDERBUFFER) {
        fgl_error(GL_INVALID_ENUM);
        return;
    }
    fgl_attach_to(c, target, attachment, GL_RENDERBUFFER, rb, 0);
}

static void fgl_detach_all(fgl_ctx* c, GLenum type, GLuint name)
{
    for (int i = 0; i < c->nfbos; i++) {
        fgl_fbo* f = &c->fbos[i];
        if (!f->used) continue;
        fgl_attach* all[FGL_COLOR_ATTACHMENTS + 2];
        for (int k = 0; k < FGL_COLOR_ATTACHMENTS; k++) all[k] = &f->color[k];
        all[FGL_COLOR_ATTACHMENTS] = &f->depth, all[FGL_COLOR_ATTACHMENTS + 1] = &f->stencil;
        for (int k = 0; k < FGL_COLOR_ATTACHMENTS + 2; k++)
            if (all[k]->type == type && all[k]->name == name) memset(all[k], 0, sizeof(*all[k]));
    }
}

/* textures call this when deleted (gl_texture.c has no access to the framebuffers) */
void fgl_texture_detach(fgl_ctx* c, GLuint name) { fgl_detach_all(c, GL_TEXTURE, name); }

/* the image of an attachment; tex receives the texture (to mark it rendered) */
static fm_surface* fgl_attach_surface(fgl_ctx* c, const fgl_attach* a, int depth, fgl_tex** tex)
{
    if (tex) *tex = NULL;
    if (a->type == GL_RENDERBUFFER) {
        fgl_rbo* r = fgl_rbo_get(c, a->name, 0);
        return r ? r->s : NULL;
    }
    if (a->type == GL_TEXTURE) {
        fgl_tex* t = fgl_texture(c, a->name, 0);
        if (!t || a->level != 0) return NULL;
        if (tex) *tex = t;
        return depth ? t->depth : t->level0;
    }
    return NULL;
}

GLenum APIENTRY glCheckFramebufferStatus(GLenum target)
{
    fgl_ctx* c = fgl_cur();
    if (!c) return 0;
    if (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER && target != GL_READ_FRAMEBUFFER) {
        fgl_error(GL_INVALID_ENUM);
        return 0;
    }
    fgl_fbo* f = fgl_target_fbo(c, target);
    if (!f) return GL_FRAMEBUFFER_COMPLETE; /* the window */
    int any = 0;
    for (int k = 0; k < FGL_COLOR_ATTACHMENTS; k++) {
        if (f->color[k].type == GL_NONE) continue;
        fm_surface* s = fgl_attach_surface(c, &f->color[k], 0, NULL);
        if (!s || s->format != FM_FORMAT_ARGB32) return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        any = 1;
    }
    if (f->depth.type != GL_NONE) {
        fm_surface* s = fgl_attach_surface(c, &f->depth, 1, NULL);
        if (!s || !fm_format_is_depth(s->format)) return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        any = 1;
    }
    if (f->stencil.type != GL_NONE) {
        fm_surface* s = fgl_attach_surface(c, &f->stencil, 1, NULL);
        if (!s || (s->format != FM_FORMAT_D24S8 && s->format != FM_FORMAT_A8)) return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        any = 1;
    }
    return any ? GL_FRAMEBUFFER_COMPLETE : GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
}

void APIENTRY glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum p, GLint* v)
{
    FGL_CTX_OR_RETURN(c);
    fgl_fbo* f = fgl_target_fbo(c, target);
    if (!f) { /* the window: GL_BACK / GL_FRONT / GL_DEPTH / GL_STENCIL */
        int dep = attachment == GL_DEPTH || attachment == GL_STENCIL;
        switch (p) {
        case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE: *v = GL_FRAMEBUFFER_DEFAULT; break;
        case GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE: case GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE:
        case GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE: case GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE: *v = dep ? 0 : 8; break;
        case GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE: *v = attachment == GL_DEPTH ? 24 : 0; break;
        case GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE: *v = attachment == GL_STENCIL ? 8 : 0; break;
        case GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE: *v = GL_UNSIGNED_NORMALIZED; break;
        case GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING: *v = GL_LINEAR; break;
        default: fgl_error(GL_INVALID_ENUM); break;
        }
        return;
    }
    fgl_attach* pt[2];
    if (!fgl_attach_points(f, attachment, pt)) {
        fgl_error(GL_INVALID_ENUM);
        return;
    }
    const fgl_attach* a = pt[0];
    fm_surface*       s = fgl_attach_surface(c, a, a == &f->depth || a == &f->stencil, NULL);
    int               dep = s && s->format != FM_FORMAT_ARGB32;
    switch (p) {
    case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE: *v = (GLint)a->type; break;
    case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME: *v = (GLint)a->name; break;
    case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL: *v = a->level; break;
    case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE: case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER:
    case GL_FRAMEBUFFER_ATTACHMENT_LAYERED: *v = 0; break;
    case GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE: case GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE:
    case GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE: case GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE: *v = s && !dep ? 8 : 0; break;
    case GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE:
        *v = !dep || s->format == FM_FORMAT_A8 ? 0 : (s->format == FM_FORMAT_D16 ? 16 : (s->format == FM_FORMAT_D32F ? 32 : 24));
        break;
    case GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE: *v = s && (s->format == FM_FORMAT_D24S8 || s->format == FM_FORMAT_A8) ? 8 : 0; break;
    case GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE: *v = s && s->format == FM_FORMAT_D32F ? GL_FLOAT : GL_UNSIGNED_NORMALIZED; break;
    case GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING: *v = GL_LINEAR; break;
    default: fgl_error(GL_INVALID_ENUM); break;
    }
}

void APIENTRY glDrawBuffers(GLsizei n, const GLenum* bufs)
{
    FGL_CTX_OR_RETURN(c);
    fgl_fbo* f = fgl_fbo_get(c, c->draw_fbo, 0);
    if (n < 0 || n > FGL_COLOR_ATTACHMENTS) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    if (!f) return; /* the window: GL_BACK_LEFT / GL_NONE */
    for (int i = 0; i < FGL_COLOR_ATTACHMENTS; i++) f->draw_buf[i] = i < n ? bufs[i] : GL_NONE;
    for (int i = 1; i < n; i++)
        if (bufs[i] != GL_NONE) fgl_unimplemented("glDrawBuffers (more than one color output)");
    c->tgt_color = NULL;
}

void APIENTRY glDrawBuffer(GLenum b)
{
    FGL_CTX_OR_RETURN(c);
    fgl_fbo* f = fgl_fbo_get(c, c->draw_fbo, 0);
    if (!f) return;
    for (int i = 0; i < FGL_COLOR_ATTACHMENTS; i++) f->draw_buf[i] = GL_NONE;
    f->draw_buf[0] = b;
    c->tgt_color   = NULL;
}

void APIENTRY glReadBuffer(GLenum b)
{
    FGL_CTX_OR_RETURN(c);
    fgl_fbo* f = fgl_fbo_get(c, c->read_fbo, 0);
    if (f) f->read_buf = b;
}

/* ---- binding to fatmap ---- */
void fgl_surface_gone(fgl_ctx* c, const fm_surface* s)
{
    if (!s || !c->c3) return;
    if (s == c->tgt_color || s == c->tgt_depth || s == c->tgt_stencil) {
        fm3d_set_target(c->c3, c->color, c->depth); /* the window (flushes); rebound on the next draw */
        fm3d_set_stencil_buffer(c->c3, NULL);
        c->tgt_color = NULL;
    }
}

/* the color attachment drawn into: draw buffer 0 */
static const fgl_attach* fgl_draw_attach(const fgl_fbo* f)
{
    GLenum b = f->draw_buf[0];
    if (b >= GL_COLOR_ATTACHMENT0 && b < GL_COLOR_ATTACHMENT0 + FGL_COLOR_ATTACHMENTS) return &f->color[b - GL_COLOR_ATTACHMENT0];
    return NULL;
}

void fgl_bind_draw(fgl_ctx* c)
{
    fgl_fbo* f = fgl_fbo_get(c, c->draw_fbo, 0);
    fm_surface *col = c->color, *dep = c->depth, *sten = NULL;
    int         write_color = 1;
    if (f) {
        fgl_tex *           tc = NULL, *td = NULL, *ts = NULL;
        const fgl_attach*   ca = fgl_draw_attach(f);
        col                    = ca ? fgl_attach_surface(c, ca, 0, &tc) : NULL;
        dep                    = fgl_attach_surface(c, &f->depth, 1, &td);
        fm_surface* st         = fgl_attach_surface(c, &f->stencil, 1, &ts);
        if (col && col->format != FM_FORMAT_ARGB32) col = NULL;
        if (dep && !fm_format_is_depth(dep->format)) dep = NULL;
        if (st && st->format == FM_FORMAT_A8) sten = st;
        else if (st && st->format == FM_FORMAT_D24S8 && st != dep) dep = dep ? dep : st; /* a packed stencil only attachment */
        if (tc) tc->rendered = 1;
        if (td) td->rendered = 1;
        if (!col) { /* depth / stencil only: a dummy color target of the size, color writes off */
            int w = dep ? dep->width : (sten ? sten->width : 1), h = dep ? dep->height : (sten ? sten->height : 1);
            if (!f->dummy || f->dummy->width != w || f->dummy->height != h) {
                fgl_surface_gone(c, f->dummy);
                fm_surface_destroy(f->dummy);
                f->dummy = fm_surface_create(w, h, FM_FORMAT_ARGB32);
            }
            col         = f->dummy;
            write_color = 0;
        }
        if (dep && col && (dep->width < col->width || dep->height < col->height)) dep = NULL; /* fatmap: depth covers the color target */
    }
    if (col != c->tgt_color || dep != c->tgt_depth || sten != c->tgt_stencil) {
        fm3d_set_target(c->c3, col, dep);
        fm3d_set_stencil_buffer(c->c3, sten);
        c->tgt_color = col, c->tgt_depth = dep, c->tgt_stencil = sten;
    }
    if (!write_color) fm3d_set_color_write(c->c3, 0);
}

fm_surface* fgl_read_color(fgl_ctx* c)
{
    fgl_fbo* f = fgl_fbo_get(c, c->read_fbo, 0);
    if (!f) return c->color;
    GLenum b = f->read_buf;
    if (b < GL_COLOR_ATTACHMENT0 || b >= GL_COLOR_ATTACHMENT0 + FGL_COLOR_ATTACHMENTS) return NULL;
    fm_surface* s = fgl_attach_surface(c, &f->color[b - GL_COLOR_ATTACHMENT0], 0, NULL);
    return s && s->format == FM_FORMAT_ARGB32 ? s : NULL;
}

static fm_surface* fgl_fbo_depth(fgl_ctx* c, GLuint name);
fm_surface*        fgl_read_depth(fgl_ctx* c) { return fgl_fbo_depth(c, c->read_fbo); }

static fm_surface* fgl_fbo_depth(fgl_ctx* c, GLuint name)
{
    fgl_fbo* f = fgl_fbo_get(c, name, 0);
    if (!f) return c->depth;
    fm_surface* s = fgl_attach_surface(c, &f->depth, 1, NULL);
    return s && fm_format_is_depth(s->format) ? s : NULL;
}

float fgl_depth_at(const fm_surface* s, int x, int y)
{
    switch (s->format) {
    case FM_FORMAT_D32F: return fm_surface_rowf(s, y)[x];
    case FM_FORMAT_D16: return (float)((const uint16_t*)fm_surface_row8(s, y))[x] * (1.0f / 65535.0f);
    case FM_FORMAT_D24S8: return (float)(fm_surface_row32(s, y)[x] & 0xFFFFFFu) * (1.0f / 16777215.0f);
    default: return 1.0f;
    }
}

/* ---- blits (nearest, or bilinear for color with GL_LINEAR) ---- */
void APIENTRY glBlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1,
                                GLbitfield mask, GLenum filter)
{
    FGL_CTX_OR_RETURN(c);
    if ((mask & ~(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) ||
        ((mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) && filter != GL_NEAREST)) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    fgl_bind_draw(c);
    fgl_flush(c);
    int dw = dx1 - dx0, dh = dy1 - dy0, sw = sx1 - sx0, sh = sy1 - sy0;
    if (!dw || !dh) return;
    struct {
        fm_surface *src, *dst;
        int         depth;
    } job[2] = { { NULL, NULL, 0 }, { NULL, NULL, 1 } };
    if (mask & GL_COLOR_BUFFER_BIT) job[0].src = fgl_read_color(c), job[0].dst = c->tgt_color;
    if (mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) job[1].src = fgl_fbo_depth(c, c->read_fbo), job[1].dst = c->tgt_depth;
    fgl_fbo* df = fgl_fbo_get(c, c->draw_fbo, 0);
    if (df && job[0].dst == df->dummy) job[0].dst = NULL;
    for (int j = 0; j < 2; j++) {
        fm_surface *src = job[j].src, *dst = job[j].dst;
        if (!src || !dst || src == dst) continue;
        if (job[j].depth && src->format != dst->format) continue; /* GL: an error; nothing to copy */
        int bpp = fm_format_bpp(dst->format);
        int y0 = dy0 < dy1 ? dy0 : dy1, y1 = dy0 < dy1 ? dy1 : dy0, x0 = dx0 < dx1 ? dx0 : dx1, x1 = dx0 < dx1 ? dx1 : dx0;
        if (c->enables & FGL_E_SCISSOR) {
            x0 = x0 > c->scissor[0] ? x0 : c->scissor[0], y0 = y0 > c->scissor[1] ? y0 : c->scissor[1];
            x1 = x1 < c->scissor[0] + c->scissor[2] ? x1 : c->scissor[0] + c->scissor[2];
            y1 = y1 < c->scissor[1] + c->scissor[3] ? y1 : c->scissor[1] + c->scissor[3];
        }
        x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0;
        x1 = x1 > dst->width ? dst->width : x1, y1 = y1 > dst->height ? dst->height : y1;
        int lin = !job[j].depth && filter == GL_LINEAR && (sw != dw || sh != dh);
        for (int y = y0; y < y1; y++) {
            float    fy = (float)sy0 + ((float)(y - dy0) + 0.5f) * (float)sh / (float)dh; /* source y of the pixel center */
            uint8_t* d  = fm_surface_row8(dst, y);
            for (int x = x0; x < x1; x++) {
                float fx = (float)sx0 + ((float)(x - dx0) + 0.5f) * (float)sw / (float)dw;
                if (lin) {
                    float ax = fx - 0.5f, ay = fy - 0.5f;
                    int   ix = (int)(ax < 0 ? ax - 1 : ax), iy = (int)(ay < 0 ? ay - 1 : ay);
                    float tx = ax - (float)ix, ty = ay - (float)iy;
                    uint32_t q[4];
                    for (int k = 0; k < 4; k++) {
                        int qx = ix + (k & 1), qy = iy + (k >> 1);
                        qx = qx < 0 ? 0 : (qx >= src->width ? src->width - 1 : qx);
                        qy = qy < 0 ? 0 : (qy >= src->height ? src->height - 1 : qy);
                        q[k] = fm_surface_row32(src, qy)[qx];
                    }
                    uint32_t o = 0;
                    for (int sh8 = 0; sh8 < 32; sh8 += 8) {
                        float a = (float)((q[0] >> sh8) & 255) * (1 - tx) + (float)((q[1] >> sh8) & 255) * tx;
                        float b = (float)((q[2] >> sh8) & 255) * (1 - tx) + (float)((q[3] >> sh8) & 255) * tx;
                        o |= (uint32_t)(a * (1 - ty) + b * ty + 0.5f) << sh8;
                    }
                    ((uint32_t*)d)[x] = o;
                    continue;
                }
                int ix = (int)(fx < 0 ? fx - 1 : fx), iy = (int)(fy < 0 ? fy - 1 : fy);
                if (ix < 0 || iy < 0 || ix >= src->width || iy >= src->height) continue;
                memcpy(d + (size_t)x * (size_t)bpp, fm_surface_row8(src, iy) + (size_t)ix * (size_t)bpp, (size_t)bpp);
            }
        }
    }
    if (df) { /* textures drawn into */
        fgl_tex* t;
        const fgl_attach* ca = fgl_draw_attach(df);
        if (ca && fgl_attach_surface(c, ca, 0, &t) && t) t->rendered = 1;
        if (fgl_attach_surface(c, &df->depth, 1, &t) && t) t->rendered = 1;
    }
}

void fgl_fbo_free(fgl_ctx* c)
{
    for (int i = 0; i < c->nrbos; i++) fm_surface_destroy(c->rbos[i].s);
    for (int i = 0; i < c->nfbos; i++) fm_surface_destroy(c->fbos[i].dummy);
    free(c->rbos);
    free(c->fbos);
    c->rbos = NULL, c->fbos = NULL, c->nrbos = c->nfbos = 0;
}
