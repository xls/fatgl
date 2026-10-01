#!/usr/bin/env python3
"""
Generate fatgl's entry point plumbing (run after adding GL functions):

  python tools/gen_gl.py

Reads
  tools/gl11_functions.txt   the 335 OpenGL 1.1 functions opengl32.dll exports
                             (prototypes from MinGW-w64's GL/gl.h, public domain)
  include/GL/glcorearb.h     core prototypes of GL 1.2 .. 3.3 (Khronos, MIT)
  include/GL/glext.h         ARB / EXT / KHR names of promoted functions (aliases)
  src/*.c                    implemented functions: `APIENTRY glName(`
Writes
  src/gen/opengl32.def       exports: GL 1.1 + WGL
  src/gen/gl_stubs.c         every function up to GL 3.3 that is not implemented
                             yet (records GL_INVALID_OPERATION, warns once)
  src/gen/gl_procs.c         name -> entry point for wglGetProcAddress
  src/gen/gl_decls.h         GL 1.1 prototypes (glcorearb.h has the core ones)
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAX_VERSION = (3, 3)

WGL_EXPORTS = [
    "wglCopyContext", "wglCreateContext", "wglCreateLayerContext", "wglDeleteContext", "wglDescribeLayerPlane",
    "wglGetCurrentContext", "wglGetCurrentDC", "wglGetLayerPaletteEntries", "wglGetProcAddress", "wglMakeCurrent",
    "wglRealizeLayerPalette", "wglSetLayerPaletteEntries", "wglShareLists", "wglSwapLayerBuffers",
    "wglSwapMultipleBuffers", "wglUseFontBitmapsA", "wglUseFontBitmapsW", "wglUseFontOutlinesA",
    "wglUseFontOutlinesW", "wglChoosePixelFormat", "wglDescribePixelFormat", "wglGetPixelFormat",
    "wglSetPixelFormat", "wglSwapBuffers", "wglGetDefaultProcAddress",
]
# WGL extensions reachable through wglGetProcAddress (implemented in src/wgl.c)
WGL_EXT = [
    "wglGetExtensionsStringARB", "wglGetExtensionsStringEXT", "wglCreateContextAttribsARB", "wglChoosePixelFormatARB",
    "wglGetPixelFormatAttribivARB", "wglGetPixelFormatAttribfvARB", "wglSwapIntervalEXT", "wglGetSwapIntervalEXT",
    "wglMakeContextCurrentARB", "wglGetCurrentReadDCARB",
]

PROTO = re.compile(r"^\s*(?:GLAPI\s+)?(.+?)\s*APIENTRY\s+(gl\w+)\s*\((.*)\)\s*;")


def parse_gl11():
    out = []
    for line in open(os.path.join(ROOT, "tools", "gl11_functions.txt")):
        m = PROTO.match(line)
        if m:
            out.append((m.group(2), m.group(1).strip(), m.group(3).strip()))
    return out


def parse_core(header="glcorearb.h"):
    """GL_VERSION_1_2 .. MAX_VERSION prototypes from glcorearb.h (core) or
    glext.h (also the compatibility profile functions: glMultiTexCoord,
    glClientActiveTexture, glSecondaryColor, glFogCoord, glWindowPos, ...)"""
    out, ver = [], None
    for line in open(os.path.join(ROOT, "include", "GL", header)):
        m = re.match(r"#ifndef GL_VERSION_(\d)_(\d)\b", line)
        if m:
            ver = (int(m.group(1)), int(m.group(2)))
            continue
        if line.startswith("#ifndef GL_ARB_") or line.startswith("#ifndef GL_KHR_") or line.startswith("#ifndef GL_EXT_"):
            ver = None
        m = PROTO.match(line)
        if m and line.startswith("GLAPI") and ver and (1, 2) <= ver <= MAX_VERSION:
            out.append((m.group(2), m.group(1).strip(), m.group(3).strip()))
    return out


def parse_aliases(core_names):
    """ARB / EXT / KHR names of functions promoted to core (glext.h): the
    extension entry point is the core one when the suffix-less name is a
    core function with the same parameter types"""
    def sig(args):
        t = re.sub(r"\b(\w+)\s*(?=,|$)", "", args)  # drop parameter names
        t = re.sub(r"(ARB|EXT)\b", "", t)            # GLsizeiptrARB == GLsizeiptr, GLintptrARB, ...
        return re.sub(r"\s+", "", t)
    out = {}
    for line in open(os.path.join(ROOT, "include", "GL", "glext.h")):
        m = PROTO.match(line)
        if not m or not line.startswith("GLAPI"):
            continue
        name = m.group(2)
        for suf in ("ARB", "EXT", "KHR"):
            if name.endswith(suf) and name[: -len(suf)] in core_names:
                base, ret, args = core_names[name[: -len(suf)]]
                if sig(args) == sig(m.group(3)) and ret.replace("ARB", "") == m.group(1).strip().replace("ARB", ""):
                    out[name] = base
    return out


def implemented():
    names = set()
    for f in glob.glob(os.path.join(ROOT, "src", "*.c")):
        for m in re.finditer(r"APIENTRY\s+(gl\w+)\s*\(", open(f).read()):
            names.add(m.group(1))
        for m in re.finditer(r"^FGL_\w+\((gl\w+)\s*,", open(f).read(), re.M):  # macro defined entry points
            names.add(m.group(1))
    return names


def params(args):
    """names and C declarations of a parameter list ('void' -> none)"""
    if args.strip() in ("", "void"):
        return []
    out = []
    for i, p in enumerate(args.split(",")):
        p = p.strip()
        m = re.match(r"(.*?)(\w+)\s*(\[[^\]]*\])?$", p)
        if not m or m.group(1).strip() == "":
            out.append(("a%d" % i, p + " a%d" % i))
        else:
            out.append((m.group(2), p))
    return out


def main():
    gl11 = parse_gl11()
    core = parse_core()
    core_names = {n for n, _, _ in core}
    compat = [f for f in parse_core("glext.h") if f[0] not in core_names]  # compatibility profile only
    impl = implemented()
    seen, funcs = set(), []
    for name, ret, args in gl11 + core + compat:
        if name not in seen:
            seen.add(name)
            funcs.append((name, ret, args))
    gen = os.path.join(ROOT, "src", "gen")
    os.makedirs(gen, exist_ok=True)
    aliases = parse_aliases({n: (n, r, a) for n, r, a in funcs})

    with open(os.path.join(gen, "opengl32.def"), "w", newline="\n") as f:
        f.write("; generated by tools/gen_gl.py: do not edit\nLIBRARY opengl32\nEXPORTS\n")
        for name, _, _ in gl11:
            f.write("    %s\n" % name)
        for name in WGL_EXPORTS:
            f.write("    %s\n" % name)

    # prototypes of the GL 1.1 functions (glcorearb.h has only the core ones)
    def clean(t):
        return t.replace("GLclampf", "GLfloat").replace("GLclampd", "GLdouble").replace("GLvoid", "void")
    with open(os.path.join(gen, "gl_decls.h"), "w", newline="\n") as f:
        f.write("/* generated by tools/gen_gl.py: do not edit. OpenGL 1.1 prototypes and the\n"
                " * compatibility profile functions of 1.2 .. %d.%d (glcorearb.h has the core ones). */\n" % MAX_VERSION)
        f.write("#ifndef FGL_GL_DECLS_H\n#define FGL_GL_DECLS_H\n")
        seen_d = set()
        for name, ret, args in gl11 + compat:
            if name in seen_d:
                continue
            seen_d.add(name)
            f.write("%s APIENTRY %s(%s);\n" % (clean(ret), name, clean(args)))
        f.write("#endif\n")

    stubs = [(n, r, a) for n, r, a in funcs if n not in impl]
    with open(os.path.join(gen, "gl_stubs.c"), "w", newline="\n") as f:
        f.write("/* generated by tools/gen_gl.py: do not edit. GL functions up to %d.%d not\n"
                " * implemented yet: GL_INVALID_OPERATION and a warning (once). */\n" % MAX_VERSION)
        f.write('#include "fgl.h"\n\n')
        for name, ret, args in stubs:
            ret, args = ret.replace("GLclampf", "GLfloat").replace("GLclampd", "GLdouble").replace("GLvoid", "void"), \
                args.replace("GLclampf", "GLfloat").replace("GLclampd", "GLdouble").replace("GLvoid", "void")
            ps = params(args)
            decl = ", ".join(p[1] for p in ps) or "void"
            f.write("%s APIENTRY %s(%s)\n{\n" % (ret, name, decl))
            for pn, _ in ps:
                f.write("    (void)%s;\n" % pn)
            f.write('    fgl_unimplemented("%s");\n' % name)
            if ret != "void":
                f.write("    return (%s)0;\n" % ret)
            f.write("}\n\n")

    with open(os.path.join(gen, "gl_procs.c"), "w", newline="\n") as f:
        f.write("/* generated by tools/gen_gl.py: do not edit. Entry points for\n"
                " * wglGetProcAddress (sorted by name). */\n")
        f.write('#include "fgl.h"\n\n')
        target = {n: n for n, _, _ in funcs}
        target.update({n: n for n in WGL_EXT})
        target.update(aliases)  # glGenBuffersARB -> glGenBuffers, ...
        allp = sorted(target)
        f.write("const fgl_proc fgl_procs[] = {\n")
        for name in allp:
            f.write('    { "%s", (PROC)(void (*)(void))%s },\n' % (name, target[name]))
        f.write("};\nconst int fgl_nprocs = %d;\n" % len(allp))
    print("%d functions (%d GL 1.1, %d compatibility 1.2+), %d implemented, %d stubs; %d ARB / EXT / KHR aliases" %
          (len(funcs), len(gl11), len(compat), len(funcs) - len(stubs), len(stubs), len(aliases)))


if __name__ == "__main__":
    main()
