/*
 * fatgl - legacy GLSL (1.10 .. 1.30, compatibility profiles) to GLSL 3.30
 * core, so glslang can compile it to SPIR-V.
 *
 * Identifiers are rewritten token by token (comments and #version /
 * #extension lines are left alone): attribute / varying become in / out,
 * texture2D & co. become texture, and the built-in state of the fixed
 * function pipeline becomes ordinary GLSL that fatgl feeds:
 *   - built-in attributes (gl_Vertex, gl_Normal, gl_Color, gl_MultiTexCoordN,
 *     ...) become inputs named fgl_Vertex, ... (fed from the client arrays
 *     or the current values, gl_draw33.c);
 *   - built-in uniforms (matrices, lights, material, fog, ...) live in the
 *     uniform block fgl_Builtins (layout below, filled per draw);
 *   - gl_FrontColor / gl_TexCoord[] / ... become varyings, gl_FragColor /
 *     gl_FragData[] fragment outputs.
 * Only what a shader uses is declared.
 */
#include "fgl.h"
#include <ctype.h>
#include <stdio.h>

typedef struct fgl_sb {
    char*  p;
    size_t n, cap;
    int    fail;
} fgl_sb;

static void sb_put(fgl_sb* b, const char* s, size_t n)
{
    if (b->fail) return;
    if (b->n + n + 1 > b->cap) {
        size_t c = b->cap ? b->cap * 2 : 4096;
        while (c < b->n + n + 1) c *= 2;
        char* q = (char*)realloc(b->p, c);
        if (!q) {
            b->fail = 1;
            return;
        }
        b->p = q, b->cap = c;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}
static void sb_str(fgl_sb* b, const char* s) { sb_put(b, s, strlen(s)); }

/* what the shader uses */
enum {
    U_VERTEX, U_NORMAL, U_COLOR_IN, U_SECCOLOR_IN, U_FOGCOORD_IN, U_MTC0, /* .. U_MTC0 + 7 */
    U_FRONTCOLOR = U_MTC0 + 8, U_BACKCOLOR, U_FRONTSEC, U_BACKSEC, U_TEXCOORD, U_FOGFRAG, U_CLIPVERTEX,
    U_FRAGCOLOR, U_FRAGDATA, U_BUILTINS, U_SHADOW, U_N
};

/* identifier replacements: name, vertex stage text, fragment stage text, use flag */
typedef struct fgl_rw {
    const char* name;
    const char* vs;
    const char* fs;
    int         use;
} fgl_rw;

static const fgl_rw g_rw[] = {
    { "attribute", "in", "in", -1 },
    { "sampler", "fgl_sampler", "fgl_sampler", -1 }, /* keywords of Vulkan GLSL, plain names in GL */
    { "samplerShadow", "fgl_samplerShadow", "fgl_samplerShadow", -1 },
    { "texture1D", "texture", "texture", -1 },
    { "texture1DProj", "textureProj", "textureProj", -1 },
    { "texture1DLod", "textureLod", "textureLod", -1 },
    { "texture2D", "texture", "texture", -1 },
    { "texture2DProj", "textureProj", "textureProj", -1 },
    { "texture2DLod", "textureLod", "textureLod", -1 },
    { "texture2DProjLod", "textureProjLod", "textureProjLod", -1 },
    { "texture2DGrad", "textureGrad", "textureGrad", -1 },
    { "texture2DGradARB", "textureGrad", "textureGrad", -1 },
    { "texture2DLodEXT", "textureLod", "textureLod", -1 },
    { "texture2DGradEXT", "textureGrad", "textureGrad", -1 },
    { "texture3D", "texture", "texture", -1 },
    { "texture3DProj", "textureProj", "textureProj", -1 },
    { "texture3DLod", "textureLod", "textureLod", -1 },
    { "textureCube", "texture", "texture", -1 },
    { "textureCubeLod", "textureLod", "textureLod", -1 },
    { "texture2DRect", "texture", "texture", -1 },
    { "shadow2D", "fgl_shadow2D", "fgl_shadow2D", U_SHADOW },
    { "shadow2DProj", "fgl_shadow2DProj", "fgl_shadow2DProj", U_SHADOW },
    { "shadow2DLod", "fgl_shadow2DLod", "fgl_shadow2DLod", U_SHADOW },
    { "gl_Vertex", "fgl_Vertex", NULL, U_VERTEX },
    { "gl_Normal", "fgl_Normal", NULL, U_NORMAL },
    { "gl_SecondaryColor", "fgl_SecondaryColor", "fgl_FrontSecondaryColor", -2 }, /* stage dependent */
    { "gl_Color", "fgl_Color", "fgl_FrontColor", -3 },
    { "gl_FogCoord", "fgl_FogCoord", NULL, U_FOGCOORD_IN },
    { "gl_MultiTexCoord0", "fgl_MultiTexCoord0", NULL, U_MTC0 },
    { "gl_MultiTexCoord1", "fgl_MultiTexCoord1", NULL, U_MTC0 + 1 },
    { "gl_MultiTexCoord2", "fgl_MultiTexCoord2", NULL, U_MTC0 + 2 },
    { "gl_MultiTexCoord3", "fgl_MultiTexCoord3", NULL, U_MTC0 + 3 },
    { "gl_MultiTexCoord4", "fgl_MultiTexCoord4", NULL, U_MTC0 + 4 },
    { "gl_MultiTexCoord5", "fgl_MultiTexCoord5", NULL, U_MTC0 + 5 },
    { "gl_MultiTexCoord6", "fgl_MultiTexCoord6", NULL, U_MTC0 + 6 },
    { "gl_MultiTexCoord7", "fgl_MultiTexCoord7", NULL, U_MTC0 + 7 },
    { "gl_FrontColor", "fgl_FrontColor", "fgl_FrontColor", U_FRONTCOLOR },
    { "gl_BackColor", "fgl_BackColor", "fgl_BackColor", U_BACKCOLOR },
    { "gl_FrontSecondaryColor", "fgl_FrontSecondaryColor", "fgl_FrontSecondaryColor", U_FRONTSEC },
    { "gl_BackSecondaryColor", "fgl_BackSecondaryColor", "fgl_BackSecondaryColor", U_BACKSEC },
    { "gl_TexCoord", "fgl_TexCoord", "fgl_TexCoord", U_TEXCOORD },
    { "gl_FogFragCoord", "fgl_FogFragCoord", "fgl_FogFragCoord", U_FOGFRAG },
    { "gl_ClipVertex", "fgl_ClipVertex", NULL, U_CLIPVERTEX },
    { "gl_FragColor", NULL, "fgl_FragColor", U_FRAGCOLOR },
    { "gl_FragData", NULL, "fgl_FragData", U_FRAGDATA },
    { "ftransform", "fgl_ftransform", NULL, U_BUILTINS },
    /* built-in uniforms (fgl_Builtins) */
    { "gl_ModelViewMatrix", "fgl_ModelViewMatrix", "fgl_ModelViewMatrix", U_BUILTINS },
    { "gl_ProjectionMatrix", "fgl_ProjectionMatrix", "fgl_ProjectionMatrix", U_BUILTINS },
    { "gl_ModelViewProjectionMatrix", "fgl_ModelViewProjectionMatrix", "fgl_ModelViewProjectionMatrix", U_BUILTINS },
    { "gl_TextureMatrix", "fgl_TextureMatrix", "fgl_TextureMatrix", U_BUILTINS },
    { "gl_NormalMatrix", "mat3(fgl_NormalMatrix)", "mat3(fgl_NormalMatrix)", U_BUILTINS },
    { "gl_ModelViewMatrixInverse", "inverse(fgl_ModelViewMatrix)", "inverse(fgl_ModelViewMatrix)", U_BUILTINS },
    { "gl_ProjectionMatrixInverse", "inverse(fgl_ProjectionMatrix)", "inverse(fgl_ProjectionMatrix)", U_BUILTINS },
    { "gl_ModelViewProjectionMatrixInverse", "inverse(fgl_ModelViewProjectionMatrix)", "inverse(fgl_ModelViewProjectionMatrix)", U_BUILTINS },
    { "gl_ModelViewMatrixTranspose", "transpose(fgl_ModelViewMatrix)", "transpose(fgl_ModelViewMatrix)", U_BUILTINS },
    { "gl_ProjectionMatrixTranspose", "transpose(fgl_ProjectionMatrix)", "transpose(fgl_ProjectionMatrix)", U_BUILTINS },
    { "gl_ModelViewProjectionMatrixTranspose", "transpose(fgl_ModelViewProjectionMatrix)", "transpose(fgl_ModelViewProjectionMatrix)",
      U_BUILTINS },
    { "gl_ModelViewMatrixInverseTranspose", "transpose(inverse(fgl_ModelViewMatrix))", "transpose(inverse(fgl_ModelViewMatrix))",
      U_BUILTINS },
    { "gl_ProjectionMatrixInverseTranspose", "transpose(inverse(fgl_ProjectionMatrix))", "transpose(inverse(fgl_ProjectionMatrix))",
      U_BUILTINS },
    { "gl_ModelViewProjectionMatrixInverseTranspose", "transpose(inverse(fgl_ModelViewProjectionMatrix))",
      "transpose(inverse(fgl_ModelViewProjectionMatrix))", U_BUILTINS },
    { "gl_LightSource", "fgl_LightSource", "fgl_LightSource", U_BUILTINS },
    { "gl_LightModel", "fgl_LightModel", "fgl_LightModel", U_BUILTINS },
    { "gl_FrontMaterial", "fgl_FrontMaterial", "fgl_FrontMaterial", U_BUILTINS },
    { "gl_BackMaterial", "fgl_FrontMaterial", "fgl_FrontMaterial", U_BUILTINS },
    { "gl_FrontLightModelProduct", "fgl_FrontLightModelProduct", "fgl_FrontLightModelProduct", U_BUILTINS },
    { "gl_BackLightModelProduct", "fgl_FrontLightModelProduct", "fgl_FrontLightModelProduct", U_BUILTINS },
    { "gl_FrontLightProduct", "fgl_FrontLightProduct", "fgl_FrontLightProduct", U_BUILTINS },
    { "gl_BackLightProduct", "fgl_FrontLightProduct", "fgl_FrontLightProduct", U_BUILTINS },
    { "gl_Fog", "fgl_Fog", "fgl_Fog", U_BUILTINS },
    { "gl_Point", "fgl_Point", "fgl_Point", U_BUILTINS },
    { "gl_ClipPlane", "fgl_ClipPlane", "fgl_ClipPlane", U_BUILTINS },
    { "gl_TextureEnvColor", "fgl_TextureEnvColor", "fgl_TextureEnvColor", U_BUILTINS },
    { "gl_MaxLights", "8", "8", -1 },
    { "gl_MaxTextureCoords", "8", "8", -1 },
    { "gl_MaxTextureUnits", "8", "8", -1 },
    { "gl_MaxClipPlanes", "8", "8", -1 },
};

/* the block fatgl fills (fgl_builtins_fill below writes the same layout) */
static const char* g_builtins =
    "struct fgl_LightSourceParameters { vec4 ambient; vec4 diffuse; vec4 specular; vec4 position; vec4 halfVector;\n"
    "    vec3 spotDirection; float spotExponent; float spotCutoff; float spotCosCutoff; float constantAttenuation;\n"
    "    float linearAttenuation; float quadraticAttenuation; };\n"
    "struct fgl_MaterialParameters { vec4 emission; vec4 ambient; vec4 diffuse; vec4 specular; float shininess; };\n"
    "struct fgl_LightModelParameters { vec4 ambient; };\n"
    "struct fgl_LightModelProducts { vec4 sceneColor; };\n"
    "struct fgl_LightProducts { vec4 ambient; vec4 diffuse; vec4 specular; };\n"
    "struct fgl_FogParameters { vec4 color; float density; float start; float end; float scale; };\n"
    "struct fgl_PointParameters { float size; float sizeMin; float sizeMax; float fadeThresholdSize;\n"
    "    float distanceConstantAttenuation; float distanceLinearAttenuation; float distanceQuadraticAttenuation; };\n"
    "layout(std140) uniform fgl_Builtins {\n"
    "    mat4 fgl_ModelViewMatrix; mat4 fgl_ProjectionMatrix; mat4 fgl_ModelViewProjectionMatrix; mat4 fgl_NormalMatrix;\n"
    "    mat4 fgl_TextureMatrix[8];\n"
    "    fgl_LightSourceParameters fgl_LightSource[8];\n"
    "    fgl_LightModelParameters fgl_LightModel;\n"
    "    fgl_MaterialParameters fgl_FrontMaterial;\n"
    "    fgl_LightModelProducts fgl_FrontLightModelProduct;\n"
    "    fgl_LightProducts fgl_FrontLightProduct[8];\n"
    "    fgl_FogParameters fgl_Fog;\n"
    "    fgl_PointParameters fgl_Point;\n"
    "    vec4 fgl_ClipPlane[8];\n"
    "    vec4 fgl_TextureEnvColor[8];\n"
    "};\n";

static int is_id0(int ch) { return isalpha(ch) || ch == '_'; }
static int is_id(int ch) { return isalnum(ch) || ch == '_'; }

/* the #version line: number and profile; returns 1 if the shader needs the rewrite */
static int fgl_legacy_version(const char* s, int* ver, int* compat)
{
    *ver = 110, *compat = 0;
    const char* p = s;
    for (;;) { /* skip blanks and comments before #version */
        while (*p && isspace((unsigned char)*p)) p++;
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') p++;
        } else if (p[0] == '/' && p[1] == '*') {
            const char* e = strstr(p + 2, "*/");
            p             = e ? e + 2 : p + strlen(p);
        } else {
            break;
        }
    }
    if (*p == '#') {
        const char* q = p + 1;
        while (*q == ' ' || *q == '\t') q++;
        if (!strncmp(q, "version", 7)) {
            q += 7;
            *ver = atoi(q);
            while (*q && *q != '\n') {
                if (!strncmp(q, "compatibility", 13)) *compat = 1;
                q++;
            }
        }
    }
    if (*ver == 100 || (*ver >= 300 && strstr(s, " es"))) return 0; /* GLSL ES: glslang compiles it */
    /* every desktop shader: older ones need the rewrite, newer ones often
     * use what GL drivers accept anyway (compatibility built ins in core
     * versions, redeclared built ins, identifiers that are Vulkan keywords) */
    return 1;
}

