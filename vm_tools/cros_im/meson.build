# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

project('ime_bridge', 'c', 'cpp',
        default_options : ['cpp_std=c++17'])

#=========#
# Wayland #
#=========#

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
]

wl_protocols = [
  '../sommelier/protocol/text-input-unstable-v1.xml',
]

wl_outs = []

foreach p : wl_protocols
  foreach g : wl_generators
    wl_outs += g.process(p)
  endforeach
endforeach

#============#
# ime_bridge #
#============#

backend_sources = [
  'backend/im_context_backend.cc',
  'backend/im_context_backend.h',
  'backend/wayland_manager.cc',
  'backend/wayland_manager.h',
]

libim_cros_gtk = shared_library('im_cros_gtk',
  sources: [
    'frontend/gtk/cros_gtk_im_context.cc',
    'frontend/gtk/cros_gtk_im_context.h',
    'frontend/gtk/im_cros.cc',
  ] + backend_sources + wl_outs,
  dependencies: [
    meson.get_compiler('cpp').find_library('m'),
    dependency('gtk+-3.0'),
    dependency('wayland-client'),
  ],
)
