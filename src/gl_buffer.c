/*
 * fatgl - buffer objects, vertex array objects, generic vertex attributes.
 */
#include "fgl.h"

/* ---- buffers ---- */
fgl_buf* fgl_buffer(fgl_ctx* c, GLuint name)
{
    if (!name) return NULL;
    for (int i = 0; i < c->nbufs; i++)
        if (c->bufs[i].used && c->bufs[i].name == name) return &c->bufs[i];
    return NULL;
}

static fgl_buf* fgl_buffer_new(fgl_ctx* c, GLuint name)
{
    int slot = -1;
    for (int i = 0; i < c->nbufs && slot < 0; i++)
        if (!c->bufs[i].used) slot = i;
    if (slot < 0) {
        fgl_buf* n = (fgl_buf*)realloc(c->bufs, ((size_t)c->nbufs + 32) * sizeof(fgl_buf));
        if (!n) return NULL;
        memset(n + c->nbufs, 0, 32 * sizeof(fgl_buf));
        c->bufs = n, slot = c->nbufs, c->nbufs += 32;
    }
    fgl_buf* b = &c->bufs[slot];
    memset(b, 0, sizeof(*b));
    b->used = 1, b->name = name;
    return b;
}

static GLuint g_next_buf = 1;

void APIENTRY glGenBuffers(GLsizei n, GLuint* out)
{
    FGL_CTX_OR_RETURN(c);
    for (GLsizei i = 0; i < n; i++) {
        while (fgl_buffer(c, g_next_buf)) g_next_buf++;
        out[i] = g_next_buf++;
        fgl_buffer_new(c, out[i]);
    }
}

void APIENTRY glDeleteBuffers(GLsizei n, const GLuint* names)
{
    FGL_CTX_OR_RETURN(c);
    fgl_flush(c);
    for (GLsizei i = 0; i < n; i++) {
        fgl_buf* b = fgl_buffer(c, names[i]);
        if (!b) continue;
        free(b->data);
        memset(b, 0, sizeof(*b));
        GLuint nm = names[i];
        if (c->array_buffer == nm) c->array_buffer = 0;
        if (c->uniform_buffer == nm) c->uniform_buffer = 0;
        fgl_vao* v = fgl_cur_vao(c);
        if (v->elements == nm) v->elements = 0;
        for (int a = 0; a < FGL_ATTRIBS; a++)
            if (v->a[a].buffer == nm) v->a[a].buffer = 0;
        for (int u = 0; u < FGL_UBO_BINDS; u++)
            if (c->ubo[u].buffer == nm) c->ubo[u].buffer = 0;
    }
}

GLboolean APIENTRY glIsBuffer(GLuint name)
{
    fgl_ctx* c = fgl_cur();
    return c && fgl_buffer(c, name) ? GL_TRUE : GL_FALSE;
}

static GLuint* fgl_target(fgl_ctx* c, GLenum target)
{
    switch (target) {
    case GL_ARRAY_BUFFER: return &c->array_buffer;
    case GL_ELEMENT_ARRAY_BUFFER: return &fgl_cur_vao(c)->elements;
    case GL_UNIFORM_BUFFER: return &c->uniform_buffer;
    case GL_COPY_READ_BUFFER: return &c->copy_read;
    case GL_COPY_WRITE_BUFFER: return &c->copy_write;
    case GL_PIXEL_UNPACK_BUFFER: return &c->unpack_buffer;
    case GL_PIXEL_PACK_BUFFER: return &c->pack_buffer;
    default: return NULL;
    }
}

void APIENTRY glBindBuffer(GLenum target, GLuint name)
{
    FGL_CTX_OR_RETURN(c);
    GLuint* t = fgl_target(c, target);
    if (!t) {
        fgl_error(GL_INVALID_ENUM);
        return;
    }
    if (name && !fgl_buffer(c, name)) fgl_buffer_new(c, name); /* GL 2: binding creates */
    *t = name;
}

