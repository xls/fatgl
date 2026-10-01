"""The interaction benchmark (bench_interaction.c / bench_scene.h) on Mesa
llvmpipe: headless EGL through moderngl, same GLSL, same textures, mesh and
uniforms. Run in a linux container:
  LP_NUM_THREADS=1 EGL_PLATFORM=surfaceless python3 bench_interaction.py [frames]
Writes bench_interaction_llvmpipe.ppm next to this file."""
import os
import struct
import sys
import time

import moderngl
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
W, H, GX, GY, LIGHTS = 1280, 720, 64, 36, 4
M32 = 0xFFFFFFFF


def hsh(x, y, k):
    h = ((x * 73856093) ^ (y * 19349663) ^ (k * 83492791)) & M32
    h ^= h >> 13
    h = (h * 0x5BD1E995) & M32
    h ^= h >> 15
    return h


def tex_size(i):
    return (64, 16) if i == 1 else ((128, 128) if i == 2 else (256, 256))


def texture(i):
    w, h = tex_size(i)
    out = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            r = hsh(x, y, i)
            if i == 0:
                p = (128, 96 + (r & 63), 255, 96 + ((r >> 8) & 63))
            elif i == 1:
                v = max(0, 255 - 4 * x)
                p = (v, v, v, 255)
            elif i == 2:
                d = (x - 64) * (x - 64) + (y - 64) * (y - 64)
                v = max(0, 255 - (d * 255) // 4096)
                p = (v, (v * 3) // 4, v // 2, 255)
            elif i == 3:
                p = (120 + (r & 15), 120 + ((r >> 4) & 15), 8, 64 + ((x ^ y) & 127))
            else:
                v = 64 + (r & 63)
                p = (v, v, v, 255)
            o = (y * w + x) * 4
            out[o:o + 4] = bytes(p)
    return bytes(out)


def mesh():
    f32 = np.float32
    verts = bytearray()
    for j in range(GY + 1):
        for i in range(GX + 1):
            x = f32(-1.0) + f32(2.0) * f32(i) / f32(GX)
            y = f32(-1.0) + f32(2.0) * f32(j) / f32(GY)
            z = f32(0.02) * f32((i * 7 + j * 3) % 11) / f32(11.0)
            verts += struct.pack("<6f", x, y, z, 1.0, f32(i) / f32(GX), f32(j) / f32(GY))
            verts += bytes((120 + (i * 5 + j) % 17, 120 + (j * 3 + i) % 17, 250, 0, 255, 128, 128, 255, 255, 255, 255, 255))
    idx = []
    for j in range(GY):
        for i in range(GX):
            a = j * (GX + 1) + i
            b, c = a + 1, a + GX + 1
            d = c + 1
            idx += [a, b, d, a, d, c]
    return bytes(verts), np.array(idx, dtype="u4").tobytes()


def uniforms(l):
    lp = [(-0.5, -0.4), (0.5, -0.3), (-0.3, 0.5), (0.4, 0.4)]
    lc = [(1.0, 0.8, 0.6), (0.6, 0.8, 1.0), (0.8, 1.0, 0.7), (1.0, 1.0, 1.0)]
    f32 = np.float32
    lx, ly = f32(lp[l][0]), f32(lp[l][1])
    va = [(lx, ly, 0.6, 1.0), (0.0, 0.0, 3.0, 1.0), (0.6, 0.0, 0.0, f32(0.5) - f32(0.6) * lx), (0.0, 0.6, 0.0, f32(0.5) - f32(0.6) * ly),
          (0.0, 0.0, 0.0, 1.0), (0.0, 0.0, 10.0, 0.3), (4.0, 0.0, 0.0, 0.0), (0.0, 4.0, 0.0, 0.0), (4.0, 0.0, 0.0, 0.0),
          (0.0, 4.0, 0.0, 0.0), (4.0, 0.0, 0.0, 0.0), (0.0, 4.0, 0.0, 0.0), (0.0, 0.0, 0.0, 0.0), (1.0, 1.0, 1.0, 1.0),
          (1.0, 0.0, 0.0, 0.0), (0.0, 1.0, 0.0, 0.0), (0.0, 0.0, 0.5, 0.0), (0.0, 0.0, 0.0, 1.0)]
    fa = [tuple(lc[l]) + (1.0,), tuple(f32(0.8) * f32(c) for c in lc[l]) + (1.0,)]
    return va, fa


ctx = moderngl.create_standalone_context(backend="egl", libgl="libGL.so.1", libegl="libEGL.so.1")
print("renderer:", ctx.info["GL_RENDERER"], "| LP_NUM_THREADS =", os.environ.get("LP_NUM_THREADS", "default"))
prog = ctx.program(vertex_shader=open(os.path.join(HERE, "interaction.vert")).read(),
                   fragment_shader=open(os.path.join(HERE, "interaction.frag")).read())
texs = []
for i in range(5):
    w, h = tex_size(i)
    t = ctx.texture((w, h), 4, texture(i))
    t.build_mipmaps()
    t.filter = (moderngl.LINEAR_MIPMAP_LINEAR, moderngl.LINEAR)
    t.repeat_x = t.repeat_y = i not in (1, 2)
    if i in (1, 2):  # GL_CLAMP_TO_BORDER, black
        t.border_color = (0.0, 0.0, 0.0, 0.0)
    t.use(location=i)
    prog["samp%d" % i].value = i
    texs.append(t)
vdata, idata = mesh()
vbo, ibo = ctx.buffer(vdata), ctx.buffer(idata)
vao = ctx.vertex_array(prog, [(vbo, "4f 2f 4f1 4f1 4f1", "in_Position", "in_TexCoord", "in_Normal", "in_Tangent", "in_Color")],
                       index_buffer=ibo, index_element_size=4)
fbo = ctx.simple_framebuffer((W, H))
fbo.use()
ctx.disable(moderngl.DEPTH_TEST)
ctx.enable(moderngl.BLEND)
ctx.blend_func = (moderngl.ONE, moderngl.ONE)


def frame():
    fbo.clear(0.0, 0.0, 0.0, 1.0)
    for l in range(LIGHTS):
        va, fa = uniforms(l)
        prog["_va_"].write(np.array(va, dtype="f4").tobytes())
        prog["_fa_"].write(np.array(fa, dtype="f4").tobytes())
        vao.render(moderngl.TRIANGLES)
    ctx.finish()


frame()  # compile / warm up
frames = int(sys.argv[1]) if len(sys.argv) > 1 else 10
best = 1e30
for _ in range(frames):
    t0 = time.perf_counter()
    frame()
    best = min(best, (time.perf_counter() - t0) * 1000)
print("interaction %dx%d, %d lights: %.2f ms (best of %d)" % (W, H, LIGHTS, best, frames))
img = np.frombuffer(fbo.read(components=3), dtype=np.uint8).reshape(H, W, 3)
print("mean color %.2f %.2f %.2f" % tuple(img.reshape(-1, 3).mean(axis=0)))
with open(os.path.join(HERE, "bench_interaction_llvmpipe.ppm"), "wb") as f:
    f.write(b"P6 %d %d 255\n" % (W, H))
    f.write(img[::-1].tobytes())
