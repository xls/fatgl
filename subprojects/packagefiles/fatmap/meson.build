# the prebuilt fatmap release package as a Meson dependency
project('fatmap', 'c', version : '0.3.0')
cc  = meson.get_compiler('c')
lib = cc.find_library('fatmap', dirs : meson.current_source_dir() / 'lib', static : true)
fatmap_dep = declare_dependency(
  dependencies : [lib, cc.find_library('m', required : false)],
  include_directories : include_directories('include'))
meson.override_dependency('fatmap', fatmap_dep)
