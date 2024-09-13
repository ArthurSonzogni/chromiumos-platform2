[TOC]
# vhost_user_starter

`vhost_user_starter` is a daemon that serves dbus request from concierge to create new vhost-user
device for concierge. See [`/vm_tools/README.md`](/vm_tools/README.md) for details.


## Development

If you are modifying protocols, cros_workon `chromeos-base/system_api` `dev-rust/system_api`.

```
cros_workon --board ${BOARD} start \
  dev-rust/system_api chromeos-base/vhost_user_starter
```

Then it is possible to work on `vhost_user_starter`.
```
emerge-${BOARD} dev-rust/system_api chromeos-base/vhost_user_starter
```

If you want to build vhost_user_starter by using `cargo build`, you need enter chroot:
```
cros_sdk
(chroot) cd /mnt/host/source/src/platform2/vm_tools/vhost_user_starter && cargo build
```
