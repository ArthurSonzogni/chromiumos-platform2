# Chrome OS arc-setup.

## `/usr/sbin/arc-setup`

`arc-setup` handles setup/teardown of ARC container or upgrading container.
For example, mount point creation, directory creation, setting permissions uids
and gids, selinux label setting, config file creation.

Often, script language is used for such stuff in general, but ARC uses native
executable just for performance and better testability.

## `/usr/share/arc-setup/config.json`

`config.json` is the configuration file for `arc-setup`. Currently, only
`ANDROID_DEBUGGABLE` is defined in this file, which is rewritten by
`board_specific_setup.py` at image build time.
Setting this value to `true` will make Android boot with `ro.debuggable`. This
should make Android behave *mostly* like an -userdebug image.

A non-comprehensive list of caveats:

* Anything that detects the build type at compile-time will be unaffected, in
  particular SELinux rules that are relaxed, or the conditional compilation of
  some system tools.
* `adb root` will still be unavailable.
* `su` will be missing.
* `strace` won't work.
* The build type will still be -user.

Be careful when adding, removing, or renaming the entries.
