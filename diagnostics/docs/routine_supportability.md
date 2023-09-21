# Routine supportability

Healthd develops more and more [routines][cros_healthd_routines.mojom],
our clients use them as part of their diagnostic flow. However, clients want
to know if a certain routine is supported or not so that they can decide to
render an icon on the UI or not. Hence, we release an interface
`CrosHealthdRoutinesService.IsRoutineArgumentSupported` for our clients to
query the routine support status.

This document focuses on the following things:
- How we determine if a routine is supported.
- What we need from OEMs to configure to make the routine supported.
- Focus on V2 routines only.

This document assumes that OEMs/ODMs understand how to make changes to the
Boxster config. If OEMs/ODMs have any trouble on this, please
[contact us][team-contact].

[TOC]

## Command line interface

Some commands help you debug issues or have a quick try:

1. `cros-health-tool diag --help` Use this command to check all possible routine
   types.
2. `cros-health-tool diag $routine --help` Use this command to see the
   parameters of each routine.
3. `cros-health-tool diag $routine {parameters...} --check_supported` Use this
   command to see if the routine with specific parameters is supported or not.
   When the flag `--check_supported` is enabled, we won't run the routine. An
   example:
   `cros-health-tool diag disk_read_v2 --file_size_mib=0 --check_supported`
   shows `Not supported` with message: `Test file size should not be zero`.

Please understand that routine supportability differs from event
supportability. The status check will also verify routine parameters. For
example, you may think that the `Disk read` routine is always supported but
it's not the truth. If the `read file size` is set to zero, it's obviously
not supported.

## Routines

### Memory

Always supported.

### Audio driver

Always supported.

### Cpu stress

Always supported.

### Ufs lifetime

Supported only when `storage-type` is explicitly configured as "UFS".

You can run the following commands on your DUT:
1. `cros_config /hardware-properties storage-type` This is helpful to understand
   what the value of `storage-type` is.
2. `cros-health-tool diag ufs_lifetime --check_supported` Use this to see if
   healthd reports the correct support status.

To configure `storage-type` in Boxster, you can use the
`create_non_volatile_storage` function defined in [hw_topology.star] to set it
up.

### Disk read

Supported only when the routine parameters are well provided.

| Parameter | Type | Description | Rule |
| --------- | ---- | ----------- | ---- |
| type | DiskReadTypeEnum | Type of how disk reading is performed. | Only accept one of the ["linear", "random"]. |
| disk_read_duration | ash.cros_healthd.external.mojo_base.mojom.TimeDelta | Expected duration to read the test file in the routine. | Should be greater than or equal to `1` second. |
| file_size_mib | uint32 | Test file size, in megabytes (MiB). | Should be greater than or equal to `1`. |

You can run the following command on your DUT:
- `cros-health-tool diag disk_read_v2 {parameters...} --check_supported` Use
   this to see if healthd reports the correct support status.

Some examples:
```
# cros-health-tool diag disk_read_v2 --type=ab --check_supported
{
  "debug_message": "Unexpected disk read type",
  "status": "Not supported"
}

# cros-health-tool diag disk_read_v2 --file_size_mib=0 --check_supported
{
  "debug_message": "Test file size should not be zero",
  "status": "Not supported"
}

# cros-health-tool diag disk_read_v2 --file_size_mib=1 --check_supported
{
  "status": "Supported"
}
```

### Cpu cache

Always supported.

### Volume button

Supported only when `has-side-volume-button` is explicitly configured as "true".

You can run the following commands on your DUT:
1. `cros_config /hardware-properties has-side-volume-button` This is helpful to
   understand what the value of `has-side-volume-button` is.
2. `cros-health-tool diag volume_button --button_type=up --check_supported` Use
   this to see if healthd reports the correct support status.

To configure `has-side-volume-button` in Boxster, you can use
`create_volume_button` function defined in [hw_topology.star] to set it up.

### LED lit up

Supported only on a device with a CrOS EC.

You can run the following commands on your DUT:
1. `ls /sys/class/chromeos/cros_ec` This is helpful to understand if the device
   has a CrOS EC. If not, the output will contain `No such file or directory`.
2. `cros-health-tool diag led_lit_up --led_name=power --led_color=red --check_supported`
   Use this to see if healthd reports the correct support status.

### Bluetooth power

Supported only when ChromeOS uses Floss as Bluetooth stack.

[team-contact]: mailto:cros-tdm-tpe-eng@google.com
[cros_healthd_routines.mojom]: /diagnostics/mojom/public/cros_healthd_routines.mojom
[hw_topology.star]: https://chromium.googlesource.com/chromiumos/config/+/refs/heads/main/util/hw_topology.star
