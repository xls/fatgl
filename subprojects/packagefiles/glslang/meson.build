# glslang: GLSL -> SPIR-V (no HLSL front end, no SPIRV-Tools optimizer)
project('glslang', 'cpp', version : '16.6.0',
  license : 'BSD-3-Clause AND BSD-2-Clause AND MIT AND Apache-2.0 AND GPL-3.0-or-later WITH Bison-exception-2.2',
  default_options : ['cpp_std=c++17', 'warning_level=0', 'buildtype=release'])

subdir('gen/glslang') # build_info.h

defs = ['-DENABLE_SPIRV', '-DENABLE_OPT=0']
os_src = []
if host_machine.system() == 'windows'
  defs += ['-DGLSLANG_OSINCLUDE_WIN32']
  os_src = ['glslang/OSDependent/Windows/ossource.cpp']
else
  defs += ['-DGLSLANG_OSINCLUDE_UNIX']
  os_src = ['glslang/OSDependent/Unix/ossource.cpp']
endif

mi = 'glslang/MachineIndependent/'
src = files(
  'glslang/GenericCodeGen/CodeGen.cpp', 'glslang/GenericCodeGen/Link.cpp',
  mi + 'glslang_tab.cpp', mi + 'attribute.cpp', mi + 'Constant.cpp', mi + 'iomapper.cpp', mi + 'InfoSink.cpp',
  mi + 'Initialize.cpp', mi + 'IntermTraverse.cpp', mi + 'Intermediate.cpp', mi + 'ParseContextBase.cpp',
  mi + 'ParseHelper.cpp', mi + 'PoolAlloc.cpp', mi + 'RemoveTree.cpp', mi + 'Scan.cpp', mi + 'ShaderLang.cpp',
  mi + 'SpirvIntrinsics.cpp', mi + 'SymbolTable.cpp', mi + 'Versions.cpp', mi + 'intermOut.cpp', mi + 'limits.cpp',
  mi + 'linkValidate.cpp', mi + 'parseConst.cpp', mi + 'reflection.cpp', mi + 'propagateNoContraction.cpp',
  mi + 'preprocessor/Pp.cpp', mi + 'preprocessor/PpAtom.cpp', mi + 'preprocessor/PpContext.cpp',
  mi + 'preprocessor/PpScanner.cpp', mi + 'preprocessor/PpTokens.cpp',
  'glslang/CInterface/glslang_c_interface.cpp',
  'glslang/ResourceLimits/ResourceLimits.cpp', 'glslang/ResourceLimits/resource_limits_c.cpp',
  'SPIRV/GlslangToSpv.cpp', 'SPIRV/InReadableOrder.cpp', 'SPIRV/Logger.cpp', 'SPIRV/SpvBuilder.cpp',
  'SPIRV/SpvPostProcess.cpp', 'SPIRV/doc.cpp', 'SPIRV/SpvTools.cpp', 'SPIRV/disassemble.cpp',
  'SPIRV/CInterface/spirv_c_interface.cpp',
  os_src)

inc = include_directories('.', 'gen')
glslang_lib = static_library('glslang', src, build_info_h,
  cpp_args : defs,
  include_directories : inc,
  dependencies : dependency('threads'))
glslang_dep = declare_dependency(
  link_with : glslang_lib,
  include_directories : inc,
  dependencies : dependency('threads'))
meson.override_dependency('glslang', glslang_dep)
