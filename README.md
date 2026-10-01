# fatgl

An OpenGL implementation for Windows on top of the
[fatmap](https://github.com/xls/fatmap) software rasterizer, built as a
drop in `opengl32.dll`.

Put fatgl's `opengl32.dll` next to an application's `.exe` and the
application renders through fatmap: no relinking, no driver. It exports
the same 336 OpenGL 1.1 functions and WGL functions as the system DLL
(gdi32's `ChoosePixelFormat` / `SetPixelFormat` / `SwapBuffers` route into
it), and everything newer comes through `wglGetProcAddress` (GL 1.2 .. 3.3
core, 196 ARB / EXT / KHR aliases of promoted functions), as GL loaders
(GLEW, glad, GLFW) expect.

## Status

| | |
|---|---|
| WGL | pixel formats (RGBA8, depth 24, stencil 8, double / single buffered), contexts, `WGL_ARB_create_context(_profile)`, `WGL_ARB_pixel_format`, `WGL_EXT_swap_control`, presentation with GDI |
| GL 1.x fixed function | immediate mode (all primitives except points / lines), client vertex arrays, display lists, matrix stacks, lighting (8 lights, materials, color material), 2D textures (RGB(A), BGR(A), luminance, alpha; mipmaps by fatmap), texenv, depth / alpha test, culling, scissor, polygon offset, common blend functions, `glReadPixels` |
| not yet | points / lines, GLSL (GL 2.0+ shaders) and the GL 3.x object model (buffers, VAOs, FBOs, UBOs), fog, two sided lighting |

Unimplemented entry points exist (so loaders and applications link) and
report `GL_INVALID_OPERATION`; set `FATGL_VERBOSE=1` to see them on stderr
(they also go to `OutputDebugString`).

### Plan

* OpenGL 2.1 / 3.3 (core and compatibility profiles): GLSL through glslang
  (vendored with a Meson build) to SPIR-V, run by fatmap's SPIR-V backend;
  buffer objects, VAOs, FBOs, UBOs. Needs in fatmap: SPIR-V function calls,
  several uniform blocks, GL blending (`glBlendFunc` / `glBlendEquation` on
  straight alpha), straight alpha texture formats, more render target
  formats.
* Not planned: OpenGL 4.x (tessellation and double precision shaders are
  outside fatmap's pipeline); 3.2's geometry shaders come later, if at all.

## Building

Meson + Ninja, Windows (MinGW-w64; MSVC later):

```sh
meson setup build
ninja -C build
build\gears.exe          # fatgl's opengl32.dll sits next to it
```

fatmap comes from its release package (`subprojects/fatmap.wrap`, v0.2.0),
or from a local install when `PKG_CONFIG_PATH` points at one. The examples
link against the system `opengl32` import library on purpose: Windows
loads the DLL next to the executable, which is fatgl's.

`examples/*.exe --shot file.bmp` renders 30 frames, saves the last one and
quits.

`tools/gen_gl.py` regenerates the export list, the `wglGetProcAddress`
table and the stubs after adding GL functions.

## License

MIT. The Khronos headers in `include/` are MIT licensed (Khronos Group).
