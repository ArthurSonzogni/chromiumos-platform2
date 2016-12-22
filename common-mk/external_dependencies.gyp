{
  'targets': [
    {
      'target_name': 'modemmanager-dbus-proxies',
      'type': 'none',
      'variables': {
        'xml2cpp_type': 'proxy',
        'xml2cpp_in_dir': '<(sysroot)/usr/share/dbus-1/interfaces/',
        'xml2cpp_out_dir': 'include/dbus_proxies',
      },
      'sources': [
        '<(xml2cpp_in_dir)/mm-mobile-error.xml',
        '<(xml2cpp_in_dir)/mm-serial-error.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager.Modem.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager.Modem.Cdma.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager.Modem.Firmware.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager.Modem.Gsm.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager.Modem.Gsm.Card.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager.Modem.Gsm.Contacts.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager.Modem.Gsm.Network.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager.Modem.Gsm.SMS.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager.Modem.Gsm.Ussd.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager.Modem.Simple.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager1.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager1.Bearer.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager1.Modem.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager1.Modem.Location.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager1.Modem.Modem3gpp.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager1.Modem.ModemCdma.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager1.Modem.Simple.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager1.Modem.Time.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager1.Sim.xml',
      ],
      'includes': ['xml2cpp.gypi'],
    },
    {
      'target_name': 'modemmanager-dbus-adaptors',
      'type': 'none',
      'variables': {
        'xml2cpp_type': 'adaptor',
        'xml2cpp_in_dir': '<(sysroot)/usr/share/dbus-1/interfaces/',
        'xml2cpp_out_dir': 'include/dbus_adaptors',
      },
      'sources': [
        '<(xml2cpp_in_dir)/mm-mobile-error.xml',
        '<(xml2cpp_in_dir)/mm-serial-error.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager.Modem.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager.Modem.Cdma.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager.Modem.Firmware.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager.Modem.Gsm.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager.Modem.Gsm.Card.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager.Modem.Gsm.Contacts.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager.Modem.Gsm.Network.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager.Modem.Gsm.SMS.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager.Modem.Gsm.Ussd.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager.Modem.Simple.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager1.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager1.Bearer.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager1.Modem.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager1.Modem.Location.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager1.Modem.Modem3gpp.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager1.Modem.ModemCdma.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager1.Modem.Simple.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager1.Modem.Time.xml',
        '<(xml2cpp_in_dir)/org.freedesktop.ModemManager1.Sim.xml',
      ],
      'includes': ['xml2cpp.gypi'],
    },
    {
      'target_name': 'dbus-proxies',
      'type': 'none',
      'variables': {
        'xml2cpp_type': 'proxy',
        'xml2cpp_in_dir': '<(sysroot)/usr/share/dbus-1/interfaces/',
        'xml2cpp_out_dir': 'include/dbus_proxies',
      },
      'sources': [
        '<(xml2cpp_in_dir)/org.freedesktop.DBus.Properties.xml',
      ],
      'includes': ['xml2cpp.gypi'],
    },
    {
      'target_name': 'cloud_policy_proto_generator',
      'type': 'none',
      'hard_dependency': 1,
      'variables': {
        'policy_tools_dir': '<(sysroot)/usr/share/policy_tools',
        'policy_resources_dir': '<(sysroot)/usr/share/policy_resources',
        'proto_out_dir': '<(SHARED_INTERMEDIATE_DIR)/proto',
      },
      'actions': [{
        'action_name': 'run_generate_script',
        'inputs': [
          '<(policy_tools_dir)/generate_policy_source.py',
          '<(policy_resources_dir)/policy_templates.json',
          '<(policy_resources_dir)/VERSION'
        ],
        'outputs': [ '<(proto_out_dir)/cloud_policy.proto' ],
        'action': [
          'python', '<(policy_tools_dir)/generate_policy_source.py',
          '--cloud-policy-protobuf=<(proto_out_dir)/cloud_policy.proto',
          '<(policy_resources_dir)/VERSION',
          '<(OS)',
          '1',         # chromeos-flag
          '<(policy_resources_dir)/policy_templates.json',
        ]
      }],
    },
    {
      'target_name': 'policy-protos',
      'type': 'static_library',
      'variables': {
        'proto_in_dir': '<(sysroot)/usr/include/proto',
        'proto_out_dir': 'include/bindings',
      },
      'sources': [
        '<(proto_in_dir)/chrome_device_policy.proto',
        '<(proto_in_dir)/chrome_extension_policy.proto',
        '<(proto_in_dir)/device_management_backend.proto',
        '<(proto_in_dir)/device_management_local.proto',
      ],
      'includes': ['protoc.gypi'],
    },
    {
      'target_name': 'user_policy-protos',
      'type': 'static_library',
      'variables': {
        'proto_in_dir': '<(SHARED_INTERMEDIATE_DIR)/proto',
        'proto_out_dir': 'include/bindings',
      },
      'dependencies': [
        'cloud_policy_proto_generator',
      ],
      'sources': [
        '<(proto_in_dir)/cloud_policy.proto',
      ],
      'includes': ['protoc.gypi'],
    },
    {
      'target_name': 'install_attributes-proto',
      'type': 'static_library',
      'variables': {
        'proto_in_dir': '<(sysroot)/usr/include/proto',
        'proto_out_dir': 'include/bindings',
      },
      'sources': [
        '<(proto_in_dir)/install_attributes.proto',
      ],
      'includes': ['protoc.gypi'],
    },
  ],
}
