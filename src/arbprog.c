/*
 * fatgl - ARB_vertex_program / ARB_fragment_program (the GL 1.x assembly
 * shading language of 2002: Doom 3, Quake 4, many games of 2003 .. 2006).
 *
 * Programs are parsed and translated to GLSL 3.30; the vertex and fragment
 * program enabled at a draw are compiled together as one GLSL program
 * (cached per pair), so they run on the same path as every other shader.
 * A stage without an ARB program gets a fixed function stand in, written
 * as an ARB program too.
 *
 *   temporaries / addresses    GLSL locals
 *   PARAM, program.env/local,  slots of a per stage uniform block (fgl_PV /
 *   state.* bindings           fgl_PF) that fatgl fills before each draw
 *   vertex.attrib[n] & co.     inputs fgl_va<n> at location n (NVIDIA's
 *                              aliasing: position 0, normal 2, color 3,
 *                              secondary color 4, fog 5, texcoord[n] 8 + n)
 *   result.* / fragment.*      varyings at fixed locations (colors 0 / 1,
 *                              texcoord[n] 2 + n, fog 10)
 *   texture[n], TARGET         uniform samplerXX fgl_s<n>_<target> on unit n
 */
#include "fgl.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>

#define ARB_MAX_ENV   96
#define ARB_MAX_LOCAL 96
#define ARB_MAX_SLOTS 256
#define ARB_MAX_NAMES 512

enum { SLOT_CONST, SLOT_ENV, SLOT_LOCAL, SLOT_STATE };
/* state bindings */
enum {
    ST_MATRIX, ST_LIGHT, ST_LIGHTMODEL_AMBIENT, ST_LIGHTMODEL_SCENE, ST_LIGHTPROD, ST_MATERIAL, ST_FOG_COLOR, ST_FOG_PARAMS,
    ST_TEXENV_COLOR, ST_DEPTH_RANGE, ST_POINT_SIZE, ST_POINT_ATTEN, ST_CLIP, ST_TEXGEN, ST_ZERO
};
/* matrices */
enum { MAT_MODELVIEW, MAT_PROJECTION, MAT_MVP, MAT_TEXTURE, MAT_PALETTE, MAT_PROGRAM };

typedef struct arb_slot {
    int   kind, index;     /* SLOT_ENV / LOCAL: the parameter; SLOT_STATE: the binding */
    float v[4];            /* SLOT_CONST */
    int   st, a, b, row;   /* state: ST_*, matrix / light index, property / modifier, matrix row */
} arb_slot;

typedef struct arb_name { /* a declared name */
    char name[64];
    int  kind; /* 0 temp, 1 address, 2 param (slot base, count), 3 attrib (expression), 4 output (lvalue) */
    int  base, count;
    char expr[96]; /* attrib: expression; output: lvalue; temp / address: the GLSL name */
} arb_name;

typedef struct fgl_arbprog {
    GLuint   name;
    int      used;
    GLenum   target; /* GL_VERTEX_PROGRAM_ARB / GL_FRAGMENT_PROGRAM_ARB */
    int      valid, gen;
    char*    body;   /* translated statements of main() */
    char*    decl;   /* locals */
    arb_slot slots[ARB_MAX_SLOTS];
    int      nslots;
    unsigned attribs;     /* vp: generic inputs (fgl_va<n>) read */
    unsigned outs;        /* vp: varyings written (bit: 0 / 1 colors, 2 + n texcoords, 10 fog) */
    unsigned ins;         /* fp: varyings read */
    unsigned samplers[4]; /* fp: units by target (1D, 2D, 3D, CUBE / RECT in [3] bit 16 + n) */
    int      position_invariant, writes_position, writes_psize, writes_depth, fog_mode, reads_fragpos;
    float    local[ARB_MAX_LOCAL][4];
} fgl_arbprog;

/* ---- the translator ---- */
typedef struct arb_tr {
    fgl_arbprog* p;
    const char*  s;   /* the source */
    const char*  at;  /* the parse position */
    int          vp;
    char         err[256];
    arb_name     names[ARB_MAX_NAMES];
    int          nnames;
    char*        out;
    size_t       on, ocap;
    char*        dcl;
    size_t       dn, dcap;
} arb_tr;

static void tr_put(char** b, size_t* n, size_t* cap, const char* fmt, va_list ap)
{
    char tmp[2048];
    int  k = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    if (k < 0) return;
    if (*n + (size_t)k + 1 > *cap) {
        size_t c = *cap ? *cap * 2 : 4096;
        while (c < *n + (size_t)k + 1) c *= 2;
        char* q = (char*)realloc(*b, c);
        if (!q) return;
        *b = q, *cap = c;
    }
    memcpy(*b + *n, tmp, (size_t)k + 1);
    *n += (size_t)k;
}
static void tr_emit(arb_tr* t, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    tr_put(&t->out, &t->on, &t->ocap, fmt, ap);
    va_end(ap);
}
static void tr_decl(arb_tr* t, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    tr_put(&t->dcl, &t->dn, &t->dcap, fmt, ap);
    va_end(ap);
}
static int tr_fail(arb_tr* t, const char* fmt, ...)
{
    if (t->err[0]) return 0;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(t->err, sizeof(t->err), fmt, ap);
    va_end(ap);
    return 0;
}

/* tokens */
static void tr_ws(arb_tr* t)
{
    for (;;) {
        while (*t->at && isspace((unsigned char)*t->at)) t->at++;
        if (*t->at == '#') {
            while (*t->at && *t->at != '\n') t->at++;
            continue;
        }
        break;
    }
}
static int tr_ch(arb_tr* t, char ch) /* consume ch if next */
{
    tr_ws(t);
    if (*t->at != ch) return 0;
    t->at++;
    return 1;
}
static int tr_expect(arb_tr* t, char ch)
{
    if (tr_ch(t, ch)) return 1;
    return tr_fail(t, "expected '%c'", ch);
}
static int tr_ident(arb_tr* t, char* out, size_t n) /* letters, digits, _ */
{
    tr_ws(t);
    const char* b = t->at;
    if (!isalpha((unsigned char)*b) && *b != '_') return 0;
    while (isalnum((unsigned char)*t->at) || *t->at == '_') t->at++;
    size_t k = (size_t)(t->at - b);
    if (k >= n) k = n - 1;
    memcpy(out, b, k), out[k] = 0;
    return 1;
}
static int tr_peek_ident(arb_tr* t, char* out, size_t n)
{
    const char* save = t->at;
    int         ok   = tr_ident(t, out, n);
    t->at            = save;
    return ok;
}
static int tr_int(arb_tr* t, int* v)
{
    tr_ws(t);
    if (!isdigit((unsigned char)*t->at)) return 0;
    *v = (int)strtol(t->at, (char**)&t->at, 10);
    return 1;
}
static int tr_number(arb_tr* t, float* v) /* [-|+] float */
{
    tr_ws(t);
    const char* b   = t->at;
    float       sgn = 1;
    if (*b == '-' || *b == '+') {
        sgn = *b == '-' ? -1.0f : 1.0f;
        b++;
        while (isspace((unsigned char)*b)) b++;
    }
    if (!isdigit((unsigned char)*b) && !(*b == '.' && isdigit((unsigned char)b[1]))) return 0;
    char* e;
    *v    = sgn * (float)strtod(b, &e);
    t->at = e;
    return 1;
}
/* "word" follows (an identifier boundary) */
static int tr_word(arb_tr* t, const char* w)
{
    tr_ws(t);
    size_t n = strlen(w);
    if (strncmp(t->at, w, n) || isalnum((unsigned char)t->at[n]) || t->at[n] == '_') return 0;
    t->at += n;
    return 1;
}
/* ".word" follows */
static int tr_dotword(arb_tr* t, const char* w)
{
    const char* save = t->at;
    tr_ws(t);
    if (*t->at == '.') {
        t->at++;
        if (tr_word(t, w)) return 1;
    }
    t->at = save;
    return 0;
}
static int tr_index(arb_tr* t, int* v, int def) /* optional [n] */
{
    *v = def;
    if (!tr_ch(t, '[')) return 1;
    if (!tr_int(t, v)) return tr_fail(t, "expected an index");
    return tr_expect(t, ']');
}

static arb_name* tr_find(arb_tr* t, const char* n)
{
    for (int i = 0; i < t->nnames; i++)
        if (!strcmp(t->names[i].name, n)) return &t->names[i];
    return NULL;
}
static arb_name* tr_add(arb_tr* t, const char* n, int kind)
{
    if (tr_find(t, n)) {
        tr_fail(t, "%s declared twice", n);
        return NULL;
    }
    if (t->nnames == ARB_MAX_NAMES) {
        tr_fail(t, "too many names");
        return NULL;
    }
    arb_name* a = &t->names[t->nnames++];
    memset(a, 0, sizeof(*a));
    snprintf(a->name, sizeof(a->name), "%s", n);
    a->kind = kind;
    return a;
}

static int tr_slot(arb_tr* t, const arb_slot* s)
{
    fgl_arbprog* p = t->p;
    for (int i = 0; i < p->nslots; i++) /* bindings are shared */
        if (!memcmp(&p->slots[i], s, sizeof(*s))) return i;
    if (p->nslots == ARB_MAX_SLOTS) return tr_fail(t, "too many parameters"), 0;
    p->slots[p->nslots] = *s;
    return p->nslots++;
}
static int tr_slots_run(arb_tr* t, const arb_slot* s, int n) /* n consecutive new slots (arrays: no sharing) */
{
    fgl_arbprog* p = t->p;
    if (p->nslots + n > ARB_MAX_SLOTS) return tr_fail(t, "too many parameters"), 0;
    int base = p->nslots;
    for (int i = 0; i < n; i++) p->slots[p->nslots++] = s[i];
    return base;
}