static fgl_buf* fgl_bound(fgl_ctx* c, GLenum target)
{
    GLuint* t = fgl_target(c, target);
    if (!t) {
        fgl_error(GL_INVALID_ENUM);
        return NULL;
    }
    fgl_buf* b = fgl_buffer(c, *t);
    if (!b) fgl_error(GL_INVALID_OPERATION);
    return b;
}

void APIENTRY glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage)
{
    FGL_CTX_OR_RETURN(c);
    if (size < 0) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    fgl_buf* b = fgl_bound(c, target);
    if (!b) return;
    fgl_flush(c); /* deferred draws were recorded with copies, but be safe */
    uint8_t* n = (uint8_t*)malloc(size ? (size_t)size : 1);
    if (!n) {
        fgl_error(GL_OUT_OF_MEMORY);
        return;
    }
    if (data && size) memcpy(n, data, (size_t)size);
    else memset(n, 0, size ? (size_t)size : 1);
    free(b->data);
    b->data = n, b->size = size, b->usage = usage;
}

void APIENTRY glBufferSubData(GLenum target, GLintptr off, GLsizeiptr size, const void* data)
{
    FGL_CTX_OR_RETURN(c);
    fgl_buf* b = fgl_bound(c, target);
    if (!b) return;
    if (off < 0 || size < 0 || off + size > b->size) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    if (data && size) memcpy(b->data + off, data, (size_t)size);
}

void APIENTRY glGetBufferSubData(GLenum target, GLintptr off, GLsizeiptr size, void* data)
{
    FGL_CTX_OR_RETURN(c);
    fgl_buf* b = fgl_bound(c, target);
    if (!b) return;
    if (off < 0 || size < 0 || off + size > b->size) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    memcpy(data, b->data + off, (size_t)size);
}