/* a line that only redeclares a built in variable, as GL drivers accept and
 * glslang does not: [layout(...)] [qualifiers] in|out|varying|attribute|uniform type gl_Name[...]; */
static int fgl_builtin_redecl(const char* p)
{
    char w[64];
    int  storage = 0;
    for (;;) {
        while (*p == ' ' || *p == '\t') p++;
        if (!strncmp(p, "layout", 6)) {
            const char* e = strchr(p, ')');
            if (!e) return 0;
            p = e + 1;
            continue;
        }
        int n = 0;
        while ((isalnum((unsigned char)*p) || *p == '_') && n < 63) w[n++] = *p++;
        w[n] = 0;
        if (!n) return 0;
        if (!strncmp(w, "gl_", 3)) {
            if (!storage) return 0;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '[') {
                const char* e = strchr(p, ']');
                if (!e) return 0;
                p = e + 1;
                while (*p == ' ' || *p == '\t') p++;
            }
            if (*p != ';') return 0;
            p++;
            while (*p == ' ' || *p == '\t' || *p == '\r') p++;
            return *p == '\n' || !*p || (p[0] == '/' && p[1] == '/');
        }
        if (!strcmp(w, "in") || !strcmp(w, "out") || !strcmp(w, "varying") || !strcmp(w, "attribute") || !strcmp(w, "uniform"))
            storage = 1;
        /* other words: qualifiers or the type */
    }
}

