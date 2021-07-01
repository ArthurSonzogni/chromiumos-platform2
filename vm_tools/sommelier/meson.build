# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

project('sommelier', 'c', 'cpp')

#===============#
# Configuration #
#===============#

includes = []
cpp_args = []

#===============#
# Wayland Stuff #
#===============#

wl_scanner = find_program('wayland-scanner')

wl_generators = [
  generator(
    wl_scanner,
    output: '@BASENAME@-code.c',
    arguments: ['private-code', '@INPUT@', '@OUTPUT@']
  ),
  generator(
    wl_scanner,
    output: '@BASENAME@-client-protocol.h',
    arguments: ['client-header', '@INPUT@', '@OUTPUT@']
  ),
  generator(
    wl_scanner,
    output: '@BASENAME@-server-protocol.h',
    arguments: ['server-header', '@INPUT@', '@OUTPUT@']
  ),
]

wl_protocols = [
    'protocol/aura-shell.xml',
    'protocol/drm.xml',
    'protocol/gaming-input-unstable-v2.xml',
    'protocol/gtk-shell.xml',
    'protocol/keyboard-extension-unstable-v1.xml',
    'protocol/linux-dmabuf-unstable-v1.xml',
    'protocol/linux-explicit-synchronization-unstable-v1.xml',
    'protocol/pointer-constraints-unstable-v1.xml',
    'protocol/relative-pointer-unstable-v1.xml',
    'protocol/text-input-unstable-v1.xml',
    'protocol/viewporter.xml',
    'protocol/xdg-shell-unstable-v6.xml',
]

wl_outs = []

foreach p : wl_protocols
  foreach g : wl_generators
    wl_outs += g.process(p)
  endforeach
endforeach

#==========#
# Perfetto #
#==========#

tracing_sources = []
tracing_dependencies = []

if get_option('tracing')
  tracing_sources = [
    'perfetto/sdk/perfetto.cc',
  ]
  tracing_dependencies = [
    dependency('threads'),
  ]
  cpp_args += '-DPERFETTO_TRACING'
  includes += include_directories('perfetto/sdk')
endif

#=================#
# Gamepad support #
#=================#

gamepad_sources = []
gamepad_dependencies = []

if get_option('gamepad')
  gamepad_sources = [
    'sommelier-gaming.cc',
  ]
  gamepad_dependencies = [
    dependency('libevdev'),
  ]
  cpp_args += '-DGAMEPAD_SUPPORT'
endif

#===========#
# Sommelier #
#===========#

if get_option('commit_loop_fix')
  cpp_args += '-DCOMMIT_LOOP_FIX'
endif

sommelier_defines = [
    '-D_GNU_SOURCE',
    '-DWL_HIDE_DEPRECATED',
    '-DXWAYLAND_PATH="' + get_option('xwayland_path') + '"',
    '-DXWAYLAND_GL_DRIVER_PATH="' + get_option('xwayland_gl_driver_path') + '"',
    '-DFRAME_COLOR="' + get_option('frame_color') + '"',
    '-DDARK_FRAME_COLOR="' + get_option('dark_frame_color') + '"',
]

libsommelier = static_library('sommelier',
  sources: [
    'sommelier-compositor.cc',
    'sommelier-ctx.cc',
    'sommelier-data-device-manager.cc',
    'sommelier-display.cc',
    'sommelier-drm.cc',
    'sommelier-gtk-shell.cc',
    'sommelier-global.cc',
    'sommelier-mmap.cc',
    'sommelier-output.cc',
    'sommelier-pointer-constraints.cc',
    'sommelier-relative-pointer-manager.cc',
    'sommelier-seat.cc',
    'sommelier-shell.cc',
    'sommelier-shm.cc',
    'sommelier-subcompositor.cc',
    'sommelier-text-input.cc',
    'sommelier-timing.cc',
    'sommelier-tracing.cc',
    'sommelier-util.cc',
    'sommelier-viewporter.cc',
    'sommelier-xdg-shell.cc',
    'sommelier.cc',
    'sommelier-window.cc',
    'virtualization/virtwl_channel.cc',
    'virtualization/virtgpu_channel.cc',
  ] + wl_outs + tracing_sources + gamepad_sources,
  dependencies: [
    meson.get_compiler('cpp').find_library('m'),
    dependency('gbm'),
    dependency('libdrm'),
    dependency('pixman-1'),
    dependency('wayland-client'),
    dependency('wayland-server'),
    dependency('xcb'),
    dependency('xcb-composite'),
    dependency('xcb-xfixes'),
    dependency('xkbcommon'),
  ] + tracing_dependencies + gamepad_dependencies,
  cpp_args: cpp_args + sommelier_defines,
  include_directories: includes,
)

executable('sommelier',
  install: true,
  sources: [
    'sommelier-main.cc',
  ] + wl_outs, 
  link_with: libsommelier,
  cpp_args: cpp_args + sommelier_defines,
  include_directories: includes,
)

if get_option('with_tests')
  sommelier_test = executable('sommelier_test',
    install: true,
    sources: [
      'sommelier_test.cc',
    ],
    link_with: libsommelier,
    dependencies: [
      dependency('gtest'),
      dependency('gmock'),
    ],
    cpp_args: cpp_args + sommelier_defines,
    include_directories: includes,
  )

  test('sommelier_test', sommelier_test)
endif