/* ---- parameter bindings: constants, program.env / local, state ---- */
/* parses one binding at the cursor into up to 4 slots (a whole matrix: 4 rows); returns the count */
static int tr_param_binding(arb_tr* t, arb_slot* out, int max)
{
    memset(out, 0, sizeof(arb_slot) * (size_t)max);
    float v;
    if (tr_ch(t, '{')) { /* {x, y, z, w} */
        float f[4] = { 0, 0, 0, 1 };
        int   n    = 0;
        do {
            if (!tr_number(t, &v)) return tr_fail(t, "expected a number");
            if (n < 4) f[n++] = v;
        } while (tr_ch(t, ','));
        if (!tr_expect(t, '}')) return 0;
        if (n == 1) f[1] = f[2] = f[3] = f[0]; /* {x}: replicated (scalar constant) */
        out[0].kind = SLOT_CONST;
        memcpy(out[0].v, f, sizeof(f));
        return 1;
    }
    if (tr_number(t, &v)) { /* a scalar: replicated */
        out[0].kind = SLOT_CONST;
        out[0].v[0] = out[0].v[1] = out[0].v[2] = out[0].v[3] = v;
        return 1;
    }
    if (tr_word(t, "program")) {
        int env = 0;
        if (tr_dotword(t, "env")) env = 1;
        else if (!tr_dotword(t, "local")) return tr_fail(t, "program.env or program.local");
        if (!tr_expect(t, '[')) return 0;
        int a, b;
        if (!tr_int(t, &a)) return tr_fail(t, "expected an index");
        b = a;
        if (tr_ch(t, '.')) { /* a range a..b */
            if (!tr_expect(t, '.') || !tr_int(t, &b)) return tr_fail(t, "expected a range");
        }
        if (!tr_expect(t, ']')) return 0;
        int n = 0;
        for (int i = a; i <= b && n < max; i++) {
            out[n].kind  = env ? SLOT_ENV : SLOT_LOCAL;
            out[n].index = i;
            n++;
        }
        if ((env && b >= ARB_MAX_ENV) || (!env && b >= ARB_MAX_LOCAL)) return tr_fail(t, "parameter index out of range");
        return n;
    }
    if (!tr_word(t, "state")) return tr_fail(t, "expected a parameter binding");
    if (!tr_expect(t, '.')) return 0;
    arb_slot s;
    memset(&s, 0, sizeof(s));
    s.kind = SLOT_STATE;
    if (tr_word(t, "matrix")) {
        if (!tr_expect(t, '.')) return 0;
        s.st = ST_MATRIX;
        if (tr_word(t, "modelview")) s.a = MAT_MODELVIEW;
        else if (tr_word(t, "projection")) s.a = MAT_PROJECTION;
        else if (tr_word(t, "mvp")) s.a = MAT_MVP;
        else if (tr_word(t, "texture")) s.a = MAT_TEXTURE;
        else if (tr_word(t, "palette")) s.a = MAT_PALETTE;
        else if (tr_word(t, "program")) s.a = MAT_PROGRAM;
        else return tr_fail(t, "unknown matrix");
        int idx;
        if (!tr_index(t, &idx, 0)) return 0;
        s.index = idx;
        if (tr_dotword(t, "inverse")) s.b = 1;
        else if (tr_dotword(t, "transpose")) s.b = 2;
        else if (tr_dotword(t, "invtrans")) s.b = 3;
        int r0 = 0, r1 = 3;
        if (tr_dotword(t, "row")) {
            if (!tr_expect(t, '[') || !tr_int(t, &r0)) return tr_fail(t, "expected a row");
            r1 = r0;
            if (tr_ch(t, '.')) {
                if (!tr_expect(t, '.') || !tr_int(t, &r1)) return tr_fail(t, "expected a row range");
            }
            if (!tr_expect(t, ']')) return 0;
        }
        int n = 0;
        for (int r = r0; r <= r1 && r < 4 && n < max; r++) out[n] = s, out[n].row = r, n++;
        return n;
    }
    if (tr_word(t, "light")) {
        s.st = ST_LIGHT;
        if (!tr_expect(t, '[') || !tr_int(t, &s.a) || !tr_expect(t, ']') || !tr_expect(t, '.')) return 0;
        if (tr_word(t, "ambient")) s.b = 0;
        else if (tr_word(t, "diffuse")) s.b = 1;
        else if (tr_word(t, "specular")) s.b = 2;
        else if (tr_word(t, "position")) s.b = 3;
        else if (tr_word(t, "attenuation")) s.b = 4;
        else if (tr_word(t, "spot")) {
            if (!tr_dotword(t, "direction")) return tr_fail(t, "state.light[n].spot.direction");
            s.b = 5;
        } else if (tr_word(t, "half")) s.b = 6;
        else return tr_fail(t, "unknown light property");
        out[0] = s;
        return 1;
    }
    if (tr_word(t, "lightmodel")) {
        tr_dotword(t, "front"), tr_dotword(t, "back");
        if (tr_dotword(t, "ambient")) s.st = ST_LIGHTMODEL_AMBIENT;
        else if (tr_dotword(t, "scenecolor")) s.st = ST_LIGHTMODEL_SCENE;
        else return tr_fail(t, "unknown lightmodel property");
        out[0] = s;
        return 1;
    }
    if (tr_word(t, "lightprod")) {
        s.st = ST_LIGHTPROD;
        if (!tr_expect(t, '[') || !tr_int(t, &s.a) || !tr_expect(t, ']')) return 0;
        tr_dotword(t, "front"), tr_dotword(t, "back");
        if (tr_dotword(t, "ambient")) s.b = 0;
        else if (tr_dotword(t, "diffuse")) s.b = 1;
        else if (tr_dotword(t, "specular")) s.b = 2;
        else return tr_fail(t, "unknown lightprod property");
        out[0] = s;
        return 1;
    }
    if (tr_word(t, "material")) {
        s.st = ST_MATERIAL;
        tr_dotword(t, "front"), tr_dotword(t, "back");
        if (tr_dotword(t, "ambient")) s.b = 0;
        else if (tr_dotword(t, "diffuse")) s.b = 1;
        else if (tr_dotword(t, "specular")) s.b = 2;
        else if (tr_dotword(t, "emission")) s.b = 3;
        else if (tr_dotword(t, "shininess")) s.b = 4;
        else return tr_fail(t, "unknown material property");
        out[0] = s;
        return 1;
    }
    if (tr_word(t, "fog")) {
        if (tr_dotword(t, "color")) s.st = ST_FOG_COLOR;
        else if (tr_dotword(t, "params")) s.st = ST_FOG_PARAMS;
        else return tr_fail(t, "unknown fog property");
        out[0] = s;
        return 1;
    }
    if (tr_word(t, "texenv")) {
        s.st = ST_TEXENV_COLOR;
        if (!tr_index(t, &s.a, 0) || !tr_dotword(t, "color")) return tr_fail(t, "state.texenv[n].color");
        out[0] = s;
        return 1;
    }
    if (tr_word(t, "depth")) {
        if (!tr_dotword(t, "range")) return tr_fail(t, "state.depth.range");
        s.st   = ST_DEPTH_RANGE;
        out[0] = s;
        return 1;
    }
    if (tr_word(t, "point")) {
        if (tr_dotword(t, "size")) s.st = ST_POINT_SIZE;
        else if (tr_dotword(t, "attenuation")) s.st = ST_POINT_ATTEN;
        else return tr_fail(t, "unknown point property");
        out[0] = s;
        return 1;
    }
    if (tr_word(t, "clip")) {
        s.st = ST_CLIP;
        if (!tr_index(t, &s.a, 0) || !tr_dotword(t, "plane")) return tr_fail(t, "state.clip[n].plane");
        out[0] = s;
        return 1;
    }
    if (tr_word(t, "texgen")) {
        s.st = ST_TEXGEN;
        if (!tr_index(t, &s.a, 0)) return 0;
        if (!tr_dotword(t, "eye") && !tr_dotword(t, "object")) return tr_fail(t, "state.texgen[n].eye / object");
        if (!tr_expect(t, '.')) return 0;
        char w[8];
        tr_ident(t, w, sizeof(w)); /* s t r q */
        out[0] = s;
        return 1;
    }
    return tr_fail(t, "unknown state binding");
}

/* vertex attribute bindings -> NVIDIA aliasing index; fragment -> an expression */
static int tr_attrib_binding(arb_tr* t, char* expr, size_t n)
{
    if (t->vp) {
        if (!tr_word(t, "vertex") || !tr_expect(t, '.')) return tr_fail(t, "expected vertex.*");
        int a = -1, i;
        if (tr_word(t, "position")) a = 0;
        else if (tr_word(t, "weight")) {
            if (!tr_index(t, &i, 0)) return 0;
            a = 1;
        } else if (tr_word(t, "normal")) a = 2;
        else if (tr_word(t, "color")) {
            a = 3;
            if (tr_dotword(t, "secondary")) a = 4;
            else tr_dotword(t, "primary");
        } else if (tr_word(t, "fogcoord")) a = 5;
        else if (tr_word(t, "texcoord")) {
            if (!tr_index(t, &i, 0)) return 0;
            a = 8 + i;
        } else if (tr_word(t, "attrib")) {
            if (!tr_expect(t, '[') || !tr_int(t, &a) || !tr_expect(t, ']')) return tr_fail(t, "vertex.attrib[n]");
        } else if (tr_word(t, "matrixindex")) {
            if (!tr_index(t, &i, 0)) return 0;
            a = 7;
        } else {
            return tr_fail(t, "unknown vertex attribute");
        }
        if (a < 0 || a >= 16) return tr_fail(t, "attribute index out of range");
        t->p->attribs |= 1u << a;
        snprintf(expr, n, "fgl_va%d", a);
        return 1;
    }
    if (!tr_word(t, "fragment") || !tr_expect(t, '.')) return tr_fail(t, "expected fragment.*");
    if (tr_word(t, "color")) {
        int sec = tr_dotword(t, "secondary");
        if (!sec) tr_dotword(t, "primary");
        t->p->ins |= 1u << sec;
        snprintf(expr, n, "fgl_vc%d", sec);
        return 1;
    }
    if (tr_word(t, "texcoord")) {
        int i;
        if (!tr_index(t, &i, 0)) return 0;
        if (i < 0 || i >= 8) return tr_fail(t, "texcoord index out of range");
        t->p->ins |= 1u << (2 + i);
        snprintf(expr, n, "fgl_vt%d", i);
        return 1;
    }
    if (tr_word(t, "fogcoord")) {
        t->p->ins |= 1u << 10;
        snprintf(expr, n, "vec4(fgl_vfog, 0.0, 0.0, 1.0)");
        return 1;
    }
    if (tr_word(t, "position")) {
        t->p->reads_fragpos = 1;
        snprintf(expr, n, "gl_FragCoord");
        return 1;
    }
    return tr_fail(t, "unknown fragment attribute");
}

