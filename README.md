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
| GL 1.x fixed function | immediate mode (every primitive, points and lines with `glPointSize` / `glLineWidth`), client vertex arrays, display lists, matrix stacks, lighting (8 lights, materials, color material), multitexture (`GL_ARB_multitexture`: two fixed function texture stages, e.g. texture x lightmap), fog (linear / exp / exp2), 2D textures (RGB(A), BGR(A), luminance, alpha; mipmaps by fatmap), S3TC (DXT1 / 3 / 5) and RGTC compressed textures, every pixel format / type of GL 3.3 (packed, half float, integer), pixel buffer objects, texenv, depth / alpha / stencil test, culling, scissor, polygon offset, blending, `glReadPixels`, `glCopyTex(Sub)Image2D`, `glGetTexImage` |
| GL 2.0 .. 3.3 | GLSL compiled at run time (glslang inside the DLL, GLSL to SPIR-V, run by fatmap's SPIR-V backend), programs, loose uniforms, uniform blocks (`glBindBufferBase` / `glUniformBlockBinding`), samplers on 16 texture units, buffer objects (map, copy), vertex array objects (any attribute type, normalized, integer, instance divisors), `glDrawArrays` / `glDrawElements` with instancing and base vertex, `gl_VertexID` / `gl_InstanceID`, `glGetStringi`, framebuffer objects (texture and renderbuffer attachments, depth only passes, `glBlitFramebuffer`), renderbuffers, depth textures, all blend factors and equations (`glBlendFuncSeparate`, `glBlendEquationSeparate`, `glBlendColor`), separate stencil state, points (`gl_PointSize` with `GL_PROGRAM_POINT_SIZE`, `gl_PointCoord`) and every line mode, primitive restart, sampler objects, occlusion / primitive / timer queries, sync objects, `glClearBuffer*`, indexed and 64 bit queries |
| legacy GLSL | GLSL 1.10 .. 1.30 and compatibility profiles: rewritten to 3.30 before glslang (`attribute` / `varying`, `texture2D` & co., `gl_FragColor` / `gl_FragData`, `ftransform`); the fixed function state as GLSL sees it (`gl_Vertex`, `gl_Normal`, `gl_Color`, `gl_MultiTexCoord0`, `gl_ModelViewProjectionMatrix` and friends, `gl_NormalMatrix`, `gl_LightSource[]`, `gl_FrontMaterial`, `gl_LightModel`, `gl_TexCoord[]`, ...), fed from client arrays, immediate mode or the current values |
| not yet | cube maps, 3D and array textures, BPTC (BC7) textures, geometry shaders, transform feedback, more than one color output, multisampling, per channel color masks, logic ops, float / integer render targets (stored as 8 bit unorm), shadow samplers, fog coordinates, more than two fixed function texture stages, texture coordinate generation, two sided lighting, wide smooth lines |

Colors are kept the way GL keeps them: straight (not premultiplied) in
textures and framebuffers, rows bottom up, so render to texture, blending
and `glReadPixels` need no conversions.

Unimplemented entry points exist (so loaders and applications link) and
report `GL_INVALID_OPERATION`. fatgl writes `fatgl.log` next to the
executable: the DLL that was loaded (so you can tell it is fatgl), the
contexts the application created, every unimplemented call it made (once
each) and shader compile / link errors with their source.
`FATGL_LOG=0` turns it off, `FATGL_LOG=<file>` writes elsewhere,
`FATGL_VERBOSE=1` also prints unimplemented calls on stderr. `GL_RENDERER`
reads "fatgl on fatmap ...".

### Plan

* OpenGL 2.1 / 3.3 (core and compatibility profiles): GLSL through glslang
  (vendored with a Meson build) to SPIR-V, run by fatmap's SPIR-V backend;
  buffer objects, VAOs, FBOs, UBOs, points / lines, legacy GLSL (done).
  Next: cube maps, 3D / array textures, compressed (S3TC) textures,
  multiple render targets, more render target formats.
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

fatmap comes from its release package (`subprojects/fatmap.wrap`, v0.5.0),
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
