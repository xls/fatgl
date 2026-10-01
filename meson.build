project('fatgl', 'c',
  version : '0.1.0',
  license : 'MIT',
  meson_version : '>=1.3.0',
  default_options : [
    'c_std=c11',
    'warning_level=2',
    'buildtype=release',
    'default_library=static',
  ])

cc = meson.get_compiler('c')
if host_machine.system() != 'windows'
  error('fatgl is a Windows opengl32.dll replacement (WGL) for now')
endif

# fatmap: a local install (pkg-config / cmake) if there is one, else the
# release package (subprojects/fatmap.wrap)
fatmap_dep = dependency('fatmap', version : '>=0.2.0', fallback : ['fatmap', 'fatmap_dep'])

args = cc.get_supported_arguments(['-ffp-contract=off', '-Wno-unused-parameter'])

sources = files(
  'src/wgl.c',
  'src/gl_state.c',
  'src/gl_matrix.c',
  'src/gl_draw.c',
  'src/gl_light.c',
  'src/gl_texture.c',
  'src/gl_list.c',
  'src/gen/gl_stubs.c',
  'src/gen/gl_procs.c',
)

# a drop in for the system opengl32.dll: same name, same exports, no
# runtime DLLs of its own
static_rt = cc.get_id() == 'msvc' ? [] : ['-static', '-static-libgcc']
opengl32 = shared_library('opengl32', sources,
  name_prefix : '',
  c_args : args,
  include_directories : include_directories('include', 'src'),
  vs_module_defs : 'src/gen/opengl32.def',
  dependencies : [fatmap_dep],
  link_args : static_rt + ['-lgdi32', '-luser32'],
  install : true)

# examples: linked against the *system* opengl32 import library on purpose;
# next to fatgl's opengl32.dll (the build directory) Windows loads fatgl's
sys_gl = cc.find_library('opengl32')
gdi = cc.find_library('gdi32')
foreach ex : ['triangle', 'gears']
  executable(ex, 'examples/' + ex + '.c',
    c_args : args,
    dependencies : [sys_gl, gdi],
    link_args : static_rt,
    win_subsystem : 'windows')
endforeach