/* output bindings -> an lvalue */
static int tr_result_binding(arb_tr* t, char* lv, size_t n)
{
    if (!tr_word(t, "result") || !tr_expect(t, '.')) return tr_fail(t, "expected result.*");
    fgl_arbprog* p = t->p;
    if (t->vp) {
        if (tr_word(t, "position")) {
            p->writes_position = 1;
            snprintf(lv, n, "fgl_opos");
            return 1;
        }
        if (tr_word(t, "color")) {
            int back = 0, sec = 0;
            if (tr_dotword(t, "back")) back = 1;
            else tr_dotword(t, "front");
            if (tr_dotword(t, "secondary")) sec = 1;
            else tr_dotword(t, "primary");
            if (back) {
                snprintf(lv, n, "fgl_oback");
                return 1;
            }
            p->outs |= 1u << sec;
            snprintf(lv, n, "fgl_oc%d", sec);
            return 1;
        }
        if (tr_word(t, "texcoord")) {
            int i;
            if (!tr_index(t, &i, 0)) return 0;
            if (i < 0 || i >= 8) return tr_fail(t, "texcoord index out of range");
            p->outs |= 1u << (2 + i);
            snprintf(lv, n, "fgl_ot%d", i);
            return 1;
        }
        if (tr_word(t, "fogcoord")) {
            p->outs |= 1u << 10;
            snprintf(lv, n, "fgl_ofog");
            return 1;
        }
        if (tr_word(t, "pointsize")) {
            p->writes_psize = 1;
            snprintf(lv, n, "fgl_opsize");
            return 1;
        }
        return tr_fail(t, "unknown vertex result");
    }
    if (tr_word(t, "color")) {
        int i;
        if (!tr_index(t, &i, 0)) return 0;
        snprintf(lv, n, i ? "fgl_ounused" : "fgl_ocolor");
        return 1;
    }
    if (tr_word(t, "depth")) {
        p->writes_depth = 1;
        snprintf(lv, n, "fgl_odepth");
        return 1;
    }
    return tr_fail(t, "unknown fragment result");
}

/* ---- operands ---- */
static int tr_is_swizzle(const char* w)
{
    size_t n = strlen(w);
    if (n != 1 && n != 4) return 0;
    for (size_t i = 0; i < n; i++)
        if (!strchr("xyzwrgba", w[i])) return 0;
    return 1;
}
static void tr_swz_norm(const char* w, char* o) /* rgba -> xyzw, 1 -> 4 */
{
    size_t n = strlen(w);
    for (int i = 0; i < 4; i++) {
        char ch = w[n == 1 ? 0 : i];
        o[i]    = ch == 'r' ? 'x' : (ch == 'g' ? 'y' : (ch == 'b' ? 'z' : (ch == 'a' ? 'w' : ch)));
    }
    o[4] = 0;
}

static const char* tr_pref(arb_tr* t) { return t->vp ? "fgl_pv" : "fgl_pf"; }

/* a source operand -> a vec4 expression (scalar: true for a single component source) */
static int tr_src(arb_tr* t, char* e, size_t n, int* scalar)
{
    char base[192], w[64];
    int  neg = 0;
    if (scalar) *scalar = 0;
    tr_ws(t);
    if (*t->at == '-') neg = 1, t->at++;
    else if (*t->at == '+') t->at++;
    tr_ws(t);
    if (*t->at == '{' || isdigit((unsigned char)*t->at) || *t->at == '.') { /* an inline constant */
        arb_slot s[4];
        if (!tr_param_binding(t, s, 4)) return 0;
        snprintf(base, sizeof(base), "%s[%d]", tr_pref(t), tr_slot(t, &s[0]));
    } else if (tr_peek_ident(t, w, sizeof(w)) && (!strcmp(w, "vertex") || !strcmp(w, "fragment"))) {
        if (!tr_attrib_binding(t, base, sizeof(base))) return 0;
    } else if (!strcmp(w, "program") || !strcmp(w, "state")) {
        arb_slot s[4];
        int      k = tr_param_binding(t, s, 4);
        if (k != 1) return k ? tr_fail(t, "a single parameter expected") : 0;
        snprintf(base, sizeof(base), "%s[%d]", tr_pref(t), tr_slot(t, &s[0]));
    } else {
        if (!tr_ident(t, w, sizeof(w))) return tr_fail(t, "expected an operand");
        arb_name* a = tr_find(t, w);
        if (!a) return tr_fail(t, "%s is not declared", w);
        if (a->kind == 0) {
            snprintf(base, sizeof(base), "%s", a->expr);
        } else if (a->kind == 3) {
            snprintf(base, sizeof(base), "%s", a->expr);
        } else if (a->kind == 2) {
            if (tr_ch(t, '[')) { /* an element, maybe relative: arr[A0.x + n] */
                tr_ws(t);
                int idx;
                if (tr_int(t, &idx)) {
                    if (idx < 0 || idx >= a->count) return tr_fail(t, "%s[%d] out of range", a->name, idx);
                    snprintf(base, sizeof(base), "%s[%d]", tr_pref(t), a->base + idx);
                } else {
                    char an[64], comp[8];
                    if (!tr_ident(t, an, sizeof(an))) return tr_fail(t, "expected an index");
                    arb_name* ad = tr_find(t, an);
                    if (!ad || ad->kind != 1) return tr_fail(t, "%s is not an address register", an);
                    if (!tr_expect(t, '.') || !tr_ident(t, comp, sizeof(comp))) return tr_fail(t, "expected .x");
                    int off = 0;
                    tr_ws(t);
                    if (*t->at == '+' || *t->at == '-') {
                        int sg = *t->at == '-' ? -1 : 1;
                        t->at++;
                        if (!tr_int(t, &off)) return tr_fail(t, "expected an offset");
                        off *= sg;
                    }
                    snprintf(base, sizeof(base), "%s[clamp(%d + %s.x, 0, %d)]", tr_pref(t), a->base + off, ad->expr, ARB_MAX_SLOTS - 1);
                }
                if (!tr_expect(t, ']')) return 0;
            } else {
                snprintf(base, sizeof(base), "%s[%d]", tr_pref(t), a->base);
            }
        } else {
            return tr_fail(t, "%s cannot be read", w);
        }
    }
    /* swizzle */
    char sw[8] = "xyzw";
    const char* save = t->at;
    tr_ws(t);
    if (*t->at == '.') {
        t->at++;
        char s4[16];
        if (tr_ident(t, s4, sizeof(s4)) && tr_is_swizzle(s4)) {
            tr_swz_norm(s4, sw);
            if (scalar && strlen(s4) == 1) *scalar = 1;
        } else {
            t->at = save;
        }
    }
    if (!strcmp(sw, "xyzw")) snprintf(e, n, "%s(%s)", neg ? "-" : "", base);
    else snprintf(e, n, "%s(%s).%s", neg ? "-" : "", base, sw);
    return 1;
}

/* a destination: lvalue + write mask */
static int tr_dst(arb_tr* t, char* lv, size_t n, char* mask)
{
    char w[64];
    strcpy(mask, "xyzw");
    if (tr_peek_ident(t, w, sizeof(w)) && !strcmp(w, "result")) {
        if (!tr_result_binding(t, lv, n)) return 0;
    } else {
        if (!tr_ident(t, w, sizeof(w))) return tr_fail(t, "expected a destination");
        arb_name* a = tr_find(t, w);
        if (!a) return tr_fail(t, "%s is not declared", w);
        if (a->kind == 0 || a->kind == 1 || a->kind == 4) snprintf(lv, n, "%s", a->expr);
        else return tr_fail(t, "%s cannot be written", w);
    }
    const char* save = t->at;
    tr_ws(t);
    if (*t->at == '.') {
        t->at++;
        char m[16];
        if (tr_ident(t, m, sizeof(m))) {
            char o[8];
            int  k = 0;
            for (const char* q = m; *q && k < 4; q++) {
                char ch = *q == 'r' ? 'x' : (*q == 'g' ? 'y' : (*q == 'b' ? 'z' : (*q == 'a' ? 'w' : *q)));
                if (!strchr("xyzw", ch)) return tr_fail(t, "bad write mask");
                o[k++] = ch;
            }
            o[k] = 0;
            strcpy(mask, o);
        } else {
            t->at = save;
        }
    }
    return 1;
}

/* dst.mask = expression (a vec4), with saturation */
static void tr_assign(arb_tr* t, const char* lv, const char* mask, int sat, const char* expr)
{
    char e[1400];
    snprintf(e, sizeof(e), sat ? "clamp(%s, 0.0, 1.0)" : "%s", expr);
    if (!strncmp(lv, "A_", 2)) { /* ARL: address registers hold integers */
        tr_emit(t, "    %s.%s = ivec4(floor(%s)).%s;\n", lv, mask, e, mask);
        return;
    }
    if (!strcmp(mask, "xyzw")) tr_emit(t, "    %s = %s;\n", lv, e);
    else tr_emit(t, "    %s.%s = (%s).%s;\n", lv, mask, e, mask);
}

typedef struct arb_op {
    const char* name;
    int         nsrc;  /* vector sources */
    int         scalar; /* sources are scalars, the result is replicated */
    int         vp, fp; /* valid in */
} arb_op;
static const arb_op g_ops[] = {
    { "ABS", 1, 0, 1, 1 }, { "ADD", 2, 0, 1, 1 }, { "ARL", 1, 1, 1, 0 }, { "CMP", 3, 0, 0, 1 }, { "COS", 1, 1, 0, 1 },
    { "DP3", 2, 0, 1, 1 }, { "DP4", 2, 0, 1, 1 }, { "DPH", 2, 0, 1, 1 }, { "DST", 2, 0, 1, 1 }, { "EX2", 1, 1, 1, 1 },
    { "EXP", 1, 1, 1, 0 }, { "FLR", 1, 0, 1, 1 }, { "FRC", 1, 0, 1, 1 }, { "KIL", 1, 0, 0, 1 }, { "LG2", 1, 1, 1, 1 },
    { "LIT", 1, 0, 1, 1 }, { "LOG", 1, 1, 1, 0 }, { "LRP", 3, 0, 0, 1 }, { "MAD", 3, 0, 1, 1 }, { "MAX", 2, 0, 1, 1 },
    { "MIN", 2, 0, 1, 1 }, { "MOV", 1, 0, 1, 1 }, { "MUL", 2, 0, 1, 1 }, { "POW", 2, 1, 1, 1 }, { "RCP", 1, 1, 1, 1 },
    { "RSQ", 1, 1, 1, 1 }, { "SCS", 1, 1, 0, 1 }, { "SGE", 2, 0, 1, 1 }, { "SIN", 1, 1, 0, 1 }, { "SLT", 2, 0, 1, 1 },
    { "SUB", 2, 0, 1, 1 }, { "SWZ", 1, 0, 1, 1 }, { "TEX", 1, 0, 0, 1 }, { "TXB", 1, 0, 0, 1 }, { "TXP", 1, 0, 0, 1 },
    { "XPD", 2, 0, 1, 1 },
};

