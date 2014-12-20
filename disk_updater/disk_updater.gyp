{
  'targets': [
    {
      # Add a fake action rule so that in the absence of any targets needing to
      # be built, we don't generate an empty ninja build rules file.
      'target_name': 'phony',
      'type': 'none',
      'actions': [{
        'inputs': [],
        # "outputs" can not be empty. Set it to refer to this gyp file so
        # ninja never actually runs this rule.
        'outputs': [
          'disk_updater.gyp'
        ],
        'action_name': 'phony',
        'action': ['true']
      }],
    },
  ],
}
