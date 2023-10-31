# dev.conf documentation

[TOC]

## What is dev.conf

Concierge allows for configuration of crosvm command line parameters (including
guest kernel boot parameters) for development purposes. It works only when
Chrome OS is in developer mode. The configuration files are called `*_dev.conf`.
Example of such files are located at:

-   `/usr/local/vms/etc/arcvm_dev.conf`: ARCVM
-   `/usr/local/vms/etc/termina_dev.conf`: termina
-   `/usr/local/vms/etc/borealis_dev.conf`: Borealis
-   `/usr/local/vms/etc/bruschetta_dev.conf`: Bruschetta

## Format of the configuration file.

This file may be modified to make local changes to the command line that ARCVM
uses to start a VM. It contains one directive per line, with the following forms
available:

`# This is a comment.`
:   Lines beginning with '#' are skipped.

`--some-flag=some-value`
:   Appends "--some-flag" and "some-value" to command line.

`--some-flag`
:   Appends "--some-flag" to command line.

`!--flag-prefix`
:   Removes all arguments beginning with "--flag-prefix".

`^--some-flag=some-value`
:   Prepends "--some-flag" and "some-value" before the other command line flags,
    but after "crosvm run".

`^--some-flag`
:   Prepends "--some-flag" before the other command line flags, but after
    "crosvm run".

`prerun:--some-flag`
:   Prepends "--some-flag" before `run` and after `crosvm` in "crosvm run".

`precrosvm:something`
:   Prepends "something" before "crosvm". Each line is one argument for exec,
    use multiple lines if you want to pass multiple parameters.

`KERNEL_PATH=/set/to/new_path`
:   Override the guest kernel path to /set/to/new_path. KERNEL_PATH must consist
    of capital letters.

`O_DIRECT=true`
:   Force O_DIRECT on all virtio-blk devices.

`O_DIRECT_N=N`
:   Force O_DIRECT on Nth virtio-blk device. N starts from 0. For example,
    `O_DIRECT_N=0` for turning on O_DIRECT on system.img on ARCVM.

`BLOCK_MULTIPLE_WORKERS=true`
:   Enable multiple worker threads on all virtio-blk devices.

`BLOCK_ASYNC_EXECUTOR=(uring|epoll)`
:   Switch the async executor of all virtio-blk devices.

`SKIP_SWAP_POLICY=true`
:   Skip policy gates when enabling ARCVM swap. Since this skips total bytes
    written throttling and low disk management, setting this may result in
    excessive wear on the disk or errors due to exhausting disk space.

Directives are applied in the order they appear (i.e. to change a flag, first
delete it and then re-add it with the desired value).

Setting values on environment variables is not supported.

## Tips

### Add serial-based earlycon and virtio-console logging.

crosvm takes serial flags with virtio-console. Needs separate serial config for
earlycon. Here is the configuration to dump them to syslog.

```
--serial=type=syslog,hardware=serial,num=1,earlycon=true
--serial=type=syslog,hardware=virtio-console,num=1,console=true
```

### Getting dmesg logs from android guest kernel.

TIP: When you want to see raw dmesg logs from the Android guest kernel and
system processes such as init, uncomment the following line. By default, the
guest kernel rate-limits the logging and some logs could be silently dropped.
This is useful when modifying init.bertha.rc, for example.

```
--params=printk.devkmsg=on
```

Only using this line above won't let you see all the logs though. If you want to
see very early (the first a few hundred milliseconds) logs from the guest
kernel, make sure to install the guest kernel built with USE=pcserial:

```
chroot$ cros_workon --board=$BOARD start arcvm-kernel-5_4
chroot$ USE=pcserial emerge-$BOARD arcvm-kernel-5_4
chroot$ cros deploy DUT arcvm-kernel-5_4
```

### Other ARCVM tips:

Disable selinux on userdebug image

```
--params=androidboot.selinux=permissive
```

Suppress selinux audit kernel message.

```
--params=audit=0
```

Override default `androidboot.*` parameters, which requires to be prepended to
the parameter, e.g. override `androidboot.arc_top_app_uclamp_min` to 80.

```
^--params=androidboot.arc_top_app_uclamp_min=80
```
