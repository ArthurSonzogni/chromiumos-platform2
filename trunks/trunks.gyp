# Copyright 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'target_defaults': {
    'includes': ['../common-mk/common.gypi'],
    'variables': {
      'deps': [  # This is a list of pkg-config dependencies
        'libchrome-<(libbase_ver)',
        'libchromeos-<(libbase_ver)',
        'openssl',
        'protobuf-lite',
      ],
    },
  },
  'targets': [
    {
      'target_name': 'dbus_interface_proto',
      'type': 'static_library',
      'variables': {
        'proto_in_dir': '.',
        'proto_out_dir': 'include/trunks',
      },
      'sources': [
        '<(proto_in_dir)/dbus_interface.proto',
      ],
      'includes': ['../common-mk/protoc.gypi'],
    },
    {
      'target_name': 'trunks',
      'type': 'shared_library',
      'sources': [
        'blob_parser.cc',
        'error_codes.cc',
        'hmac_authorization_delegate.cc',
        'hmac_session_impl.cc',
        'password_authorization_delegate.cc',
        'policy_session_impl.cc',
        'session_manager_impl.cc',
        'scoped_key_handle.cc',
        'tpm_generated.cc',
        'tpm_state_impl.cc',
        'tpm_utility_impl.cc',
        'trunks_factory_impl.cc',
        'trunks_proxy.cc',
      ],
      'dependencies': [
        'dbus_interface_proto',
      ],
      'conditions': [
        ['USE_ftdi_tpm == 1', {
            'sources': [
              'ftdi/mpsse.c',
              'ftdi/support.c',
              'trunks_ftdi_spi.cc',
            ],
            'libraries': [
              '-lftdi1',
            ],
            'defines': [
              'SPI_OVER_FTDI=1',
            ],
          },
        ],
      ],
    },
    {
      'target_name': 'trunks_client',
      'type': 'executable',
      'sources': [
        'trunks_client.cc',
        'trunks_client_test.cc',
      ],
      'dependencies': [
        'trunks',
      ],
    },
    {
      'target_name': 'trunksd_lib',
      'type': 'static_library',
      'sources': [
        'background_command_transceiver.cc',
        'resource_manager.cc',
        'tpm_handle.cc',
        'trunks_service.cc',
      ],
      'dependencies': [
        'dbus_interface_proto',
      ],
    },
    {
      'target_name': 'trunksd',
      'type': 'executable',
      'sources': [
        'trunksd.cc',
      ],
      'variables': {
        'deps': [
          'libminijail',
        ],
      },
      'dependencies': [
        'dbus_interface_proto',
        'trunks',
        'trunksd_lib',
      ],
      'conditions': [
        ['USE_ftdi_tpm == 1', {
            'defines': [
              'SPI_OVER_FTDI=1',
            ],
          },
        ],
      ],
    },
  ],
  'conditions': [
    ['USE_test == 1', {
      'targets': [
        {
          'target_name': 'trunks_testrunner',
          'type': 'executable',
          'includes': ['../common-mk/common_test.gypi'],
          'sources': [
            'background_command_transceiver_test.cc',
            'hmac_authorization_delegate_unittest.cc',
            'hmac_session_test.cc',
            'mock_authorization_delegate.cc',
            'mock_blob_parser.cc',
            'mock_command_transceiver.cc',
            'mock_hmac_session.cc',
            'mock_policy_session.cc',
            'mock_session_manager.cc',
            'mock_tpm.cc',
            'mock_tpm_state.cc',
            'mock_tpm_utility.cc',
            'password_authorization_delegate_unittest.cc',
            'policy_session_test.cc',
            'resource_manager_test.cc',
            'scoped_key_handle_test.cc',
            'session_manager_test.cc',
            'tpm_generated_test.cc',
            'tpm_state_test.cc',
            'tpm_utility_test.cc',
            'trunks_factory_for_test.cc',
            'trunks_testrunner.cc',
          ],
          'dependencies': [
            'trunks',
            'trunksd_lib',
          ],
        },
      ],
    }],
  ],
}
