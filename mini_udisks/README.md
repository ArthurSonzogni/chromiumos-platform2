# mini_udisks

This directory contains a small daemon that implements part of the
[UDisks2 API][udisks]. This is used on ChromeOS Flex by [fwupd], which
uses UDisks to interact with the [EFI System Partition (ESP)][esp] when
installing UEFI firmware updates.

The daemon is only installed on the reven board (i.e. ChromeOS Flex) and
its derivatives.

## API

Get the list of block devices (will only include the ESP):
```
sudo -u fwupd \
    gdbus call --system --dest org.freedesktop.UDisks2 \
    --object-path /org/freedesktop/UDisks2/Manager \
    --method org.freedesktop.UDisks2.Manager.GetBlockDevices {}
```
Example output:
```
([objectpath '/org/freedesktop/UDisks2/block_devices/sda12'],)
```


View properties of the ESP partition device:
```
gdbus introspect --system --dest org.freedesktop.UDisks2 --only-properties \
    --object-path /org/freedesktop/UDisks2/block_devices/sda12
```

Example output:
```
node /org/freedesktop/UDisks2/block_devices/sda12 {
  interface org.freedesktop.UDisks2.Block {
    properties:
      readonly s Device = '/dev/sda12';
  };
  interface org.freedesktop.UDisks2.Filesystem {
    properties:
      readonly aay MountPoints = [[0x2f, 0x65, 0x66, 0x69]];
  };
  interface org.freedesktop.UDisks2.Partition {
    properties:
      readonly u Number = 12;
      readonly s Type = 'c12a7328-f81f-11d2-ba4b-00a0c93ec93b';
      readonly s UUID = 'ac45b957-1eab-6c48-98de-71b63a017b6b';
  };
};
```

[esp]: https://en.wikipedia.org/wiki/EFI_system_partition
[fwupd]: https://github.com/fwupd/fwupd
[udisks]: http://storaged.org/doc/udisks2-api/latest/index.html