/* the source after #version (and the #extension lines, which must stay first) */
/* gl_TexCoord elements a shader touches: highest literal index + 1, 8 for
 * a dynamic index, 0 if unused */
int fgl_glsl_texcoords(const char* src)
{
    int n = 0;
    for (const char* p = src; (p = strstr(p, "gl_TexCoord")) != NULL; p += 11) {
        if (p > src && is_id((unsigned char)p[-1])) continue;
        if (is_id((unsigned char)p[11])) continue;
        const char* q   = p + 11;
        int         idx = 7;
        while (*q == ' ' || *q == '\t') q++;
        if (*q == '[') {
            q++;
            while (*q == ' ') q++;
            if (isdigit((unsigned char)*q)) {
                idx = atoi(q);
                while (isdigit((unsigned char)*q)) q++;
                while (*q == ' ') q++;
                if (*q != ']') idx = 7;
            }
        }
        idx = idx < 0 ? 0 : (idx > 7 ? 7 : idx);
        n   = idx + 1 > n ? idx + 1 : n;
    }
    return n;
}

char* fgl_glsl_upgrade(const char* src, GLenum stage, int ntexcoords, int* uses_builtins)
{
    int ver, compat;
    *uses_builtins = 0;
    if (!fgl_legacy_version(src, &ver, &compat)) return NULL;
    int    vs = stage == GL_VERTEX_SHADER;
    int    use[U_N];
    fgl_sb body, ext, out;
    memset(use, 0, sizeof(use));
    memset(&body, 0, sizeof(body));
    memset(&ext, 0, sizeof(ext));
    memset(&out, 0, sizeof(out));
    const char* p    = src;
    int         bol  = 1; /* at the beginning of a line */
    int         line = 1, first_line = 1;
    while (*p) {
        if (bol) { /* #version / #extension lines */
            const char* q = p;
            while (*q == ' ' || *q == '\t') q++;
            if (*q == '#') {
                const char* d = q + 1;
                while (*d == ' ' || *d == '\t') d++;
                int isver = !strncmp(d, "version", 7), isext = !strncmp(d, "extension", 9);
                if (isver || isext) {
                    const char* e = strchr(q, '\n');
                    size_t      n = e ? (size_t)(e - q) : strlen(q);
                    if (isext) sb_put(&ext, q, n), sb_str(&ext, "\n");
                    sb_str(&body, "\n"); /* keep the line numbers */
                    p = e ? e + 1 : q + n;
                    line++;
                    if (isver) first_line = line;
                    continue;
                }
            }
        }
        if (bol && fgl_builtin_redecl(p)) { /* out vec4 gl_Position; and friends: dropped */
            const char* e = strchr(p, '\n');
            sb_str(&body, "\n");
            p = e ? e + 1 : p + strlen(p);
            line++;
            continue;
        }
        bol = 0;
        if (p[0] == '/' && p[1] == '/') {
            const char* e = strchr(p, '\n');
            size_t      n = e ? (size_t)(e - p) : strlen(p);
            sb_put(&body, p, n);
            p += n;
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            const char* e = strstr(p + 2, "*/");
            size_t      n = e ? (size_t)(e + 2 - p) : strlen(p);
            for (size_t i = 0; i < n; i++) line += p[i] == '\n';
            sb_put(&body, p, n);
            p += n;
            continue;
        }
        if (is_id0((unsigned char)*p) && (p == src || !is_id((unsigned char)p[-1]))) {
            const char* e = p;
            while (is_id((unsigned char)*e)) e++;
            size_t      n   = (size_t)(e - p);
            const char* rep = NULL;
            if (n == 7 && !strncmp(p, "varying", 7)) rep = vs ? "out" : "in";
            for (size_t k = 0; !rep && k < sizeof(g_rw) / sizeof(g_rw[0]); k++) {
                if (strlen(g_rw[k].name) != n || strncmp(g_rw[k].name, p, n)) continue;
                rep     = vs ? g_rw[k].vs : g_rw[k].fs;
                int u   = g_rw[k].use;
                if (u == -2) u = vs ? U_SECCOLOR_IN : U_FRONTSEC;
                if (u == -3) u = vs ? U_COLOR_IN : U_FRONTCOLOR;
                if (rep && u >= 0) use[u] = 1;
                if (!rep) rep = g_rw[k].name; /* not in this stage: glslang reports it */
            }
            if (rep) sb_str(&body, rep);
            else sb_put(&body, p, n);
            p = e;
            continue;
        }
        if (*p == '\n') line++, bol = 1;
        sb_put(&body, p, 1);
        p++;
    }
    (void)first_line;
    if (use[U_CLIPVERTEX]) use[U_BUILTINS] = 1;
    if (vs && use[U_BUILTINS]) use[U_VERTEX] = 1; /* ftransform() */
    if (use[U_TEXCOORD] || use[U_FRONTCOLOR] || use[U_BACKCOLOR] || use[U_FRONTSEC] || use[U_BACKSEC] || use[U_FOGFRAG]) {
        /* varyings: declared in both stages so they link by name */
    }

    /* the new source: #version, extensions, declarations, the body */
    char vline[64];
    snprintf(vline, sizeof(vline), "#version %d core\n", ver > 330 ? ver : 330);
    sb_str(&out, vline);
    if (ext.p) sb_str(&out, ext.p);
    if (use[U_BUILTINS]) sb_str(&out, g_builtins), *uses_builtins = 1;
    if (use[U_SHADOW]) {
        sb_str(&out, "vec4 fgl_shadow2D(sampler2DShadow s, vec3 c) { return vec4(texture(s, c)); }\n"
                     "vec4 fgl_shadow2DProj(sampler2DShadow s, vec4 c) { return vec4(textureProj(s, c)); }\n"
                     "vec4 fgl_shadow2DLod(sampler2DShadow s, vec3 c, float l) { return vec4(textureLod(s, c, l)); }\n");
    }
    const char* io = vs ? "out" : "in";
    char        d[160];
    if (vs) {
        if (use[U_VERTEX]) sb_str(&out, "in vec4 fgl_Vertex;\n");
        if (use[U_NORMAL]) sb_str(&out, "in vec3 fgl_Normal;\n");
        if (use[U_COLOR_IN]) sb_str(&out, "in vec4 fgl_Color;\n");
        if (use[U_SECCOLOR_IN]) sb_str(&out, "in vec4 fgl_SecondaryColor;\n");
        if (use[U_FOGCOORD_IN]) sb_str(&out, "in float fgl_FogCoord;\n");
        for (int k = 0; k < 8; k++)
            if (use[U_MTC0 + k]) snprintf(d, sizeof(d), "in vec4 fgl_MultiTexCoord%d;\n", k), sb_str(&out, d);
        if (use[U_CLIPVERTEX]) sb_str(&out, "vec4 fgl_ClipVertex;\n");
        if (use[U_BUILTINS]) sb_str(&out, "vec4 fgl_ftransform() { return fgl_ModelViewProjectionMatrix * fgl_Vertex; }\n");
    } else {
        if (use[U_FRAGCOLOR]) sb_str(&out, "layout(location = 0) out vec4 fgl_FragColor;\n");
        if (use[U_FRAGDATA]) sb_str(&out, "layout(location = 0) out vec4 fgl_FragData[1];\n");
    }
    if (use[U_FRONTCOLOR]) snprintf(d, sizeof(d), "%s vec4 fgl_FrontColor;\n", io), sb_str(&out, d);
    if (use[U_BACKCOLOR]) snprintf(d, sizeof(d), "%s vec4 fgl_BackColor;\n", io), sb_str(&out, d);
    if (use[U_FRONTSEC]) snprintf(d, sizeof(d), "%s vec4 fgl_FrontSecondaryColor;\n", io), sb_str(&out, d);
    if (use[U_BACKSEC]) snprintf(d, sizeof(d), "%s vec4 fgl_BackSecondaryColor;\n", io), sb_str(&out, d);
    /* both stages declare the same size (the program's largest use; 8 when unknown) */
    if (use[U_TEXCOORD])
        snprintf(d, sizeof(d), "%s vec4 fgl_TexCoord[%d];\n", io, ntexcoords > 0 && ntexcoords <= 8 ? ntexcoords : 8), sb_str(&out, d);
    if (use[U_FOGFRAG]) snprintf(d, sizeof(d), "%s float fgl_FogFragCoord;\n", io), sb_str(&out, d);
    sb_str(&out, "#line 1\n");
    if (body.p) sb_str(&out, body.p);
    free(body.p);
    free(ext.p);
    if (out.fail) {
        free(out.p);
        return NULL;
    }
    return out.p;
}

