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
  'text-input-crostini-unstable-v1.xml',
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

gtk4_sources = gtk_sources + [
  'frontend/gtk/im_cros_gtk4.cc',
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
    sources: gtk4_sources + wl_outs,
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

backend_test_sources = [
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
  ]

libim_test_cros_gtk3 = shared_library('im-test-cros-gtk3',
  sources: backend_test_sources + gtk3_sources,
  dependencies: [
    meson.get_compiler('cpp').find_library('m'),
    dependency('gtk+-3.0'),
  ],
  cpp_args: ['-DTEST_BACKEND'],
)

if get_option('build_gtk4')
  libim_test_cros_gtk4 = shared_library('im-test-cros-gtk4',
    sources: backend_test_sources + gtk4_sources,
    dependencies: [
      meson.get_compiler('cpp').find_library('m'),
      dependency('gtk4'),
    ],
    cpp_args: ['-DTEST_BACKEND', '-DGTK4'],
  )
endif

test_sources = [
  'test/gtk_basic_test.cc',
  'test/gtk_commit_string_test.cc',
  'test/gtk_content_type_test.cc',
  'test/gtk_test_base.h',
  'test/gtk_windowing_test.cc',
  'util/logging.cc',
  'util/logging.h',
]

cros_im_tests_gtk3 = executable('cros_im_tests_gtk3',
  sources: test_sources + [
  'test/gtk_key_sym_test.cc',
  ],
  dependencies: [
    meson.get_compiler('cpp').find_library('m'),
    dependency('gtkmm-3.0'),
    dependency('gtest', main: true),
  ],
)

if get_option('build_gtk4')
  cros_im_tests_gtk4 = executable('cros_im_tests_gtk4',
    sources: test_sources,
    dependencies: [
      meson.get_compiler('cpp').find_library('m'),
      dependency('gtkmm-4.0'),
      dependency('gtest', main: true),
    ],
    cpp_args: ['-DGTK4'],
  )
endif

python3 = find_program('python3')
# The following tests cannot be run in parallel because they share the same wayland server resources.
test('run_gtk3_unittests',
  python3,
  args: ['../test/run_tests.py', '--with_xvfb',  '--suite=gtk3'],
  depends: [libim_test_cros_gtk3, cros_im_tests_gtk3],
  is_parallel: false,
  timeout: 120,
  verbose : true,
)

if get_option('build_gtk4')
  test('run_gtk4_unittests',
    python3,
    args: ['../test/run_tests.py', '--with_xvfb', '--suite=gtk4'],
    depends: [libim_test_cros_gtk4, cros_im_tests_gtk4],
    is_parallel: false,
    timeout: 120,
    verbose : true,
  )
endif
