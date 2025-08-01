# Log Entries

This document describes what the log entries mean. Each entry has a matching
section. Please add a section here for new entries and some brief description of
what it means and how to parse it.

## CLIENT_ID

## DEVICETYPE

## LOGDATE

## all_disk_stats

Disk I/O stats for all block devices on the system.

## amd_pmc_idlemask

Value of the SMU idlemask

## amd_s0ix_stats

Contains the time when the system last entered/exited S0ix and how long it
remained in that state.

## amd_smu_fw_info

Contains additional timing information about the last S0ix suspend including
how long it took to enter and exit that state.

## amd_stb

Base64 encoded contents of the smart trace buffer, contains timestamped POST
codes and logs from various other systems. Requires an AMD specific tool to
decode.

## amdgpu_gem_info

## amdgpu_gtt_mm

## amdgpu_vram_mm

## android_app_storage

Gets ARC app disk usage and includes SELinux context for /data files.
This should be empty on virtio-blk devices.

## arcvm_console_output

Dumps kernel logs using [`vm_pstore_dump`](/vm_tools#vm_pstore_dump)
tool. The log is stored in pstore in the guest which is a file on the host side,
similar to [console-ramoops](#console_ramoops). Should show kernel logs when all
else fails inside the guest OS. Timestamp is in seconds from guest Linux kernel
boot.

## arcvm_psi

[Memory PSI] of ARCVM that comes from `/proc/pressure/memory`. [Memory PSI code]

## arcvm_zram_mm_stat

Zram activity of ARCVM which is the output of /sys/block/zram0/mm_stat.

## arcvm_zram_stat

Zram activity of ARCVM which is the output of /sys/block/zram0/stat.

## atmel_tp_deltas

## atmel_tp_refs

## atmel_ts_deltas

## atmel_ts_refs

## atrus_logs

## audit_log

Recent events in audit.log as generated by [auditd(8)].
Only includes audit events of type=SYSCALL and type=AVC (SELinux denials).

## auth_failure

Contains which TPM commands are used and its response when the command failed.

## bio_crypto_init.LATEST

## bio_crypto_init.PREVIOUS

## bio_fw_updater.LATEST

## bio_fw_updater.PREVIOUS

## biod.LATEST

## biod.PREVIOUS

## bios_info

## bios_log

Log entries stored in console section of coreboot memory region. Contains logs
from things such as coreboot and depthcarge before Linux kernel starts.

## bios_stacked_times

Same as bios_times, but in flame graph compatible stacked format and with enum
names instead of human-readable descriptions.

## bios_times

This entry contains timestamps of events saved by BIOS and its components.
Timestamps are in human-readable form with description and value.

## blkid

## bluetooth.log

This file records the debuggable logs generated by Bluetooth daemons
($syslogseverity < '7'), including logs from bluetoothd, btmanagerd, and
btadapterd.

## bootstat_summary

## borealis_crosvm.log

Logs from the Borealis VM's output (kernel logs to serial, in-VM services).

## borealis_fossilize_wrap_log

Logs from Borealis attempts to install shader cache DLCs when fossilize is
called within the VM.

## borealis_frames

Frame timings from apps running in the Borealis VM. 16 KiB.

## borealis_frames_summary

Summarization of borealis frame timings. 2 KiB.

## borealis_launch_log

Logs from Borealis game launches, including command line and compat tools used
for the game launch.

## borealis_proton_crash_reports

Contains the most recent Proton crash dump containing debug info. ~11 KiB.

## borealis_quirks

Contains both system-provided and user-specified configs which control the
behaviour of Borealis system components (initially just Sommelier) on a
per-game basis. For example, a setting might permit a specific game to control
the position of its windows on-screen.

~93 bytes per setting. We print the first 10 KiB, enough for 110 settings.

## borealis_rootfs_reports

Contains MD5 digests for paths in the Borealis read-only rootfs.
This report may inform if the Borealis rootfs has become corrupted. ~44 KiB.

## borealis_steam_log

Contains the stdout and stderr of the Steam client and any games run since
Borealis boot. ~8 KiB for start up and login of the Steam client. Each game
launch adds ~8 KiB.

## borealis_xwindump

If the Borealis VM is running, displays a list of all Borealis windows and
each window's title, size, and other X properties, as seen by the X server.
Expected size is ~20 KiB.

## bt_usb_disconnects

## buddyinfo

Various virtual memory fragmentation details.  See the `/proc/buddyinfo` section
of the [proc(5)] man page for an explanation of each field.

## cbi_info

## cheets_log

## chrome_system_log

## chrome_system_log.PREVIOUS

## chromeos-pgmem

## clobber-state.log

## clobber.log

## console-ramoops

Contains logs from Linux kernel from previous boot, in pstore [ramoops
logger](https://www.kernel.org/doc/html/latest/admin-guide/ramoops.html).

## cpuinfo

Various CPU & system architecture details.  Often shows exact CPU models and
supported hardware flags, as well as how many CPUs and cores that are available.
See the `/proc/cpuinfo` section of the [proc(5)] man page for more details.

## cr50_version

## crdyboot.log

UEFI boot log for ChromeOS Flex.

## critical_disk_stats

Disk I/O stats for block devices assigned to the stateful partition, user home,
and root.

## cros_ec.log

## cros_ec.previous

## cros_ec_panicinfo

## cros_ec_pdinfo

USB-C state returned by the ectool typecstatus and typecdiscovery commands. This
includes the power role, data role, polarity, mux information, source/sink
capabilities and the SOP/SOP' identity/mode responses if available.

## cros_fp.log

## cros_fp.previous

## cros_fp_panicinfo

[FPMCU] [Panic Data] in human readable form.

## cros_ish.log

## cros_ish.previous

## cros_scp.log

## cros_scp.previous

## cros_tp console

## cros_tp frame

## cros_tp version

## crosid

The output of `crosid -v`, which can be used to understand/debug why a
device matched a certain config identity (or, why a device didn't
match one).

## crostini

## crostini_crosvm.log

Logs from the Termina VM's output (kernel logs to serial, in-VM services).

## display-debug

Logs collected from the 'display_debug' crosh tool, such as annotated drm_trace
logs and snapshots of the output of 'modetest'. The drm_trace logs are as
described below, but with additional categories enabled. See
http://go/cros-displaydebug for more details.

## dmesg

Linux kernel logs from the current run. See [console-ramoops](#console_ramoops)
for previous boot.

## drm_gem_objects

## drm_state

## drm_trace

Logs collected from the kernel's drm module. A subset of
[drm_debug_category messages](https://01.org/linuxgraphics/gfx-docs/drm/gpu/drm-internals.html#c.drm_debug_category)
are enabled and the tail of their output is collected here.

## drm_trace_legacy

Same as above, but for older kernel versions using a legacy drm_trace
implementation.

## ec_info

## edid-decode

## eventlog

Shows system events such as when a device was turned on or off, and if a reboot
was user-initiated. Comes from persistent firmware event log.

## extensions.log

Logs collected from extension acting as system extensions (managing login and
sessions).

## file-nr

The number of files opened on the system. Useful when trying to check if it's
the system-wide limit or the per-process limit when failing to open new file
descriptors.

https://docs.kernel.org/admin-guide/sysctl/fs.html#file-max-file-nr

## folder_size_dump

The folder_size_dump helper dumps the actual disk usage (in bytes) of various
system folders by calling
`du --human-readable --total --summarize --one-file-system`.
The list of folders and filtering can be found in `folder_size_dump.cc`. Each
entry calls du individually with a sorted list of subfolders.

The output of the `df` command is available separately for comparison.

0 sized entries are filtered out to reduce the size of the report, this does not
provide a complete folder contents listing.

## folder_size_dump_user

The folder_size_dump helper dumps the actual disk usage (in bytes) of top level
user directories
`du --human-readable --total --summarize --one-file-system`.
The output of the `df` command is available separately for comparison.

## font_info

## framebuffer

## fwupd_log

Logs printed from [fwupd] daemon execution.

## fwupd_state

Current state of the system as reported by [fwupd] daemon.

## fwupd_version

The [fwupd] client and daemon versions.

## gsclog

The Google Security Chip console log.

## hammerd

## hardware_class

## hardware_verification_report

## hostname

## hwsec_status

The internal state of the hwsec daemons.

## hypervisor.log

The kernel log from the ManaTEE hypervisor.

## i915_error_state

Return a (compressed) binary i915\_error\_state.

## i915_error_state_decoded

Return a (compressed) human readable i915\_error\_state decoded using `aubinator_error_decode`.

## i915_gem_gtt

## i915_gem_objects

## ifconfig

## input_devices

## interrupts

Per-CPU interrupt statistics.  See the `/proc/interrupts` section of the
[proc(5)] man page for an explanation of each field.

## iw_list

## kbmcu_info

Information about an MCU controlling an RGB keyboard, including FW version.

## kbmcu_log

Console output of an MCU controlling an RGB keyboard.

## kernel-crashes

## kiosk_apps_log

Application level logs collected from kiosk apps including logs from
browser windows, service workers and secondary extensions.

## logcat

Log (adb logcat) from Android instance in ARC. Note that timestamp timezone is
in local time unlike other logs which are mostly in UTC.

TODO(b/180562941): Migrate to UTC.

## lpstat

Information about connected printer and scanner devices produced by `lpstat -l
-r -v -a -p -o`. Produces ~22 lines of output for each device.

## lsblk

## lsmod

## lspci

Lists PCI devices. Contains output for `lspci`.

## lsusb

Lists USB devices. Contains output for `lsusb` and `lsusb -t` for topology.

## lsusb_verbose

Verbose output of the lsusb tool. Provides more detailed information including
decoded common descriptors of all currently enumerated USB devices.

## ltr_show

[Latency Tolerance Reporting] value of each external components in the
[Intel PCH (Platform Controller Hub)].

## lvs

Information about LVM logical volumes.

## mali_memory

## meminfo

Various memory usage statistics.  See the `/proc/meminfo` section of the
[proc(5)] man page for an explanation of each field.

## mm-esim-status

## mm-status

## mmc_err_stats

Error counters of MMC controllers. Each counter represents the number
of fatal error events that occurred since boot. Error types include
CRC mismatches and transfer timeouts.

## modetest

## mountinfo

File system mount information from the init process's mount namespace. See
https://www.kernel.org/doc/html/latest/filesystems/sharedsubtree.html for what
it means.

## nbr_minios_log

Details of [Network Based Recovery (NBR)] process.

## nbr_update_engine_log

Details of update engine progress during [Network Based Recovery (NBR)].

## nbr_upstart_log

[Upstart] details from [Network Based Recovery (NBR)].

## netlog

## netstat

## network-devices

## network-services

## nvmap_iovmm

## oemdata

## package_cstate_show

Each of [Package Cstate] residencies timer count value.

## pagetypeinfo

## pch_ip_power_gating_status

Whether the Intel proprietary components within the
[Intel PCH (Platform Controller Hub)] is power gated.

## pchg_info

## perf-data

Performance profiling information about how much time the system spends on
various activities (program execution stack traces). The full detail of
can be found in the [Profile protocol buffer message type](
https://github.com/google/pprof/blob/main/proto/profile.proto). This field is
xz-compressed and base64-encoded.

## perfetto-data

A trace timeline of system and kernel performance events. The data is
formatted as a [Perfetto trace protocol buffer message](
https://android.googlesource.com/platform/external/perfetto/) and can be viewed
with the [Perfetto UI](https://ui.perfetto.dev). This field is zstd-compressed
and base64-encoded.

## platform_identity_customization_id

## platform_identity_model

## platform_identity_name

## platform_identity_sku

## platform_identity_whitelabel_tag

## power_supply_info

## power_supply_sysfs

## powerd.LATEST

## powerd.PREVIOUS

## powerd.out

## powerwash_count

## primary_io_devices

For use mostly on chromebox debugging, displays which keyboards/mice system
services are tracking and considering as 'primary' to the device.

## ps

## psi

[Memory PSI] of host, output of `/proc/pressure/memory`.  [Memory PSI code]

## pvs

Information about LVM physical volumes.

## qcom_fw_info

## secagentd

This file records the debuggable logs generated by the secagentd daemon.

## segmentation_feature_level

Return the feature level calculated for this device.
It is produced by the [segmentation library], retrieved by the `feature_check`
command.

## segmentation_scope_level

Return the scope level for this device.
It is produced by the [segmentation library], retrieved by the `feature_check`
command.

## sensor_info

## slabinfo

Kernel memory allocator/cache statistics.  See the [slabinfo(5)] man page for an
explanation of each field.

## stateful_trim_data

## stateful_trim_state

## storage_info

## storage_quota_usage

Lists the quota disk usage information for all users, groups and projects for
the /home directory.

## substate_live_status_registers

The [Modern Standby (S0ix)] is one state of [ACPI]. Also called
"S0 idle low power mode". [Modern Standby (S0ix)] and [Intel S0ix Sub-states]
are triggered when specific conditions within the SoC have been achieved.
Show the status of the low power mode requirements at the time of reading.

## substate_requirements

Display the required power state for various IP blocks to enter a given power
state, and whether or not that was achieved.

## substate_residencies

Each of [Intel S0ix Sub-states] residencies timer count.

## substate_status_registers

Show the status of [Modern Standby (S0ix)] and [Intel S0ix Sub-states]
requirements. They are latched on every [Package Cstate] 10 entry & exit and
[Intel S0ix Sub-states] entry & exit as well

## swap_info

## syslog

## system_log_stats

## threads

## tlsdate

## top io threads

Shows the stats for the top I/O intensive threads, including the thread and
process IDs, the associated command name, and the number of bytes read and
written.

## top memory

## top thread

## touch_fw_version

## tpm-firmware-updater

## typec_connector_class

Information about the state of USB Type-C ports, partners and cables from the
USB Type-C connector class.

## typecd

## ui_log

## uname

Short summary of the current system information from the [uname(1)] command.
It will include the [Linux kernel](https://www.kernel.org/) version (including
the git commit), when the kernel was compiled, and some details for the system's
CPU.

## update_engine.log

Logs from update_engine. Useful to know what version the system upgraded to and
from and when. Shows lsb-release inside the old and new rootfs when updating.

## upstart

## uptime

The current system [uptime(1)] in seconds, including time spent in suspend.
In other words, how long since the system was booted.

## usb4 devices

## user_folder_size_dump

Dumps the size of all folders inside the primary user's folder and the size of
all daemon stores for all mounted users.

## verified boot

## vmlog.1.LATEST

The previous vmlog; see [vmlog.LATEST](#vmlog_LATEST).

## vmlog.1.PREVIOUS

The previous vmlog; see [vmlog.LATEST](#vmlog_LATEST).

## vmlog.LATEST

virtual memory related log written by vmlog_writer, [documented
here](/metrics/README.md#vmlog).

## vmlog.PREVIOUS

The previous vmlog; see [vmlog.LATEST](#vmlog_LATEST).

## vmstat

Various virtual memory statistics.  See the `/proc/vmstat` section of the
[proc(5)] man page for an explanation of each field.

## vpd_2.0

## wakeup_sources

Wakeup sources are devices capable of waking the system from a suspend. Contains
various stats about each wakeup source. See `struct wakeup_source` in
`include/linux/pm_wakeup.h` in the kernel for a description of fields in this
file.

Useful for debugging suspend issues.

## wifi_status_no_anonymize

## zram block device stat names

## zram block device stat values

block I/O statistics for zram, space-delimited, documented at
https://www.kernel.org/doc/html/latest/block/stat.html

Useful to know how many I/O happened and how much time was spent using zram
swap.

## zram new stats names

## zram new stats values

Memory management related statistics for zram from /sys/block/zram0/, Documented
at https://www.kernel.org/doc/html/latest/admin-guide/blockdev/zram.html#stats

Useful to know how much memory is being stored compressed in zram.

[ACPI]: https://en.wikipedia.org/wiki/ACPI
[auditd(8)]: https://man7.org/linux/man-pages/man8/auditd.8.html
[crdyboot]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/HEAD:src/platform/crdyboot
[FPMCU]: https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/docs/fingerprint/fingerprint.md
[fwupd]: https://github.com/fwupd/fwupd
[Intel PCH (Platform Controller Hub)]: https://en.wikipedia.org/wiki/Platform_Controller_Hub
[Latency Tolerance Reporting]: https://pcisig.com/latency-tolerance-reporting
[Memory PSI]: https://docs.kernel.org/accounting/psi.html
[Memory PSI code]: https://chromium.googlesource.com/chromiumos/third_party/kernel/+/v6.1/kernel/sched/psi.c#1143
[Modern Standby (S0ix)]: https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/modern-standby
[Network Based Recovery (NBR)]: https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/minios/README.md
[Package Cstate]: https://edc.intel.com/content/www/us/en/design/ipla/software-development-platforms/client/platforms/alder-lake-desktop/12th-generation-intel-core-processors-datasheet-volume-1-of-2/001/package-c-states/
[Panic Data]: https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/README.md#Panicinfo
[proc(5)]: https://man7.org/linux/man-pages/man5/proc.5.html
[Intel S0ix Sub-states]: https://edc.intel.com/content/www/tw/zh/design/ipla/software-development-platforms/client/platforms/tiger-lake-mobile-y/intel-500-series-chipset-family-on-package-platform-controller-hub-datasheet-v/005/power-management-sub-state/
[segmentation library]: /libsegmentation/README.md
[slabinfo(5)]: https://man7.org/linux/man-pages/man5/slabinfo.5.html
[uname(1)]: https://man7.org/linux/man-pages/man1/uname.1.html
[Upstart]: https://en.wikipedia.org/wiki/Upstart_(software)
[uptime(1)]: https://man7.org/linux/man-pages/man1/uptime.1.html