/* SWZ src, x, -y, 0, 1: the extended swizzle */
static int tr_swz(arb_tr* t, char* e, size_t n)
{
    char base[256], w[64];
    int  neg = 0;
    tr_ws(t);
    if (*t->at == '-') neg = 1, t->at++;
    if (tr_peek_ident(t, w, sizeof(w)) && (!strcmp(w, "vertex") || !strcmp(w, "fragment"))) {
        if (!tr_attrib_binding(t, base, sizeof(base))) return 0;
    } else if (!strcmp(w, "program") || !strcmp(w, "state")) {
        arb_slot s[4];
        if (tr_param_binding(t, s, 4) != 1) return tr_fail(t, "a single parameter expected");
        snprintf(base, sizeof(base), "%s[%d]", tr_pref(t), tr_slot(t, &s[0]));
    } else {
        if (!tr_ident(t, w, sizeof(w))) return tr_fail(t, "expected an operand");
        arb_name* a = tr_find(t, w);
        if (!a) return tr_fail(t, "%s is not declared", w);
        if (a->kind == 0 || a->kind == 3) snprintf(base, sizeof(base), "%s", a->expr);
        else if (a->kind == 2) snprintf(base, sizeof(base), "%s[%d]", tr_pref(t), a->base);
        else return tr_fail(t, "%s cannot be read", w);
    }
    char comp[4][48];
    for (int i = 0; i < 4; i++) {
        if (!tr_expect(t, ',')) return 0;
        tr_ws(t);
        int cn = 0;
        if (*t->at == '-') cn = 1, t->at++;
        else if (*t->at == '+') t->at++;
        tr_ws(t);
        char ch = *t->at++;
        const char* v;
        static const char* names[] = { "x", "y", "z", "w" };
        switch (ch) {
        case '0': v = "0.0"; break;
        case '1': v = "1.0"; break;
        case 'x': case 'r': v = names[0]; break;
        case 'y': case 'g': v = names[1]; break;
        case 'z': case 'b': v = names[2]; break;
        case 'w': case 'a': v = names[3]; break;
        default: return tr_fail(t, "bad extended swizzle");
        }
        if (v[0] == '0' || v[0] == '1') snprintf(comp[i], sizeof(comp[i]), "%s%s", cn ? "-" : "", v);
        else snprintf(comp[i], sizeof(comp[i]), "%s(%s).%s", cn ? "-" : "", base, v);
    }
    snprintf(e, n, "%svec4(%s, %s, %s, %s)", neg ? "-" : "", comp[0], comp[1], comp[2], comp[3]);
    return 1;
}

/* texture[n], TARGET */
static int tr_tex_target(arb_tr* t, int* unit, int* target)
{
    if (!tr_expect(t, ',') || !tr_word(t, "texture")) return tr_fail(t, "expected texture[n]");
    if (!tr_index(t, unit, 0)) return 0;
    if (*unit < 0 || *unit >= FGL_UNITS) return tr_fail(t, "texture unit out of range");
    if (!tr_expect(t, ',')) return 0;
    if (tr_word(t, "1D")) *target = 0;
    else if (tr_word(t, "2D")) *target = 1;
    else if (tr_word(t, "3D")) *target = 2;
    else if (tr_word(t, "CUBE")) *target = 3;
    else if (tr_word(t, "RECT")) *target = 4;
    else if (tr_word(t, "SHADOW2D")) *target = 1; /* compare not supported: sampled as 2D */
    else return tr_fail(t, "unknown texture target");
    t->p->samplers[*target < 4 ? *target : 3] |= *target == 4 ? (1u << (16 + *unit)) : (1u << *unit);
    return 1;
}

static const char* g_tgt_name[] = { "1D", "2D", "3D", "Cube", "Rect" };

static int tr_instruction(arb_tr* t, const char* opname)
{
    char base[8];
    snprintf(base, sizeof(base), "%.3s", opname);
    int sat = strlen(opname) > 3 && !strcmp(opname + 3, "_SAT");
    if (strlen(opname) > 3 && !sat) return tr_fail(t, "unknown instruction %s", opname);
    const arb_op* op = NULL;
    for (size_t i = 0; i < sizeof(g_ops) / sizeof(g_ops[0]); i++)
        if (!strcmp(g_ops[i].name, base)) op = &g_ops[i];
    if (!op || (t->vp && !op->vp) || (!t->vp && !op->fp)) return tr_fail(t, "unknown instruction %s", opname);
    if (t->vp && sat) return tr_fail(t, "_SAT in a vertex program");
    char lv[96], mask[8], a[512], b[512], c[512], e[1400];
    if (!strcmp(base, "KIL")) {
        if (!tr_src(t, a, sizeof(a), NULL)) return 0;
        tr_emit(t, "    if (any(lessThan(%s, vec4(0.0)))) discard;\n", a);
        return tr_expect(t, ';');
    }
    if (!tr_dst(t, lv, sizeof(lv), mask)) return 0;
    if (!strcmp(base, "SWZ")) {
        if (!tr_expect(t, ',') || !tr_swz(t, a, sizeof(a))) return 0;
        tr_assign(t, lv, mask, sat, a);
        return tr_expect(t, ';');
    }
    int sc[3];
    char* srcs[3] = { a, b, c };
    for (int i = 0; i < op->nsrc; i++)
        if (!tr_expect(t, ',') || !tr_src(t, srcs[i], 512, &sc[i])) return 0;
    if (op->scalar)
        for (int i = 0; i < op->nsrc; i++) { /* scalar sources: their first (only) component */
            char tmp[520];
            snprintf(tmp, sizeof(tmp), "(%s).x", srcs[i]);
            strcpy(srcs[i], tmp);
        }
    if (!strcmp(base, "TEX") || !strcmp(base, "TXB") || !strcmp(base, "TXP")) {
        int unit, tg;
        if (!tr_tex_target(t, &unit, &tg)) return 0;
        static const char* crd[] = { ".x", ".xy", ".xyz", ".xyz", ".xy" };
        char s[48];
        snprintf(s, sizeof(s), "fgl_s%d_%s", unit, g_tgt_name[tg]);
        if (!strcmp(base, "TEX")) snprintf(e, sizeof(e), "texture(%s, (%s)%s)", s, a, crd[tg]);
        else if (!strcmp(base, "TXB")) snprintf(e, sizeof(e), "texture(%s, (%s)%s, (%s).w)", s, a, crd[tg], a);
        else if (tg == 3) snprintf(e, sizeof(e), "texture(%s, (%s).xyz)", s, a); /* projecting a direction changes nothing */
        else if (tg == 0) snprintf(e, sizeof(e), "textureProj(%s, (%s).xw)", s, a);
        else if (tg == 2) snprintf(e, sizeof(e), "textureProj(%s, %s)", s, a);
        else snprintf(e, sizeof(e), "textureProj(%s, (%s).xyw)", s, a);
        tr_assign(t, lv, mask, sat, e);
        return tr_expect(t, ';');
    }
    if (!strcmp(base, "ABS")) snprintf(e, sizeof(e), "abs(%s)", a);
    else if (!strcmp(base, "ADD")) snprintf(e, sizeof(e), "(%s + %s)", a, b);
    else if (!strcmp(base, "SUB")) snprintf(e, sizeof(e), "(%s - %s)", a, b);
    else if (!strcmp(base, "MUL")) snprintf(e, sizeof(e), "(%s * %s)", a, b);
    else if (!strcmp(base, "MAD")) snprintf(e, sizeof(e), "(%s * %s + %s)", a, b, c);
    else if (!strcmp(base, "MIN")) snprintf(e, sizeof(e), "min(%s, %s)", a, b);
    else if (!strcmp(base, "MAX")) snprintf(e, sizeof(e), "max(%s, %s)", a, b);
    else if (!strcmp(base, "MOV")) snprintf(e, sizeof(e), "%s", a);
    else if (!strcmp(base, "ARL")) snprintf(e, sizeof(e), "vec4(%s)", a);
    else if (!strcmp(base, "FLR")) snprintf(e, sizeof(e), "floor(%s)", a);
    else if (!strcmp(base, "FRC")) snprintf(e, sizeof(e), "fract(%s)", a);
    else if (!strcmp(base, "DP3")) snprintf(e, sizeof(e), "vec4(dot((%s).xyz, (%s).xyz))", a, b);
    else if (!strcmp(base, "DP4")) snprintf(e, sizeof(e), "vec4(dot(%s, %s))", a, b);
    else if (!strcmp(base, "DPH")) snprintf(e, sizeof(e), "vec4(dot((%s).xyz, (%s).xyz) + (%s).w)", a, b, b);
    else if (!strcmp(base, "DST")) snprintf(e, sizeof(e), "vec4(1.0, (%s).y * (%s).y, (%s).z, (%s).w)", a, b, a, b);
    else if (!strcmp(base, "XPD")) snprintf(e, sizeof(e), "vec4(cross((%s).xyz, (%s).xyz), 1.0)", a, b);
    else if (!strcmp(base, "SGE")) snprintf(e, sizeof(e), "vec4(greaterThanEqual(%s, %s))", a, b);
    else if (!strcmp(base, "SLT")) snprintf(e, sizeof(e), "vec4(lessThan(%s, %s))", a, b);
    else if (!strcmp(base, "CMP")) snprintf(e, sizeof(e), "mix(%s, %s, vec4(lessThan(%s, vec4(0.0))))", c, b, a);
    else if (!strcmp(base, "LRP")) snprintf(e, sizeof(e), "mix(%s, %s, %s)", c, b, a);
    else if (!strcmp(base, "LIT")) snprintf(e, sizeof(e), "fgl_lit(%s)", a);
    else if (!strcmp(base, "EX2")) snprintf(e, sizeof(e), "vec4(exp2(%s))", a);
    else if (!strcmp(base, "LG2")) snprintf(e, sizeof(e), "vec4(log2(%s))", a);
    else if (!strcmp(base, "POW")) snprintf(e, sizeof(e), "vec4(pow(%s, %s))", a, b);
    else if (!strcmp(base, "RCP")) snprintf(e, sizeof(e), "vec4(1.0 / %s)", a);
    else if (!strcmp(base, "RSQ")) snprintf(e, sizeof(e), "vec4(inversesqrt(abs(%s)))", a);
    else if (!strcmp(base, "COS")) snprintf(e, sizeof(e), "vec4(cos(%s))", a);
    else if (!strcmp(base, "SIN")) snprintf(e, sizeof(e), "vec4(sin(%s))", a);
    else if (!strcmp(base, "SCS")) snprintf(e, sizeof(e), "vec4(cos(%s), sin(%s), 0.0, 0.0)", a, a);
    else if (!strcmp(base, "EXP")) snprintf(e, sizeof(e), "fgl_exp(%s)", a);
    else if (!strcmp(base, "LOG")) snprintf(e, sizeof(e), "fgl_log(%s)", a);
    else return tr_fail(t, "instruction %s", base);
    tr_assign(t, lv, mask, sat, e);
    return tr_expect(t, ';');
}

