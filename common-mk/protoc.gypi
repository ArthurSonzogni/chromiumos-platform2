{
  'variables': {
    'cc_dir': '<(SHARED_INTERMEDIATE_DIR)/<(proto_out_dir)',
    'python_dir': '<(SHARED_INTERMEDIATE_DIR)/<(proto_out_dir)/py',
    'proto_in_dir%': '.',
    'protoc': '<!(which protoc)',
    'grpc_cpp_plugin': '<!(which grpc_cpp_plugin)',
    'gen_bidl%': 0,
    'gen_grpc%': 0,
    'gen_python%': 0,
  },
  'rules': [
    {
      'rule_name': 'genproto',
      'extension': 'proto',
      'inputs': [
        '<(protoc)',
      ],
      'conditions': [
        ['gen_bidl==1', {
          'variables': {
            'out_args': ['--bidl_out', '<(cc_dir)'],
          },
          'outputs': [
            '<(cc_dir)/<(RULE_INPUT_ROOT).pb.rpc.cc',
            '<(cc_dir)/<(RULE_INPUT_ROOT).pb.rpc.h',
            # gen_bidl generates bidl code in addition to normal protobuffers.
            '<(cc_dir)/<(RULE_INPUT_ROOT).pb.cc',
            '<(cc_dir)/<(RULE_INPUT_ROOT).pb.h',
          ],
        }],
        ['gen_python==1', {
          'variables': {
            'out_args': ['--python_out', '<(python_dir)'],
          },
          'outputs': [
            '<(python_dir)/<(RULE_INPUT_ROOT)_pb.py',
          ],
        }],
        ['gen_grpc==1', {
          'variables': {
            'out_args': [
              '--grpc_out=<(cc_dir)',
              '--plugin=protoc-gen-grpc=<(grpc_cpp_plugin)',
              '--cpp_out=<(cc_dir)',
            ],
          },
          'outputs': [
            '<(cc_dir)/<(RULE_INPUT_ROOT).grpc.pb.cc',
            '<(cc_dir)/<(RULE_INPUT_ROOT).grpc.pb.h',
            '<(cc_dir)/<(RULE_INPUT_ROOT).pb.cc',
            '<(cc_dir)/<(RULE_INPUT_ROOT).pb.h',
          ],
        }],
        ['gen_bidl==0 and gen_grpc==0 and gen_python==0', {
          'outputs': [
            '<(cc_dir)/<(RULE_INPUT_ROOT).pb.cc',
            '<(cc_dir)/<(RULE_INPUT_ROOT).pb.h',
          ],
          'variables': {
            'out_args': ['--cpp_out', '<(cc_dir)'],
          },
        }],
      ],
      'action': [
        '<(protoc)',
        '--proto_path','<(proto_in_dir)',
        '--proto_path','<(sysroot)/usr/share/proto',
        '<(proto_in_dir)/<(RULE_INPUT_ROOT)<(RULE_INPUT_EXT)',
        '>@(out_args)',
      ],
      'msvs_cygwin_shell': 0,
      'message': 'Generating C++ code from <(RULE_INPUT_PATH)',
      'process_outputs_as_sources': 1,
    },
  ],
  # This target exports a hard dependency because it generates header
  # files.
  'hard_dependency': 1,
  'direct_dependent_settings': {
    'include_dirs': [
      '<(SHARED_INTERMEDIATE_DIR)',
    ],
  },
}
