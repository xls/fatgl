project('fatgl', 'c', 'cpp',
  version : '0.1.0',
  license : 'MIT',
  meson_version : '>=1.3.0',
  default_options : [
    'c_std=c11',
    'warning_level=2',
    'buildtype=release',
    'default_library=static',
    'b_vscrt=mt', # MSVC: the static C runtime, a drop in DLL brings no runtime DLLs
  ])

cc = meson.get_compiler('c')
if host_machine.system() != 'windows'
  error('fatgl is a Windows opengl32.dll replacement (WGL) for now')
endif

# fatmap: a local install (pkg-config / cmake) if there is one, else the
# release package (subprojects/fatmap.wrap)
if get_option('fatmap') == 'source' # a checkout (e.g. a junction / symlink) at subprojects/fatmap-src
  fatmap_dep = subproject('fatmap-src', default_options : ['tests=false', 'sandbox=disabled']).get_variable('fatmap_dep')
else
  fatmap_dep = dependency('fatmap', version : '>=0.6.0', fallback : ['fatmap', 'fatmap_dep'])
endif
# GLSL -> SPIR-V (subprojects/glslang.wrap, our meson.build)
glslang_dep = dependency('glslang', fallback : ['glslang', 'glslang_dep'])

args = cc.get_supported_arguments(['-ffp-contract=off', '-Wno-unused-parameter'])
if cc.get_id() == 'msvc'
  args += ['-D_CRT_SECURE_NO_WARNINGS']
endif

sources = files(
  'src/wgl.c',
  'src/gl_state.c',
  'src/gl_matrix.c',
  'src/gl_draw.c',
  'src/gl_light.c',
  'src/gl_texture.c',
  'src/gl_list.c',
  'src/gl_buffer.c',
  'src/gl_shader.c',
  'src/gl_draw33.c',
  'src/gl_fbo.c',
  'src/gl_misc.c',
  'src/glsl_legacy.c',
  'src/gl_pixels.c',
  'src/overlay.c',
  'src/gen/gl_stubs.c',
  'src/gen/gl_procs.c',
)

# a drop in for the system opengl32.dll: same name, same exports, no
# runtime DLLs of its own
static_rt = cc.get_id() == 'msvc' ? [] : ['-static', '-static-libgcc']
opengl32 = shared_library('opengl32', sources,
  name_prefix : '',
  c_args : args,
  include_directories : include_directories('include', 'src', 'external/stb'),
  vs_module_defs : 'src/gen/opengl32.def',
  dependencies : [fatmap_dep, glslang_dep, cc.find_library('gdi32'), cc.find_library('user32')],
  link_language : 'cpp',
  link_args : static_rt,
  install : true)

# examples: linked against the *system* opengl32 import library on purpose;
# next to fatgl's opengl32.dll (the build directory) Windows loads fatgl's
sys_gl = cc.find_library('opengl32')
gdi = cc.find_library('gdi32')
foreach ex : ['triangle', 'gears', 'cube33', 'fbo33', 'blend', 'prims', 'legacy']
  executable(ex, 'examples/' + ex + '.c',
    c_args : args,
    dependencies : [sys_gl, gdi],
    link_args : static_rt,
    win_subsystem : 'windows')
endforeach

# dev tool: what fatmap accepts of glslang's output for GL GLSL
executable('glsl_probe', 'tools/glsl_probe.c',
  dependencies : [fatmap_dep, glslang_dep],
  link_language : 'cpp',
  link_args : static_rt)

# tests: GL calls through the opengl32.dll in the build directory (a hidden window)
gl_test = executable('gl_test', 'tests/gl_test.c',
  c_args : args,
  dependencies : [sys_gl, gdi, cc.find_library('user32')],
  link_args : static_rt)
test('gl_test', gl_test, workdir : meson.current_build_dir())