void* APIENTRY glMapBufferRange(GLenum target, GLintptr off, GLsizeiptr len, GLbitfield access)
{
    fgl_ctx* c = fgl_cur();
    if (!c) return NULL;
    fgl_buf* b = fgl_bound(c, target);
    if (!b) return NULL;
    if (off < 0 || len < 0 || off + len > b->size || b->mapped) {
        fgl_error(b->mapped ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
        return NULL;
    }
    (void)access;
    b->mapped = 1;
    return b->data + off;
}

void* APIENTRY glMapBuffer(GLenum target, GLenum access)
{
    fgl_ctx* c = fgl_cur();
    if (!c) return NULL;
    fgl_buf* b = fgl_bound(c, target);
    return b ? glMapBufferRange(target, 0, b->size, access) : NULL;
}

GLboolean APIENTRY glUnmapBuffer(GLenum target)
{
    fgl_ctx* c = fgl_cur();
    if (!c) return GL_FALSE;
    fgl_buf* b = fgl_bound(c, target);
    if (!b || !b->mapped) {
        fgl_error(GL_INVALID_OPERATION);
        return GL_FALSE;
    }
    b->mapped = 0;
    return GL_TRUE;
}

void APIENTRY glFlushMappedBufferRange(GLenum target, GLintptr off, GLsizeiptr len) { (void)target, (void)off, (void)len; }

void APIENTRY glCopyBufferSubData(GLenum rt, GLenum wt, GLintptr ro, GLintptr wo, GLsizeiptr size)
{
    FGL_CTX_OR_RETURN(c);
    fgl_buf* r = fgl_bound(c, rt);
    fgl_buf* w = fgl_bound(c, wt);
    if (!r || !w) return;
    if (ro < 0 || wo < 0 || size < 0 || ro + size > r->size || wo + size > w->size) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    memmove(w->data + wo, r->data + ro, (size_t)size);
}

void APIENTRY glGetBufferParameteriv(GLenum target, GLenum p, GLint* v)
{
    FGL_CTX_OR_RETURN(c);
    fgl_buf* b = fgl_bound(c, target);
    if (!b) return;
    switch (p) {
    case GL_BUFFER_SIZE: *v = (GLint)b->size; break;
    case GL_BUFFER_USAGE: *v = (GLint)b->usage; break;
    case GL_BUFFER_MAPPED: *v = b->mapped; break;
    case GL_BUFFER_ACCESS: *v = GL_READ_WRITE; break;
    default: fgl_error(GL_INVALID_ENUM); break;
    }
}

/* indexed uniform buffer bindings */
void APIENTRY glBindBufferRange(GLenum target, GLuint index, GLuint name, GLintptr off, GLsizeiptr size)
{
    FGL_CTX_OR_RETURN(c);
    if (target != GL_UNIFORM_BUFFER) {
        fgl_unimplemented("glBindBufferRange (targets other than GL_UNIFORM_BUFFER)");
        return;
    }
    if (index >= FGL_UBO_BINDS) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    if (name && !fgl_buffer(c, name)) fgl_buffer_new(c, name);
    c->ubo[index].buffer = name, c->ubo[index].offset = off, c->ubo[index].size = size;
    c->uniform_buffer = name;
}

void APIENTRY glBindBufferBase(GLenum target, GLuint index, GLuint name) { glBindBufferRange(target, index, name, 0, 0); }

/* ---- vertex arrays ---- */
fgl_vao* fgl_cur_vao(fgl_ctx* c)
{
    if (c->vao)
        for (int i = 0; i < c->nvaos; i++)
            if (c->vaos[i].used && c->vaos[i].name == c->vao) return &c->vaos[i];
    return &c->vao0;
}

static fgl_vao* fgl_vao_find(fgl_ctx* c, GLuint name)
{
    for (int i = 0; i < c->nvaos; i++)
        if (c->vaos[i].used && c->vaos[i].name == name) return &c->vaos[i];
    return NULL;
}

static GLuint g_next_vao = 1;

void APIENTRY glGenVertexArrays(GLsizei n, GLuint* out)
{
    FGL_CTX_OR_RETURN(c);
    for (GLsizei i = 0; i < n; i++) {
        while (fgl_vao_find(c, g_next_vao)) g_next_vao++;
        int slot = -1;
        for (int k = 0; k < c->nvaos && slot < 0; k++)
            if (!c->vaos[k].used) slot = k;
        if (slot < 0) {
            fgl_vao* nv = (fgl_vao*)realloc(c->vaos, ((size_t)c->nvaos + 16) * sizeof(fgl_vao));
            if (!nv) {
                fgl_error(GL_OUT_OF_MEMORY);
                return;
            }
            memset(nv + c->nvaos, 0, 16 * sizeof(fgl_vao));
            c->vaos = nv, slot = c->nvaos, c->nvaos += 16;
        }
        memset(&c->vaos[slot], 0, sizeof(fgl_vao));
        c->vaos[slot].used = 1, c->vaos[slot].name = g_next_vao;
        out[i] = g_next_vao++;
    }
}

void APIENTRY glDeleteVertexArrays(GLsizei n, const GLuint* names)
{
    FGL_CTX_OR_RETURN(c);
    for (GLsizei i = 0; i < n; i++) {
        fgl_vao* v = names[i] ? fgl_vao_find(c, names[i]) : NULL;
        if (!v) continue;
        if (c->vao == names[i]) c->vao = 0;
        memset(v, 0, sizeof(*v));
    }
}

void APIENTRY glBindVertexArray(GLuint name)
{
    FGL_CTX_OR_RETURN(c);
    if (name && !fgl_vao_find(c, name)) {
        fgl_error(GL_INVALID_OPERATION);
        return;
    }
    c->vao = name;
}

GLboolean APIENTRY glIsVertexArray(GLuint name)
{
    fgl_ctx* c = fgl_cur();
    return c && name && fgl_vao_find(c, name) ? GL_TRUE : GL_FALSE;
}

static fgl_attrib* fgl_attr(fgl_ctx* c, GLuint index)
{
    if (index >= FGL_ATTRIBS) {
        fgl_error(GL_INVALID_VALUE);
        return NULL;
    }
    return &fgl_cur_vao(c)->a[index];
}

void APIENTRY glEnableVertexAttribArray(GLuint index)
{
    FGL_CTX_OR_RETURN(c);
    fgl_attrib* a = fgl_attr(c, index);
    if (a) a->enabled = 1;
}

void APIENTRY glDisableVertexAttribArray(GLuint index)
{
    FGL_CTX_OR_RETURN(c);
    fgl_attrib* a = fgl_attr(c, index);
    if (a) a->enabled = 0;
}

static void fgl_attr_pointer(GLuint index, GLint size, GLenum type, int normalized, int integer, GLsizei stride, const void* p)
{
    FGL_CTX_OR_RETURN(c);
    fgl_attrib* a = fgl_attr(c, index);
    if (!a) return;
    if ((size < 1 || size > 4) && size != GL_BGRA) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    a->size = size == GL_BGRA ? 4 : size, a->type = type, a->normalized = normalized, a->integer = integer;
    a->stride = stride, a->offset = (GLintptr)p, a->buffer = c->array_buffer;
}

void APIENTRY glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* p)
{
    fgl_attr_pointer(index, size, type, normalized != GL_FALSE, 0, stride, p);
}

