# fatgl

An OpenGL implementation for Windows on top of the
[fatmap](https://github.com/xls/fatmap) software rasterizer, built as a
drop in `opengl32.dll`.

Put fatgl's `opengl32.dll` next to an application's `.exe` and the
application renders through fatmap: no relinking, no driver. It exports
the same 336 OpenGL 1.1 functions and WGL functions as the system DLL
(gdi32's `ChoosePixelFormat` / `SetPixelFormat` / `SwapBuffers` route into
it), and everything newer comes through `wglGetProcAddress` (GL 1.2 .. 3.3
core plus the compatibility profile functions such as `glMultiTexCoord*`, and the ARB / EXT / KHR aliases of promoted functions), as GL loaders
(GLEW, glad, GLFW) expect.

## Status

| | |
|---|---|
| WGL | pixel formats (RGBA8, depth 24, stencil 8, double / single buffered), contexts, `WGL_ARB_create_context(_profile)`, `WGL_ARB_pixel_format`, `WGL_EXT_swap_control`, presentation with GDI |
| GL 1.x fixed function | immediate mode (every primitive, points and lines with `glPointSize` / `glLineWidth`), client vertex arrays, display lists, matrix stacks, lighting (8 lights, materials, color material), multitexture (`GL_ARB_multitexture`: two fixed function texture stages, e.g. texture x lightmap), fog (linear / exp / exp2), 2D textures (RGB(A), BGR(A), luminance, alpha; mipmaps by fatmap), S3TC (DXT1 / 3 / 5) and RGTC compressed textures, every pixel format / type of GL 3.3 (packed, half float, integer), pixel buffer objects, texenv, depth / alpha / stencil test, culling, scissor, polygon offset, `glPolygonMode` (fill / line / point), blending, `glReadPixels`, `glCopyTex(Sub)Image*`, `glGetTexImage` |
| GL 2.0 .. 3.3 | GLSL compiled at run time (glslang inside the DLL, GLSL to SPIR-V, run by fatmap's SPIR-V backend), programs, loose uniforms, uniform blocks (`glBindBufferBase` / `glUniformBlockBinding`), samplers on 16 texture units, every texture target but buffer / multisample (1D, 2D, 3D, cube maps, rectangle, 1D / 2D arrays; cube faces and layers as render targets), texture swizzles, buffer objects (map, copy), vertex array objects (any attribute type, normalized, integer, instance divisors), `glDrawArrays` / `glDrawElements` with instancing and base vertex, `gl_VertexID` / `gl_InstanceID`, `glGetStringi`, framebuffer objects (texture and renderbuffer attachments, depth only passes, `glBlitFramebuffer`), renderbuffers, depth textures, all blend factors and equations (`glBlendFuncSeparate`, `glBlendEquationSeparate`, `glBlendColor`), separate stencil state, points (`gl_PointSize` with `GL_PROGRAM_POINT_SIZE`, `gl_PointCoord`) and every line mode, primitive restart, sampler objects, occlusion / primitive / timer queries, sync objects, `glClearBuffer*`, indexed and 64 bit queries |
| legacy GLSL | GLSL 1.10 .. 1.30 and compatibility profiles: rewritten to 3.30 before glslang (`attribute` / `varying`, `texture2D` & co., `gl_FragColor` / `gl_FragData`, `ftransform`); the fixed function state as GLSL sees it (`gl_Vertex`, `gl_Normal`, `gl_Color`, `gl_MultiTexCoord0`, `gl_ModelViewProjectionMatrix` and friends, `gl_NormalMatrix`, `gl_LightSource[]`, `gl_FrontMaterial`, `gl_LightModel`, `gl_TexCoord[]`, ...), fed from client arrays, immediate mode or the current values |
| not yet | cube map arrays, mipmap levels given by the application (fatmap builds them from level 0), BPTC (BC7) textures, geometry shaders, transform feedback, more than one color output, multisampling, logic ops, float / integer render targets (stored as 8 bit unorm), shadow samplers, fog coordinates, more than two fixed function texture stages, texture coordinate generation, two sided lighting, wide smooth lines |

Shaders: GLSL goes through glslang to SPIR-V, and fatmap compiles the
SPIR-V to x86 machine code (a JIT for x86-64 and x86-32, AVX2 / AVX-512,
16 pixels or vertices per pass). Programs the JIT does not cover, and CPUs
without AVX2, run on fatmap's SPIR-V interpreter. Both produce the same
bits. The JIT is part of fatmap after 0.8.0: build with `-Dfatmap=source`
until the next fatmap release.

Tested with Doom 3 BFG Edition (32 bit, GLSL shaders throughout):
menus, Bink videos, in game rendering, shadow volumes,
render to texture.

Colors are kept the way GL keeps them: straight (not premultiplied) in
textures and framebuffers, rows bottom up, so render to texture, blending
and `glReadPixels` need no conversions.

Unimplemented entry points exist (so loaders and applications link) and
report `GL_INVALID_OPERATION`. fatgl writes `fatgl.log` next to the
executable: the DLL that was loaded (so you can tell it is fatgl), the
contexts the application created, every unimplemented call it made (once
each) and shader compile / link errors with their source. `GL_RENDERER`
reads "fatgl on fatmap ...".

Environment variables:

| | |
|---|---|
| `FATGL_LOG=0` / `FATGL_LOG=<file>` | no `fatgl.log` / write it elsewhere |
| `FATGL_VERBOSE=1` | also print unimplemented calls on stderr |
| `FATGL_THREADS=<n>` | render threads (default: every core) |
| `FATGL_PRECISE_MATH=1` | shader math within 1 ulp instead of GPU-like precision (slower) |
| `FATGL_DUMP_SPIRV=<dir>` | write every linked program's SPIR-V |
| `FATGL_LOAD_SPIRV=<dir>` | use such files instead (e.g. after `spirv-opt`) |
| `FM_JIT=0` | fatmap: run shaders on the interpreter instead of the JIT |

## Performance

`tools/bench`: Doom 3 BFG style light interactions (1280 x 720, 4 additive
light passes, 5 trilinear textures each). The same GLSL and scene run on
Mesa llvmpipe through `bench_interaction.py`. Frame times on a Ryzen 9
9950X3D:

| | 1 thread | 32 threads |
|---|---|---|
| fatgl, x64 | 366 ms | 27.9 ms |
| fatgl, x86 (32 bit) | 479 ms | 31.6 ms |
| Mesa llvmpipe 25.0 (LLVM 19, Linux x64) | 133 ms | 11.0 ms |

Closing that gap is ongoing work (fatmap `docs/BACKLOG.md`).

### Plan

* OpenGL 2.1 / 3.3 (core and compatibility profiles): GLSL through glslang
  (vendored with a Meson build) to SPIR-V, run by fatmap's SPIR-V backend;
  buffer objects, VAOs, FBOs, UBOs, points / lines, legacy GLSL (done).
  Next: multiple render targets, shadow samplers, more render target formats.
* Not planned: OpenGL 4.x (tessellation and double precision shaders are
  outside fatmap's pipeline); 3.2's geometry shaders come later, if at all.

## Building

Meson + Ninja, Windows, MinGW-w64 or Visual Studio (64 bit or 32 bit):

```sh
meson setup build
ninja -C build
build\gears.exe          # fatgl's opengl32.dll sits next to it
build\cube33.exe         # OpenGL 3.3 core profile: VAO, VBO / EBO, GLSL 330, UBO, texture
build\fbo33.exe          # render to texture: framebuffer object, renderbuffer, blit
build\blend.exe          # every blend equation, constant color, straight alpha textures
build\prims.exe          # points and lines: fixed function and GLSL point sprites
build\legacy.exe         # GLSL 1.10 reading the fixed function state, next to the fixed pipeline
```

32 bit (most games of the GL 1.x era are 32 bit processes, which cannot
load a 64 bit DLL): from a Visual Studio x86 prompt (`vcvarsall.bat x86`),
`meson setup build-x86 -Dfatmap=source` then `ninja -C build-x86`. Visual
Studio builds link the C runtime statically: the DLL needs only GDI32,
USER32 and KERNEL32. `meson test` runs `tests/gl_test.c` through the
built DLL (a hidden window).

fatmap comes from its release package (`subprojects/fatmap.wrap`, v0.8.0),
or with `-Dfatmap=source` from a fatmap checkout at
`subprojects/fatmap-src` (a junction / symlink) when developing both.
glslang (GLSL to SPIR-V) comes from
`subprojects/glslang.wrap` with fatgl's own Meson build of it. The examples
link against the system `opengl32` import library on purpose: Windows
loads the DLL next to the executable, which is fatgl's.

`examples/*.exe --shot file.bmp` renders 30 frames, saves the last one and
quits.

`tools/gen_gl.py` regenerates the export list, the `wglGetProcAddress`
table and the stubs after adding GL functions.

## License

MIT. The Khronos headers in `include/` are MIT licensed (Khronos Group).
