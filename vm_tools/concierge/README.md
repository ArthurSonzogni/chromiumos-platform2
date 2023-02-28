# vm_concierge

`vm_concierge` is a daemon that exposes a D-Bus interface to control lifetime of
crosvm. See [`/vm_tools/README.md`](/vm_tools/README.md) for details.

[TOC]

## Hacking

For quick iteration on related projects, work on `vm_protos` for grpc and
`chromeos-base/system_api` for dbus.

```
cros_workon --board ${BOARD} start chromeos-base/system_api chromeos-base/vm_host_tools chromeos-base/vm_protos
```

Then it is possible to iterate on `vm_concierge`.

```
cros_workon_make --test --board=brya \
  chromeos-base/system_api \
  --install  # If system_api changed.
cros_workon_make --test --board=brya \
  chromeos-base/vm_protos \
  --install  # If vm_protos changed.
cros_workon_make --test --board=brya chromeos-base/vm_host_tools
```
