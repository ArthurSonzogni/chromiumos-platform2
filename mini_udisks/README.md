# mini_udisks

This directory contains a small daemon that implements part of the
[UDisks2 API][udisks]. This is used on ChromeOS Flex by [fwupd], which
expects UDisks to provide it with the [EFI System Partition (ESP)][esp]
mount point so that it can write UEFI firmware capsules there. See the
[uefi-capsule] fwupd plugin for more information.

The ESP on ChromeOS Flex is fairly small, so it is not used for capsule
storage. Instead, capsules are written to an unencrypted
directory on the stateful partition:
`/mnt/stateful_partition/unencrypted/uefi_capsule_updates`.

Fwupd doesn't care whether the ESP path is a real partition, it's fine
to pass in a "mount point" that is actually just a regular directory.

Note that the mount-point path provided by this API is
`/run/uefi_capsule_updates` rather than the full `uefi_capsule_updates`
path; this is because fwupd is running in a minijail sandbox which
bind-mounts the capsule directory under `/run`. (Bind-mounting to the
full stateful path inside the sandbox would require that all the
intermediate directories exist, which is more trouble than it's worth.)

The daemon is only installed on the reven board (i.e. ChromeOS Flex) and
its derivatives.

## API

Get the list of block devices (will only include the fake ESP):
```
sudo -u fwupd \
    gdbus call --system --dest org.freedesktop.UDisks2 \
    --object-path /org/freedesktop/UDisks2/Manager \
    --method org.freedesktop.UDisks2.Manager.GetBlockDevices {}
```
Output:
```
([objectpath '/org/freedesktop/UDisks2/block_devices/fakedisk12'],)
```


View properties of the ESP partition device:
```
gdbus introspect --system --dest org.freedesktop.UDisks2 --only-properties \
    --object-path /org/freedesktop/UDisks2/block_devices/fakedisk12
```

Output:
```
node /org/freedesktop/UDisks2/block_devices/fakedisk12 {
  interface org.freedesktop.UDisks2.Block {
    properties:
      readonly s Device = '/dev/fakedisk12';
  };
  interface org.freedesktop.UDisks2.Filesystem {
    properties:
      readonly aay MountPoints = [b'/run/uefi_capsule_updates'];
  };
  interface org.freedesktop.UDisks2.Partition {
    properties:
      readonly u Number = 12;
      readonly s Type = 'c12a7328-f81f-11d2-ba4b-00a0c93ec93b';
      readonly s UUID = '99cc6f39-2fd1-4d85-b15a-543e7b023a1f';
  };
};
```

[capsule]: https://github.com/fwupd/fwupd/blob/HEAD/plugins/uefi-capsule/README.md
[esp]: https://en.wikipedia.org/wiki/EFI_system_partition
[fwupd]: https://github.com/fwupd/fwupd
[udisks]: http://storaged.org/doc/udisks2-api/latest/index.html