/* ---- the fgl_Builtins block (std140) ---- */
static void put_mat(uint8_t* b, size_t off, const fm_mat4* m) { memcpy(b + off, m, 64); }
static void put_v4(uint8_t* b, size_t off, const float* v) { memcpy(b + off, v, 16); }
static void put_f(uint8_t* b, size_t off, float v) { memcpy(b + off, &v, 4); }

static fm_mat4 fgl_mul(const fm_mat4* a, const fm_mat4* b)
{
    fm_mat4      r;
    const float* A = &a->c[0].x;
    const float* B = &b->c[0].x;
    float*       R = &r.c[0].x;
    for (int c = 0; c < 4; c++)
        for (int k = 0; k < 4; k++) R[c * 4 + k] = A[k] * B[c * 4] + A[4 + k] * B[c * 4 + 1] + A[8 + k] * B[c * 4 + 2] + A[12 + k] * B[c * 4 + 3];
    return r;
}

/* offsets of the block declared above */
#define FB_MV        0
#define FB_PROJ      64
#define FB_MVP       128
#define FB_NORMAL    192
#define FB_TEXM      256
#define FB_LIGHT     (FB_TEXM + 8 * 64) /* 8 x 128 */
#define FB_LMODEL    (FB_LIGHT + 8 * 128)
#define FB_MATERIAL  (FB_LMODEL + 16) /* 80 */
#define FB_SCENE     (FB_MATERIAL + 80)
#define FB_LPRODUCT  (FB_SCENE + 16) /* 8 x 48 */
#define FB_FOG       (FB_LPRODUCT + 8 * 48) /* 32 */
#define FB_POINT     (FB_FOG + 32) /* 28 -> 32 */
#define FB_CLIP      (FB_POINT + 32)
#define FB_TEXENV    (FB_CLIP + 8 * 16)
#define FB_SIZE      (FB_TEXENV + 8 * 16)