static int tr_statement(arb_tr* t)
{
    char w[64];
    if (!tr_ident(t, w, sizeof(w))) return tr_fail(t, "expected a statement");
    if (!strcmp(w, "OPTION")) {
        char o[64];
        if (!tr_ident(t, o, sizeof(o))) return tr_fail(t, "expected an option");
        if (!strcmp(o, "ARB_position_invariant")) t->p->position_invariant = 1;
        else if (!strcmp(o, "ARB_fog_linear")) t->p->fog_mode = 1;
        else if (!strcmp(o, "ARB_fog_exp")) t->p->fog_mode = 2;
        else if (!strcmp(o, "ARB_fog_exp2")) t->p->fog_mode = 3;
        /* precision hints, NV options: ignored */
        return tr_expect(t, ';');
    }
    if (!strcmp(w, "TEMP") || !strcmp(w, "ADDRESS")) {
        int addr = w[0] == 'A';
        do {
            char nm[64];
            if (!tr_ident(t, nm, sizeof(nm))) return tr_fail(t, "expected a name");
            arb_name* a = tr_add(t, nm, addr);
            if (!a) return 0;
            snprintf(a->expr, sizeof(a->expr), "%s_%s", addr ? "A" : "T", nm);
            if (addr) tr_decl(t, "    ivec4 A_%s = ivec4(0);\n", nm);
            else tr_decl(t, "    vec4 T_%s = vec4(0.0);\n", nm);
        } while (tr_ch(t, ','));
        return tr_expect(t, ';');
    }
    if (!strcmp(w, "PARAM")) {
        char nm[64];
        if (!tr_ident(t, nm, sizeof(nm))) return tr_fail(t, "expected a name");
        int arr = 0, size = -1;
        if (tr_ch(t, '[')) {
            arr = 1;
            if (!tr_int(t, &size)) size = -1;
            if (!tr_expect(t, ']')) return 0;
        }
        if (!tr_expect(t, '=')) return 0;
        arb_slot s[ARB_MAX_SLOTS];
        int      n = 0;
        if (arr) {
            if (!tr_expect(t, '{')) return 0;
            do {
                int k = tr_param_binding(t, s + n, ARB_MAX_SLOTS - n);
                if (!k) return 0;
                n += k;
            } while (tr_ch(t, ','));
            if (!tr_expect(t, '}')) return 0;
            if (size >= 0 && size != n) return tr_fail(t, "%s: %d elements declared, %d given", nm, size, n);
        } else {
            n = tr_param_binding(t, s, 4);
            if (!n) return 0;
            if (n > 1) arr = 1; /* a matrix: 4 rows */
        }
        arb_name* a = tr_add(t, nm, 2);
        if (!a) return 0;
        a->base  = arr ? tr_slots_run(t, s, n) : tr_slot(t, &s[0]);
        a->count = n;
        return tr_expect(t, ';');
    }
    if (!strcmp(w, "ATTRIB")) {
        char nm[64], ex[96];
        if (!tr_ident(t, nm, sizeof(nm)) || !tr_expect(t, '=') || !tr_attrib_binding(t, ex, sizeof(ex))) return tr_fail(t, "ATTRIB");
        arb_name* a = tr_add(t, nm, 3);
        if (!a) return 0;
        snprintf(a->expr, sizeof(a->expr), "%s", ex);
        return tr_expect(t, ';');
    }
    if (!strcmp(w, "OUTPUT")) {
        char nm[64], lv[96];
        if (!tr_ident(t, nm, sizeof(nm)) || !tr_expect(t, '=') || !tr_result_binding(t, lv, sizeof(lv))) return tr_fail(t, "OUTPUT");
        arb_name* a = tr_add(t, nm, 4);
        if (!a) return 0;
        snprintf(a->expr, sizeof(a->expr), "%s", lv);
        return tr_expect(t, ';');
    }
    if (!strcmp(w, "ALIAS")) {
        char nm[64], to[64];
        if (!tr_ident(t, nm, sizeof(nm)) || !tr_expect(t, '=') || !tr_ident(t, to, sizeof(to))) return tr_fail(t, "ALIAS");
        arb_name* b = tr_find(t, to);
        if (!b) return tr_fail(t, "%s is not declared", to);
        arb_name  copy = *b;
        arb_name* a    = tr_add(t, nm, copy.kind);
        if (!a) return 0;
        a->base = copy.base, a->count = copy.count; /* the same storage under another name */
        snprintf(a->expr, sizeof(a->expr), "%s", copy.expr);
        return tr_expect(t, ';');
    }
    return tr_instruction(t, w);
}

/* translate p->src into p->body / p->decl; 0 + error on failure */
static int fgl_arb_translate(fgl_arbprog* p, const char* src, size_t len, char* err, size_t errn, int* errpos)
{
    arb_tr* t = (arb_tr*)calloc(1, sizeof(arb_tr));
    if (!t) return 0;
    char* text = (char*)malloc(len + 1);
    if (!text) {
        free(t);
        return 0;
    }
    memcpy(text, src, len), text[len] = 0;
    t->p = p, t->s = text, t->at = text;
    p->nslots = 0, p->attribs = p->outs = p->ins = 0;
    memset(p->samplers, 0, sizeof(p->samplers));
    p->position_invariant = p->writes_position = p->writes_psize = p->writes_depth = p->fog_mode = p->reads_fragpos = 0;
    const char* hdr = p->target == GL_VERTEX_PROGRAM_ARB ? "!!ARBvp1.0" : "!!ARBfp1.0";
    t->vp           = p->target == GL_VERTEX_PROGRAM_ARB;
    int ok          = 1;
    if (strncmp(text, hdr, strlen(hdr))) ok = tr_fail(t, "the program does not start with %s", hdr);
    else t->at = text + strlen(hdr);
    while (ok) {
        tr_ws(t);
        if (tr_word(t, "END")) break;
        if (!*t->at) {
            ok = tr_fail(t, "missing END");
            break;
        }
        ok = tr_statement(t);
    }
    if (!ok && !t->err[0]) tr_fail(t, "syntax error");
    if (!ok) {
        snprintf(err, errn, "%s", t->err);
        *errpos = (int)(t->at - text);
        free(t->out), free(t->dcl);
    } else {
        free(p->body), free(p->decl);
        p->body = t->out ? t->out : _strdup("");
        p->decl = t->dcl ? t->dcl : _strdup("");
        *errpos = -1;
    }
    free(text);
    free(t);
    return ok;
}

/* ---- program objects ---- */
static fgl_arbprog* fgl_arb_get(fgl_ctx* c, GLuint name, int create)
{
    fgl_arbprog** list = (fgl_arbprog**)&c->arb_progs;
    for (int i = 0; i < c->narb; i++)
        if (list[0][i].used && list[0][i].name == name) return &list[0][i];
    if (!create || !name) return NULL;
    int slot = -1;
    for (int i = 0; i < c->narb && slot < 0; i++)
        if (!list[0][i].used) slot = i;
    if (slot < 0) {
        fgl_arbprog* n = (fgl_arbprog*)realloc(*list, ((size_t)c->narb + 16) * sizeof(fgl_arbprog));
        if (!n) return NULL;
        memset(n + c->narb, 0, 16 * sizeof(fgl_arbprog));
        *list = n, slot = c->narb, c->narb += 16;
    }
    fgl_arbprog* p = &list[0][slot];
    memset(p, 0, sizeof(*p));
    p->used = 1, p->name = name;
    return p;
}

static int fgl_arb_tindex(GLenum target) { return target == GL_FRAGMENT_PROGRAM_ARB; }

void APIENTRY glGenProgramsARB(GLsizei n, GLuint* out)
{
    FGL_CTX_OR_RETURN(c);
    static GLuint next = 1;
    for (GLsizei i = 0; i < n; i++) {
        while (fgl_arb_get(c, next, 0)) next++;
        out[i] = next++;
    }
}

void APIENTRY glDeleteProgramsARB(GLsizei n, const GLuint* names)
{
    FGL_CTX_OR_RETURN(c);
    for (GLsizei i = 0; i < n; i++) {
        fgl_arbprog* p = names[i] ? fgl_arb_get(c, names[i], 0) : NULL;
        if (!p) continue;
        for (int k = 0; k < 2; k++)
            if (c->arb_bound[k] == names[i]) c->arb_bound[k] = 0;
        free(p->body), free(p->decl);
        memset(p, 0, sizeof(*p));
    }
}

GLboolean APIENTRY glIsProgramARB(GLuint name)
{
    fgl_ctx* c = fgl_cur();
    return c && name && fgl_arb_get(c, name, 0) ? GL_TRUE : GL_FALSE;
}

void APIENTRY glBindProgramARB(GLenum target, GLuint name)
{
    FGL_CTX_OR_RETURN(c);
    if (target != GL_VERTEX_PROGRAM_ARB && target != GL_FRAGMENT_PROGRAM_ARB) {
        fgl_error(GL_INVALID_ENUM);
        return;
    }
    fgl_arbprog* p = name ? fgl_arb_get(c, name, 1) : NULL;
    if (p && p->target && p->target != target) {
        fgl_error(GL_INVALID_OPERATION);
        return;
    }
    if (p) p->target = target;
    c->arb_bound[fgl_arb_tindex(target)] = name;
}

