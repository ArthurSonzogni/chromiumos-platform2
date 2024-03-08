# mini_udisks

This directory contains a small daemon that implements part of the
[UDisks2 API][udisks]. This is used on ChromeOS Flex by [fwupd], which
uses UDisks to interact with the [EFI System Partition (ESP)][esp] when
installing UEFI firmware updates.

The daemon is only installed on the reven board (i.e. ChromeOS Flex) and
its derivatives.

[esp]: https://en.wikipedia.org/wiki/EFI_system_partition
[fwupd]: https://github.com/fwupd/fwupd
[udisks]: http://storaged.org/doc/udisks2-api/latest/index.html
