# Copyright 2021 The ChromiumOS Authors
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
  'text-input-extension-unstable-v1.xml',
  'text-input-unstable-v1.xml',
  'text-input-x11-unstable-v1.xml',
]

wl_outs = []

foreach p : wl_protocols
  foreach g : wl_generators
    wl_outs += g.process(get_option('wl_protocol_dir') / p)
  endforeach
endforeach

#============#
# ime_bridge #
#============#

backend_sources = [
  'backend/im_context_backend.cc',
  'backend/im_context_backend.h',
  'backend/text_input.h',
  'backend/text_input_enums.h',
  'backend/wayland_client.h',
  'backend/wayland_manager.cc',
  'backend/wayland_manager.h',
  'util/logging.cc',
  'util/logging.h',
]

gtk_sources = backend_sources + [
  'frontend/gtk/cros_gtk_im_context.cc',
  'frontend/gtk/cros_gtk_im_context.h',
  'frontend/gtk/x11.cc',
  'frontend/gtk/x11.h',
]

gtk3_sources = gtk_sources + [
  'frontend/gtk/im_cros_gtk3.cc',
]

libim_cros_gtk3 = shared_library('im-cros-gtk3',
  sources: gtk3_sources + wl_outs,
  dependencies: [
    meson.get_compiler('cpp').find_library('m'),
    dependency('gtk+-3.0'),
    dependency('wayland-client'),
  ],

  # No prefix on build result.
  name_prefix: '',
  install: true,
  install_dir: get_option('libdir') / 'gtk-3.0/3.0.0/immodules',
)

if get_option('build_gtk4')
  libim_cros_gtk4 = shared_library('im-cros-gtk4',
    sources: gtk_sources + [
    'frontend/gtk/im_cros_gtk4.cc',
  ]  + wl_outs,
    dependencies: [
      meson.get_compiler('cpp').find_library('m'),
      dependency('gtk4'),
      dependency('wayland-client'),
    ],
    cpp_args: ['-DGTK4'],
    install: true,
    install_dir: get_option('libdir') / 'gtk-4.0/4.0.0/immodules',
  )
endif

qt_other_sources = [
  'frontend/qt/cros_qt_im_context.cc',
  'frontend/qt/im_cros.cc',
  'frontend/qt/x11.cc',
  'frontend/qt/x11.h',
]

moc_headers = [
  'frontend/qt/cros_qt_im_context.h',
  'frontend/qt/im_cros.h',
]

qt5 = import('qt5')

qt5_deps = dependency('qt5', modules: ['Core', 'Gui', 'InputSupport', 'XkbCommonSupport', 'Concurrent'], private_headers: true)

qt5_moc_output = qt5.preprocess(moc_headers: moc_headers, dependencies: qt5_deps)

shared_library('libcrosplatforminputcontextplugin',
  sources: backend_sources + qt_other_sources + qt5_moc_output + wl_outs,
  dependencies: [
    meson.get_compiler('cpp').find_library('m'),
    qt5_deps,
    dependency('wayland-client'),
    dependency('xkbcommon'),
    dependency('x11'),
  ],

  # No prefix on build result.
  name_prefix: '',
  install: true,
  install_dir: get_option('libdir') / 'qt5/plugins/platforminputcontexts',
)

# TODO(b/283915925): Add tests for GTK4.
libim_test_cros_gtk3 = shared_library('im_test_cros_gtk3',
  sources: [
    'backend/test/backend_test.cc',
    'backend/test/backend_test.h',
    'backend/test/backend_test_utils.h',
    'backend/test/event.cc',
    'backend/test/event.h',
    'backend/test/gtk_basic_test_backend.cc',
    'backend/test/gtk_commit_string_test_backend.cc',
    'backend/test/gtk_content_type_test_backend.cc',
    'backend/test/gtk_key_sym_test_backend.cc',
    'backend/test/gtk_windowing_test_backend.cc',
    'backend/test/mock_text_input.cc',
    'backend/test/mock_text_input.h',
    'backend/test/mock_wayland_client.cc',
    'backend/test/mock_wayland_client.h',
    'backend/test/request.cc',
    'backend/test/request.h',
  ] + gtk3_sources,
  dependencies: [
    meson.get_compiler('cpp').find_library('m'),
    dependency('gtk+-3.0'),
  ],
  cpp_args: ['-DTEST_BACKEND'],
)

test_sources = [
  'test/gtk_basic_test.cc',
  'test/gtk_commit_string_test.cc',
  'test/gtk_content_type_test.cc',
  'test/gtk_key_sym_test.cc',
  'test/gtk_test_base.h',
  'test/gtk_windowing_test.cc',
]

cros_im_tests_gtk3 = executable('cros_im_tests_gtk3',
  sources: test_sources,
  dependencies: [
    meson.get_compiler('cpp').find_library('m'),
    dependency('gtkmm-3.0'),
    dependency('gtest', main: true),
  ],
)

python3 = find_program('python3')
test('run_all_tests',
  python3,
  args: ['../test/run_tests.py', '--with_xvfb'],
  depends: [libim_test_cros_gtk3, cros_im_tests_gtk3],
  timeout: 120,
  verbose : true,
)