void APIENTRY glProgramStringARB(GLenum target, GLenum format, GLsizei len, const void* str)
{
    FGL_CTX_OR_RETURN(c);
    if (target != GL_VERTEX_PROGRAM_ARB && target != GL_FRAGMENT_PROGRAM_ARB) {
        fgl_error(GL_INVALID_ENUM);
        return;
    }
    if (format != GL_PROGRAM_FORMAT_ASCII_ARB) {
        fgl_error(GL_INVALID_ENUM);
        return;
    }
    GLuint name = c->arb_bound[fgl_arb_tindex(target)];
    static fgl_arbprog p0; /* program 0 is not editable */
    fgl_arbprog* p = name ? fgl_arb_get(c, name, 1) : &p0;
    if (!name) {
        fgl_error(GL_INVALID_OPERATION);
        return;
    }
    p->target = target;
    char err[256];
    int  pos;
    p->valid = fgl_arb_translate(p, (const char*)str, (size_t)(len < 0 ? 0 : len), err, sizeof(err), &pos);
    p->gen++;
    c->arb_error_pos = pos;
    snprintf(c->arb_error, sizeof(c->arb_error), "%s", p->valid ? "" : err);
    if (!p->valid) {
        fgl_log("ARB %s program %u: %s at %d\n", target == GL_VERTEX_PROGRAM_ARB ? "vertex" : "fragment", name, err, pos);
        fgl_error(GL_INVALID_OPERATION);
    }
}

static void fgl_arb_param(GLenum target, int local, GLuint index, const float* v)
{
    FGL_CTX_OR_RETURN(c);
    if (target != GL_VERTEX_PROGRAM_ARB && target != GL_FRAGMENT_PROGRAM_ARB) {
        fgl_error(GL_INVALID_ENUM);
        return;
    }
    if (index >= (GLuint)(local ? ARB_MAX_LOCAL : ARB_MAX_ENV)) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    if (!local) {
        memcpy(c->arb_env[fgl_arb_tindex(target)][index], v, 16);
        return;
    }
    fgl_arbprog* p = fgl_arb_get(c, c->arb_bound[fgl_arb_tindex(target)], 0);
    if (p) memcpy(p->local[index], v, 16);
}
void APIENTRY glProgramEnvParameter4fARB(GLenum t, GLuint i, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    float v[4] = { x, y, z, w };
    fgl_arb_param(t, 0, i, v);
}
void APIENTRY glProgramEnvParameter4fvARB(GLenum t, GLuint i, const GLfloat* v) { fgl_arb_param(t, 0, i, v); }
void APIENTRY glProgramEnvParameter4dARB(GLenum t, GLuint i, GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
    float v[4] = { (float)x, (float)y, (float)z, (float)w };
    fgl_arb_param(t, 0, i, v);
}
void APIENTRY glProgramEnvParameter4dvARB(GLenum t, GLuint i, const GLdouble* d)
{
    float v[4] = { (float)d[0], (float)d[1], (float)d[2], (float)d[3] };
    fgl_arb_param(t, 0, i, v);
}
void APIENTRY glProgramLocalParameter4fARB(GLenum t, GLuint i, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    float v[4] = { x, y, z, w };
    fgl_arb_param(t, 1, i, v);
}
void APIENTRY glProgramLocalParameter4fvARB(GLenum t, GLuint i, const GLfloat* v) { fgl_arb_param(t, 1, i, v); }
void APIENTRY glProgramLocalParameter4dARB(GLenum t, GLuint i, GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
    float v[4] = { (float)x, (float)y, (float)z, (float)w };
    fgl_arb_param(t, 1, i, v);
}
void APIENTRY glProgramLocalParameter4dvARB(GLenum t, GLuint i, const GLdouble* d)
{
    float v[4] = { (float)d[0], (float)d[1], (float)d[2], (float)d[3] };
    fgl_arb_param(t, 1, i, v);
}
void APIENTRY glProgramEnvParameters4fvEXT(GLenum t, GLuint i, GLsizei n, const GLfloat* v)
{
    for (GLsizei k = 0; k < n; k++) fgl_arb_param(t, 0, i + (GLuint)k, v + 4 * k);
}
void APIENTRY glProgramLocalParameters4fvEXT(GLenum t, GLuint i, GLsizei n, const GLfloat* v)
{
    for (GLsizei k = 0; k < n; k++) fgl_arb_param(t, 1, i + (GLuint)k, v + 4 * k);
}

static void fgl_arb_getparam(GLenum target, int local, GLuint index, float* v)
{
    FGL_CTX_OR_RETURN(c);
    memset(v, 0, 16);
    if ((target != GL_VERTEX_PROGRAM_ARB && target != GL_FRAGMENT_PROGRAM_ARB) || index >= (GLuint)(local ? ARB_MAX_LOCAL : ARB_MAX_ENV)) {
        fgl_error(GL_INVALID_VALUE);
        return;
    }
    if (!local) {
        memcpy(v, c->arb_env[fgl_arb_tindex(target)][index], 16);
        return;
    }
    fgl_arbprog* p = fgl_arb_get(c, c->arb_bound[fgl_arb_tindex(target)], 0);
    if (p) memcpy(v, p->local[index], 16);
}
void APIENTRY glGetProgramEnvParameterfvARB(GLenum t, GLuint i, GLfloat* v) { fgl_arb_getparam(t, 0, i, v); }
void APIENTRY glGetProgramLocalParameterfvARB(GLenum t, GLuint i, GLfloat* v) { fgl_arb_getparam(t, 1, i, v); }
void APIENTRY glGetProgramEnvParameterdvARB(GLenum t, GLuint i, GLdouble* d)
{
    float v[4];
    fgl_arb_getparam(t, 0, i, v);
    for (int k = 0; k < 4; k++) d[k] = v[k];
}
void APIENTRY glGetProgramLocalParameterdvARB(GLenum t, GLuint i, GLdouble* d)
{
    float v[4];
    fgl_arb_getparam(t, 1, i, v);
    for (int k = 0; k < 4; k++) d[k] = v[k];
}

void APIENTRY glGetProgramivARB(GLenum target, GLenum pname, GLint* v)
{
    FGL_CTX_OR_RETURN(c);
    fgl_arbprog* p  = fgl_arb_get(c, c->arb_bound[fgl_arb_tindex(target)], 0);
    int          vp = target == GL_VERTEX_PROGRAM_ARB;
    switch (pname) {
    case GL_PROGRAM_LENGTH_ARB: *v = 0; break;
    case GL_PROGRAM_FORMAT_ARB: *v = GL_PROGRAM_FORMAT_ASCII_ARB; break;
    case GL_PROGRAM_BINDING_ARB: *v = (GLint)c->arb_bound[fgl_arb_tindex(target)]; break;
    case GL_PROGRAM_UNDER_NATIVE_LIMITS_ARB: *v = p && p->valid; break;
    case GL_MAX_PROGRAM_ENV_PARAMETERS_ARB: *v = ARB_MAX_ENV; break;
    case GL_MAX_PROGRAM_LOCAL_PARAMETERS_ARB: *v = ARB_MAX_LOCAL; break;
    case GL_MAX_PROGRAM_INSTRUCTIONS_ARB: case GL_MAX_PROGRAM_NATIVE_INSTRUCTIONS_ARB:
    case GL_MAX_PROGRAM_ALU_INSTRUCTIONS_ARB: case GL_MAX_PROGRAM_NATIVE_ALU_INSTRUCTIONS_ARB:
    case GL_MAX_PROGRAM_TEX_INSTRUCTIONS_ARB: case GL_MAX_PROGRAM_NATIVE_TEX_INSTRUCTIONS_ARB:
    case GL_MAX_PROGRAM_TEX_INDIRECTIONS_ARB: case GL_MAX_PROGRAM_NATIVE_TEX_INDIRECTIONS_ARB:
        *v = 16384;
        break;
    case GL_MAX_PROGRAM_TEMPORARIES_ARB: case GL_MAX_PROGRAM_NATIVE_TEMPORARIES_ARB: *v = 256; break;
    case GL_MAX_PROGRAM_PARAMETERS_ARB: case GL_MAX_PROGRAM_NATIVE_PARAMETERS_ARB: *v = ARB_MAX_SLOTS; break;
    case GL_MAX_PROGRAM_ATTRIBS_ARB: case GL_MAX_PROGRAM_NATIVE_ATTRIBS_ARB: *v = vp ? 16 : 11; break;
    case GL_MAX_PROGRAM_ADDRESS_REGISTERS_ARB: case GL_MAX_PROGRAM_NATIVE_ADDRESS_REGISTERS_ARB: *v = vp ? 1 : 0; break;
    case GL_MAX_PROGRAM_MATRICES_ARB: *v = 8; break;
    case GL_MAX_PROGRAM_MATRIX_STACK_DEPTH_ARB: *v = 1; break;
    case GL_PROGRAM_INSTRUCTIONS_ARB: case GL_PROGRAM_NATIVE_INSTRUCTIONS_ARB: case GL_PROGRAM_TEMPORARIES_ARB:
    case GL_PROGRAM_NATIVE_TEMPORARIES_ARB: case GL_PROGRAM_ATTRIBS_ARB: case GL_PROGRAM_NATIVE_ATTRIBS_ARB:
    case GL_PROGRAM_ADDRESS_REGISTERS_ARB: case GL_PROGRAM_NATIVE_ADDRESS_REGISTERS_ARB:
        *v = 1;
        break;
    case GL_PROGRAM_PARAMETERS_ARB: case GL_PROGRAM_NATIVE_PARAMETERS_ARB: *v = p ? p->nslots : 0; break;
    default: *v = 0; break;
    }
}
void APIENTRY glGetProgramStringARB(GLenum target, GLenum pname, void* s)
{
    (void)target, (void)pname;
    if (s) *(char*)s = 0;
}

const char* fgl_arb_error_string(fgl_ctx* c) { return c->arb_error; }

/* ---- the GLSL of a (vertex, fragment) program pair ---- */
/* fixed function stand ins, as ARB programs */
static const char* g_fixed_vp = "!!ARBvp1.0\n"
                                "OPTION ARB_position_invariant;\n"
                                "PARAM t0[4] = { state.matrix.texture[0] };\n"
                                "PARAM mv2 = state.matrix.modelview.row[2];\n"
                                "MOV result.color, vertex.color;\n"
                                "MOV result.color.secondary, vertex.color.secondary;\n"
                                "DP4 result.texcoord[0].x, t0[0], vertex.texcoord[0];\n"
                                "DP4 result.texcoord[0].y, t0[1], vertex.texcoord[0];\n"
                                "DP4 result.texcoord[0].z, t0[2], vertex.texcoord[0];\n"
                                "DP4 result.texcoord[0].w, t0[3], vertex.texcoord[0];\n"
                                "MOV result.texcoord[1], vertex.texcoord[1];\n"
                                "MOV result.texcoord[2], vertex.texcoord[2];\n"
                                "MOV result.texcoord[3], vertex.texcoord[3];\n"
                                "MOV result.texcoord[4], vertex.texcoord[4];\n"
                                "MOV result.texcoord[5], vertex.texcoord[5];\n"
                                "MOV result.texcoord[6], vertex.texcoord[6];\n"
                                "MOV result.texcoord[7], vertex.texcoord[7];\n"
                                "DP4 result.fogcoord.x, -mv2, vertex.position;\n"
                                "END\n";