int fgl_builtins_size(void) { return FB_SIZE; }

void fgl_builtins_fill(fgl_ctx* c, uint8_t* b)
{
    memset(b, 0, FB_SIZE);
    const fm_mat4* mv = &c->mstack[FGL_MV][c->msp[FGL_MV]];
    const fm_mat4* pr = &c->mstack[FGL_PROJ][c->msp[FGL_PROJ]];
    fm_mat4        mvp = fgl_mul(pr, mv);
    put_mat(b, FB_MV, mv);
    put_mat(b, FB_PROJ, pr);
    put_mat(b, FB_MVP, &mvp);
    /* normal matrix: inverse transpose of the upper 3x3, as 3 vec4 columns */
    const float* m = &mv->c[0].x;
    float        a00 = m[0], a01 = m[4], a02 = m[8], a10 = m[1], a11 = m[5], a12 = m[9], a20 = m[2], a21 = m[6], a22 = m[10];
    float        det = a00 * (a11 * a22 - a12 * a21) - a01 * (a10 * a22 - a12 * a20) + a02 * (a10 * a21 - a11 * a20);
    float        id  = det != 0 ? 1.0f / det : 0.0f;
    /* inverse transpose = cofactor matrix / det (C_ij of row i, column j), stored column major */
    float c00 = a11 * a22 - a12 * a21, c01 = -(a10 * a22 - a12 * a20), c02 = a10 * a21 - a11 * a20;
    float c10 = -(a01 * a22 - a02 * a21), c11 = a00 * a22 - a02 * a20, c12 = -(a00 * a21 - a01 * a20);
    float c20 = a01 * a12 - a02 * a11, c21 = -(a00 * a12 - a02 * a10), c22 = a00 * a11 - a01 * a10;
    float n[12] = { c00 * id, c10 * id, c20 * id, 0, c01 * id, c11 * id, c21 * id, 0, c02 * id, c12 * id, c22 * id, 0 };
    memcpy(b + FB_NORMAL, n, sizeof(n));
    put_mat(b, FB_TEXM, &c->mstack[FGL_TEXM][c->msp[FGL_TEXM]]);
    fm_mat4 ident = fm_mat4_identity();
    for (int k = 1; k < 8; k++) put_mat(b, FB_TEXM + (size_t)k * 64, &ident);
    for (int i = 0; i < FGL_LIGHTS; i++) {
        size_t o = FB_LIGHT + (size_t)i * 128;
        put_v4(b, o, c->light[i].amb);
        put_v4(b, o + 16, c->light[i].dif);
        put_v4(b, o + 32, c->light[i].spe);
        put_v4(b, o + 48, c->light[i].pos);
        float hv[4] = { c->light[i].pos[0], c->light[i].pos[1], c->light[i].pos[2] + 1.0f, 1.0f }; /* infinite viewer */
        float l     = sqrtf(hv[0] * hv[0] + hv[1] * hv[1] + hv[2] * hv[2]);
        if (l > 0) hv[0] /= l, hv[1] /= l, hv[2] /= l;
        put_v4(b, o + 64, hv);
        memcpy(b + o + 80, c->light[i].spot_dir, 12);
        put_f(b, o + 92, c->light[i].spot_exp);
        put_f(b, o + 96, c->light[i].spot_cut);
        put_f(b, o + 100, cosf(c->light[i].spot_cut * 3.14159265f / 180.0f));
        put_f(b, o + 104, c->light[i].att[0]);
        put_f(b, o + 108, c->light[i].att[1]);
        put_f(b, o + 112, c->light[i].att[2]);
        size_t q   = FB_LPRODUCT + (size_t)i * 48;
        float  p[4];
        for (int k = 0; k < 4; k++) p[k] = c->light[i].amb[k] * c->mat.amb[k];
        put_v4(b, q, p);
        for (int k = 0; k < 4; k++) p[k] = c->light[i].dif[k] * c->mat.dif[k];
        put_v4(b, q + 16, p);
        for (int k = 0; k < 4; k++) p[k] = c->light[i].spe[k] * c->mat.spe[k];
        put_v4(b, q + 32, p);
    }
    put_v4(b, FB_LMODEL, c->light_model_ambient);
    put_v4(b, FB_MATERIAL, c->mat.emi);
    put_v4(b, FB_MATERIAL + 16, c->mat.amb);
    put_v4(b, FB_MATERIAL + 32, c->mat.dif);
    put_v4(b, FB_MATERIAL + 48, c->mat.spe);
    put_f(b, FB_MATERIAL + 64, c->mat.shin);
    float sc[4];
    for (int k = 0; k < 3; k++) sc[k] = c->mat.emi[k] + c->mat.amb[k] * c->light_model_ambient[k];
    sc[3] = c->mat.dif[3];
    put_v4(b, FB_SCENE, sc);
    put_f(b, FB_FOG + 16, 1.0f); /* density */
    put_f(b, FB_FOG + 24, 1.0f); /* end */
    put_f(b, FB_FOG + 28, 1.0f); /* scale = 1 / (end - start) */
    put_f(b, FB_POINT, c->point_size);
    put_f(b, FB_POINT + 4, 1.0f);
    put_f(b, FB_POINT + 8, 64.0f);
    put_f(b, FB_POINT + 16, 1.0f);
}