void APIENTRY glVertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const void* p)
{
    fgl_attr_pointer(index, size, type, 0, 1, stride, p);
}

void APIENTRY glVertexAttribDivisor(GLuint index, GLuint divisor)
{
    FGL_CTX_OR_RETURN(c);
    fgl_attrib* a = fgl_attr(c, index);
    if (a) a->divisor = divisor;
}

void APIENTRY glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    FGL_CTX_OR_RETURN(c);
    if (index >= FGL_ATTRIBS) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    c->attr_value[index][0] = x, c->attr_value[index][1] = y, c->attr_value[index][2] = z, c->attr_value[index][3] = w;
}
void APIENTRY glVertexAttrib1f(GLuint i, GLfloat x) { glVertexAttrib4f(i, x, 0, 0, 1); }
void APIENTRY glVertexAttrib2f(GLuint i, GLfloat x, GLfloat y) { glVertexAttrib4f(i, x, y, 0, 1); }
void APIENTRY glVertexAttrib3f(GLuint i, GLfloat x, GLfloat y, GLfloat z) { glVertexAttrib4f(i, x, y, z, 1); }
void APIENTRY glVertexAttrib4fv(GLuint i, const GLfloat* v) { glVertexAttrib4f(i, v[0], v[1], v[2], v[3]); }
void APIENTRY glVertexAttrib3fv(GLuint i, const GLfloat* v) { glVertexAttrib4f(i, v[0], v[1], v[2], 1); }
void APIENTRY glVertexAttrib2fv(GLuint i, const GLfloat* v) { glVertexAttrib4f(i, v[0], v[1], 0, 1); }
void APIENTRY glVertexAttrib1fv(GLuint i, const GLfloat* v) { glVertexAttrib4f(i, v[0], 0, 0, 1); }

void APIENTRY glGetVertexAttribiv(GLuint index, GLenum p, GLint* v)
{
    FGL_CTX_OR_RETURN(c);
    fgl_attrib* a = fgl_attr(c, index);
    if (!a) return;
    switch (p) {
    case GL_VERTEX_ATTRIB_ARRAY_ENABLED: *v = a->enabled; break;
    case GL_VERTEX_ATTRIB_ARRAY_SIZE: *v = a->size ? a->size : 4; break;
    case GL_VERTEX_ATTRIB_ARRAY_STRIDE: *v = a->stride; break;
    case GL_VERTEX_ATTRIB_ARRAY_TYPE: *v = a->type ? (GLint)a->type : GL_FLOAT; break;
    case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED: *v = a->normalized; break;
    case GL_VERTEX_ATTRIB_ARRAY_INTEGER: *v = a->integer; break;
    case GL_VERTEX_ATTRIB_ARRAY_DIVISOR: *v = (GLint)a->divisor; break;
    case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING: *v = (GLint)a->buffer; break;
    default: fgl_error(GL_INVALID_ENUM); break;
    }
}

void APIENTRY glGetVertexAttribPointerv(GLuint index, GLenum p, void** v)
{
    FGL_CTX_OR_RETURN(c);
    fgl_attrib* a = fgl_attr(c, index);
    if (a && p == GL_VERTEX_ATTRIB_ARRAY_POINTER) *v = (void*)a->offset;
}