static const char* g_fixed_fp_tex = "!!ARBfp1.0\nTEMP t;\nTEX t, fragment.texcoord[0], texture[0], 2D;\n"
                                    "MUL result.color, t, fragment.color;\nEND\n";
static const char* g_fixed_fp_col = "!!ARBfp1.0\nMOV result.color, fragment.color;\nEND\n";

static const char* g_helpers = "vec4 fgl_lit(vec4 a) {\n"
                               "    float d = max(a.x, 0.0), s = a.x > 0.0 ? pow(max(a.y, 0.0), clamp(a.w, -128.0, 128.0)) : 0.0;\n"
                               "    return vec4(1.0, d, s, 1.0);\n"
                               "}\n"
                               "vec4 fgl_exp(float x) { float f = floor(x); return vec4(exp2(f), x - f, exp2(x), 1.0); }\n"
                               "vec4 fgl_log(float x) { float a = abs(x), e = floor(log2(a)); return vec4(e, a / exp2(e), log2(a), 1.0); }\n";

typedef struct fgl_sb2 {
    char*  p;
    size_t n, cap;
} fgl_sb2;
static void sb2(fgl_sb2* b, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    tr_put(&b->p, &b->n, &b->cap, fmt, ap);
    va_end(ap);
}
static void sb2s(fgl_sb2* b, const char* s) /* large strings (no formatting) */
{
    size_t k = strlen(s);
    if (b->n + k + 1 > b->cap) {
        size_t c = b->cap ? b->cap * 2 : 4096;
        while (c < b->n + k + 1) c *= 2;
        char* q = (char*)realloc(b->p, c);
        if (!q) return;
        b->p = q, b->cap = c;
    }
    memcpy(b->p + b->n, s, k + 1);
    b->n += k;
}

static char* fgl_arb_vs_glsl(const fgl_arbprog* v, unsigned outs)
{
    fgl_sb2 b = { 0 };
    sb2(&b, "#version 330 core\n");
    for (int a = 0; a < 16; a++)
        if (v->attribs & (1u << a)) sb2(&b, "layout(location = %d) in vec4 fgl_va%d;\n", a, a);
    if (v->position_invariant && !(v->attribs & 1u)) sb2(&b, "layout(location = 0) in vec4 fgl_va0;\n");
    sb2(&b, "layout(std140) uniform fgl_PV { vec4 fgl_pv[%d]; };\n", v->nslots + 4 > 4 ? v->nslots + 4 : 4);
    for (int i = 0; i < 10; i++)
        if (outs & (1u << i)) sb2(&b, "out vec4 %s%d;\n", i < 2 ? "fgl_vc" : "fgl_vt", i < 2 ? i : i - 2);
    if (outs & (1u << 10)) sb2(&b, "out float fgl_vfog;\n"); /* varyings link by name */
    sb2s(&b, g_helpers);
    sb2(&b, "void main() {\n");
    sb2(&b, "    vec4 fgl_opos = vec4(0.0, 0.0, 0.0, 1.0), fgl_oback = vec4(0.0), fgl_ofog = vec4(0.0), fgl_opsize = vec4(1.0);\n");
    for (int i = 0; i < 10; i++) sb2(&b, "    vec4 %s%d = vec4(0.0, 0.0, 0.0, 1.0);\n", i < 2 ? "fgl_oc" : "fgl_ot", i < 2 ? i : i - 2);
    sb2s(&b, v->decl ? v->decl : "");
    sb2s(&b, v->body ? v->body : "");
    if (v->position_invariant) { /* the fixed transform: state.matrix.mvp rows, the last four slots */
        int m = v->nslots;
        sb2(&b, "    fgl_opos = vec4(dot(fgl_pv[%d], fgl_va0), dot(fgl_pv[%d], fgl_va0), dot(fgl_pv[%d], fgl_va0), dot(fgl_pv[%d], fgl_va0));\n", m,
            m + 1, m + 2, m + 3);
    }
    sb2(&b, "    gl_Position = fgl_opos;\n");
    if (v->writes_psize) sb2(&b, "    gl_PointSize = fgl_opsize.x;\n");
    for (int i = 0; i < 10; i++)
        if (outs & (1u << i)) sb2(&b, "    %s%d = %s%d;\n", i < 2 ? "fgl_vc" : "fgl_vt", i < 2 ? i : i - 2, i < 2 ? "fgl_oc" : "fgl_ot", i < 2 ? i : i - 2);
    if (outs & (1u << 10)) sb2(&b, "    fgl_vfog = fgl_ofog.x;\n");
    sb2(&b, "}\n");
    return b.p;
}

static char* fgl_arb_fs_glsl(const fgl_arbprog* f)
{
    fgl_sb2 b = { 0 };
    sb2(&b, "#version 330 core\n");
    for (int i = 0; i < 10; i++)
        if (f->ins & (1u << i)) sb2(&b, "in vec4 %s%d;\n", i < 2 ? "fgl_vc" : "fgl_vt", i < 2 ? i : i - 2);
    if (f->ins & (1u << 10) || f->fog_mode) sb2(&b, "in float fgl_vfog;\n");
    sb2(&b, "layout(std140) uniform fgl_PF { vec4 fgl_pf[%d]; };\n", f->nslots + 2 > 2 ? f->nslots + 2 : 2);
    static const char* sty[] = { "sampler1D", "sampler2D", "sampler3D", "samplerCube" };
    for (int tg = 0; tg < 4; tg++)
        for (int u = 0; u < FGL_UNITS; u++)
            if (f->samplers[tg] & (1u << u)) sb2(&b, "uniform %s fgl_s%d_%s;\n", sty[tg], u, g_tgt_name[tg]);
    for (int u = 0; u < FGL_UNITS; u++)
        if (f->samplers[3] & (1u << (16 + u))) sb2(&b, "uniform sampler2DRect fgl_s%d_Rect;\n", u);
    sb2(&b, "layout(location = 0) out vec4 fgl_frag;\n");
    sb2s(&b, g_helpers);
    sb2(&b, "void main() {\n");
    sb2(&b, "    vec4 fgl_ocolor = vec4(0.0), fgl_ounused = vec4(0.0), fgl_odepth = vec4(gl_FragCoord.z);\n");
    sb2s(&b, f->decl ? f->decl : "");
    sb2s(&b, f->body ? f->body : "");
    if (f->fog_mode) { /* fog.color and fog.params in the last two slots */
        int m = f->nslots;
        if (f->fog_mode == 1) sb2(&b, "    float fgl_f = clamp((fgl_pf[%d].z - fgl_vfog) * fgl_pf[%d].w, 0.0, 1.0);\n", m + 1, m + 1);
        else if (f->fog_mode == 2) sb2(&b, "    float fgl_f = clamp(exp(-fgl_pf[%d].x * fgl_vfog), 0.0, 1.0);\n", m + 1);
        else sb2(&b, "    float fgl_f = clamp(exp(-pow(fgl_pf[%d].x * fgl_vfog, 2.0)), 0.0, 1.0);\n", m + 1);
        sb2(&b, "    fgl_ocolor.rgb = mix(fgl_pf[%d].rgb, fgl_ocolor.rgb, fgl_f);\n", m);
    }
    sb2(&b, "    fgl_frag = fgl_ocolor;\n}\n");
    return b.p;
}

/* the stand in programs (translated once per context) */
static fgl_arbprog* fgl_arb_fixed(fgl_ctx* c, int which)
{
    fgl_arbprog** fx = (fgl_arbprog**)c->arb_fixed;
    if (!fx[which]) {
        fx[which] = (fgl_arbprog*)calloc(1, sizeof(fgl_arbprog));
        if (!fx[which]) return NULL;
        fx[which]->target = which == 0 ? GL_VERTEX_PROGRAM_ARB : GL_FRAGMENT_PROGRAM_ARB;
        const char* src   = which == 0 ? g_fixed_vp : (which == 1 ? g_fixed_fp_tex : g_fixed_fp_col);
        char        err[256];
        int         pos;
        fx[which]->valid = fgl_arb_translate(fx[which], src, strlen(src), err, sizeof(err), &pos);
        fx[which]->name  = 0;
        if (!fx[which]->valid) fgl_log("internal ARB program %d: %s\n", which, err);
    }
    return fx[which]->valid ? fx[which] : NULL;
}

/* the GL program of the enabled ARB programs (NULL: none enabled or invalid) */
typedef struct fgl_arbpair {
    const void* vp;
    const void* fp;
    int         vgen, fgen;
    GLuint      prog;
} fgl_arbpair;

static GLuint fgl_arb_compile(fgl_ctx* c, const fgl_arbprog* v, const fgl_arbprog* f)
{
    unsigned outs = f->ins | (f->fog_mode ? 1u << 10 : 0u); /* what the fragment program reads */
    char*    vs   = fgl_arb_vs_glsl(v, outs);
    char*    fs   = fgl_arb_fs_glsl(f);
    GLuint   p    = 0;
    if (vs && fs) {
        GLuint save = c->program;
        GLuint s[2] = { glCreateShader(GL_VERTEX_SHADER), glCreateShader(GL_FRAGMENT_SHADER) };
        const char* srcs[2] = { vs, fs };
        p = glCreateProgram();
        for (int k = 0; k < 2; k++) {
            glShaderSource(s[k], 1, &srcs[k], NULL);
            glCompileShader(s[k]);
            glAttachShader(p, s[k]);
        }
        glLinkProgram(p);
        GLint ok = 0;
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (getenv("FATGL_DUMP_ARB")) fgl_log("ARB programs %u / %u:\n--- vertex ---\n%s--- fragment ---\n%s", v->name, f->name, vs, fs);
        if (!ok) {
            fgl_log("ARB programs %u / %u: the GLSL does not link\n--- vertex ---\n%s--- fragment ---\n%s", v->name, f->name, vs, fs);
            glDeleteProgram(p);
            p = 0;
        } else {
            fgl_program* gp = fgl_program_get(c, p);
            for (int i = 0; gp && i < gp->nu; i++) { /* samplers fgl_s<unit>_<target>: their texture unit */
                int unit;
                if (sscanf(gp->u[i].name, "fgl_s%d_", &unit) == 1) gp->u[i].unit = unit;
            }
        }
        for (int k = 0; k < 2; k++) glDeleteShader(s[k]);
        c->program = save;
    }
    free(vs);
    free(fs);
    return p;
}

fgl_program* fgl_arb_program(fgl_ctx* c)
{
    int ve = (c->enables & FGL_E_VP) != 0, fe = (c->enables & FGL_E_FP) != 0;
    if (!ve && !fe) return NULL;
    const fgl_arbprog* v = ve ? fgl_arb_get(c, c->arb_bound[0], 0) : NULL;
    const fgl_arbprog* f = fe ? fgl_arb_get(c, c->arb_bound[1], 0) : NULL;
    if ((ve && (!v || !v->valid)) || (fe && (!f || !f->valid))) return NULL; /* GL: INVALID_OPERATION at the draw */
    int ftex = !f && (c->tex2d_units & 1) && c->unit_bind[FGL_TT_2D][0];
    if (!v) v = fgl_arb_fixed(c, 0);
    if (!f) f = fgl_arb_fixed(c, ftex ? 1 : 2);
    if (!v || !f) return NULL;
    fgl_arbpair* pairs = (fgl_arbpair*)c->arb_pairs;
    for (int i = 0; i < c->narb_pairs; i++)
        if (pairs[i].vp == v && pairs[i].fp == f && pairs[i].vgen == v->gen && pairs[i].fgen == f->gen)
            return pairs[i].prog ? fgl_program_get(c, pairs[i].prog) : NULL;
    /* a new pair (or a recompiled program): build it */
    for (int i = 0; i < c->narb_pairs; i++)
        if (pairs[i].vp == v && pairs[i].fp == f) { /* replaced */
            if (pairs[i].prog) glDeleteProgram(pairs[i].prog);
            pairs[i] = pairs[--c->narb_pairs];
            break;
        }
    if (c->narb_pairs == c->carb_pairs) {
        int          n  = c->carb_pairs ? c->carb_pairs * 2 : 32;
        fgl_arbpair* np = (fgl_arbpair*)realloc(c->arb_pairs, (size_t)n * sizeof(fgl_arbpair));
        if (!np) return NULL;
        c->arb_pairs = np, c->carb_pairs = n, pairs = np;
    }
    fgl_arbpair* e = &pairs[c->narb_pairs++];
    e->vp = v, e->fp = f, e->vgen = v->gen, e->fgen = f->gen;
    e->prog = fgl_arb_compile(c, v, f);
    return e->prog ? fgl_program_get(c, e->prog) : NULL;
}

/* ---- the parameter blocks before a draw ---- */
static fm_mat4 arb_mul(const fm_mat4* a, const fm_mat4* b)
{
    fm_mat4      r;
    const float* A = &a->c[0].x;
    const float* B = &b->c[0].x;
    float*       R = &r.c[0].x;
    for (int col = 0; col < 4; col++)
        for (int k = 0; k < 4; k++)
            R[col * 4 + k] = A[k] * B[col * 4] + A[4 + k] * B[col * 4 + 1] + A[8 + k] * B[col * 4 + 2] + A[12 + k] * B[col * 4 + 3];
    return r;
}
static fm_mat4 arb_transpose(const fm_mat4* m)
{
    fm_mat4      r;
    const float* A = &m->c[0].x;
    float*       R = &r.c[0].x;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) R[i * 4 + j] = A[j * 4 + i];
    return r;
}

static void arb_state(fgl_ctx* c, const arb_slot* s, float* o)
{
    o[0] = o[1] = o[2] = 0, o[3] = 1;
    switch (s->st) {
    case ST_MATRIX: {
        fm_mat4 m = fm_mat4_identity();
        if (s->a == MAT_MODELVIEW && s->index == 0) m = c->mstack[FGL_MV][c->msp[FGL_MV]];
        else if (s->a == MAT_PROJECTION) m = c->mstack[FGL_PROJ][c->msp[FGL_PROJ]];
        else if (s->a == MAT_MVP) m = arb_mul(&c->mstack[FGL_PROJ][c->msp[FGL_PROJ]], &c->mstack[FGL_MV][c->msp[FGL_MV]]);
        else if (s->a == MAT_TEXTURE && s->index == 0) m = c->mstack[FGL_TEXM][c->msp[FGL_TEXM]];
        if (s->b == 1 || s->b == 3) m = fm_mat4_inverse(m);
        if (s->b == 2 || s->b == 3) m = arb_transpose(&m);
        const float* f = &m.c[0].x; /* column major: row r = (m[0][r], m[1][r], m[2][r], m[3][r]) */
        for (int k = 0; k < 4; k++) o[k] = f[k * 4 + s->row];
        return;
    }
    case ST_LIGHT: {
        int i = s->a & 7;
        switch (s->b) {
        case 0: memcpy(o, c->light[i].amb, 16); break;
        case 1: memcpy(o, c->light[i].dif, 16); break;
        case 2: memcpy(o, c->light[i].spe, 16); break;
        case 3: memcpy(o, c->light[i].pos, 16); break;
        case 4: o[0] = c->light[i].att[0], o[1] = c->light[i].att[1], o[2] = c->light[i].att[2], o[3] = c->light[i].spot_exp; break;
        case 5: memcpy(o, c->light[i].spot_dir, 12), o[3] = cosf(c->light[i].spot_cut * 3.14159265f / 180.0f); break;
        default: { /* half vector, infinite viewer */
            float h[3] = { c->light[i].pos[0], c->light[i].pos[1], c->light[i].pos[2] + 1.0f };
            float l    = sqrtf(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]);
            if (l > 0) h[0] /= l, h[1] /= l, h[2] /= l;
            o[0] = h[0], o[1] = h[1], o[2] = h[2], o[3] = 1;
            break;
        }
        }
        return;
    }
    case ST_LIGHTMODEL_AMBIENT: memcpy(o, c->light_model_ambient, 16); return;
    case ST_LIGHTMODEL_SCENE:
        for (int k = 0; k < 3; k++) o[k] = c->mat.emi[k] + c->mat.amb[k] * c->light_model_ambient[k];
        o[3] = c->mat.dif[3];
        return;
    case ST_LIGHTPROD: {
        int          i  = s->a & 7;
        const float* lp = s->b == 0 ? c->light[i].amb : (s->b == 1 ? c->light[i].dif : c->light[i].spe);
        const float* mp = s->b == 0 ? c->mat.amb : (s->b == 1 ? c->mat.dif : c->mat.spe);
        for (int k = 0; k < 4; k++) o[k] = lp[k] * mp[k];
        o[3] = c->mat.dif[3];
        return;
    }
    case ST_MATERIAL:
        if (s->b == 4) o[0] = c->mat.shin, o[1] = o[2] = 0, o[3] = 1;
        else memcpy(o, s->b == 0 ? c->mat.amb : (s->b == 1 ? c->mat.dif : (s->b == 2 ? c->mat.spe : c->mat.emi)), 16);
        return;
    case ST_FOG_COLOR: memcpy(o, c->fog_color, 16); return;
    case ST_FOG_PARAMS:
        o[0] = c->fog_density, o[1] = c->fog_start, o[2] = c->fog_end;
        o[3] = c->fog_end != c->fog_start ? 1.0f / (c->fog_end - c->fog_start) : 0.0f;
        return;
    case ST_DEPTH_RANGE: o[0] = 0, o[1] = 1, o[2] = 1, o[3] = 1; return;
    case ST_POINT_SIZE: o[0] = c->point_size, o[1] = 1, o[2] = 64, o[3] = 1; return;
    case ST_POINT_ATTEN: o[0] = 1, o[1] = 0, o[2] = 0, o[3] = 1; return;
    default: o[0] = o[1] = o[2] = o[3] = 0; return;
    }
}

/* fills a stage's block: the slots, then the stage's extras (mvp rows / fog) */
static size_t arb_fill(fgl_ctx* c, const fgl_arbprog* p, int stage, float* out)
{
    for (int i = 0; i < p->nslots; i++) {
        const arb_slot* s = &p->slots[i];
        float*          o = out + 4 * i;
        if (s->kind == SLOT_CONST) memcpy(o, s->v, 16);
        else if (s->kind == SLOT_ENV) memcpy(o, c->arb_env[stage][s->index], 16);
        else if (s->kind == SLOT_LOCAL) memcpy(o, p->local[s->index], 16);
        else arb_state(c, s, o);
    }
    int n = p->nslots;
    if (stage == 0) { /* position invariant: mvp rows */
        arb_slot r;
        memset(&r, 0, sizeof(r));
        r.kind = SLOT_STATE, r.st = ST_MATRIX, r.a = MAT_MVP;
        for (int k = 0; k < 4; k++) r.row = k, arb_state(c, &r, out + 4 * (n + k));
        n += 4;
    } else { /* fog color, fog params */
        arb_slot r;
        memset(&r, 0, sizeof(r));
        r.kind = SLOT_STATE, r.st = ST_FOG_COLOR;
        arb_state(c, &r, out + 4 * n);
        r.st = ST_FOG_PARAMS;
        arb_state(c, &r, out + 4 * (n + 1));
        n += 2;
    }
    return (size_t)n * 16;
}

/* the ARB parameter blocks of the bound pair (gl_draw33.c, for blocks fgl_PV / fgl_PF) */
size_t fgl_arb_block(fgl_ctx* c, int stage, float* out, size_t max)
{
    int                ve = (c->enables & FGL_E_VP) != 0, fe = (c->enables & FGL_E_FP) != 0;
    const fgl_arbprog* v  = ve ? fgl_arb_get(c, c->arb_bound[0], 0) : NULL;
    const fgl_arbprog* f  = fe ? fgl_arb_get(c, c->arb_bound[1], 0) : NULL;
    int                ftex = !f && (c->tex2d_units & 1) && c->unit_bind[FGL_TT_2D][0];
    if (!v) v = fgl_arb_fixed(c, 0);
    if (!f) f = fgl_arb_fixed(c, ftex ? 1 : 2);
    const fgl_arbprog* p = stage ? f : v;
    if (!p || (size_t)(p->nslots + 4) * 16 > max) return 0;
    return arb_fill(c, p, stage, out);
}

void fgl_arb_free(fgl_ctx* c)
{
    fgl_arbprog* list = (fgl_arbprog*)c->arb_progs;
    for (int i = 0; i < c->narb; i++) free(list[i].body), free(list[i].decl);
    free(list);
    for (int k = 0; k < 3; k++) {
        fgl_arbprog* f = (fgl_arbprog*)c->arb_fixed[k];
        if (f) free(f->body), free(f->decl), free(f);
    }
    free(c->arb_pairs);
    c->arb_progs = NULL, c->arb_pairs = NULL, c->narb = c->narb_pairs = c->carb_pairs = 0;
}
