# Log Entries

This document describes what the log entries mean. Each entry has a matching
section. Please add a section here for new entries and some brief description of
what it means and how to parse it.

## CLIENT_ID

## DEVICETYPE

## LOGDATE

## amdgpu_gem_info

## amdgpu_gtt_mm

## amdgpu_vram_mm

## android_app_storage

## arcvm_console_output

## atmel_tp_deltas

## atmel_tp_refs

## atmel_ts_deltas

## atmel_ts_refs

## atrus_logs

## authpolicy

## bio_crypto_init.LATEST

## bio_crypto_init.PREVIOUS

## bio_fw_updater.LATEST

## bio_fw_updater.PREVIOUS

## biod.LATEST

## biod.PREVIOUS

## bios_info

## bios_log

## bios_times

## blkid

## bootstat_summary

## borealis_frames

Frame timings from apps running in the Borealis VM. 16 KiB.

## borealis_xwindump

If the Borealis VM is running, displays a list of all Borealis windows and
each window's title, size, and other X properties, as seen by the X server.
Expected size is ~20 KiB.

## bt_usb_disconnects

## buddyinfo

## cbi_info

## cheets_log

## chrome_system_log

## chrome_system_log.PREVIOUS

## chromeos-pgmem

## clobber-state.log

## clobber.log

## console-ramoops

## cpuinfo

## cr50_version

## cros_ec.log

## cros_ec.previous

## cros_ec_panicinfo

## cros_ec_pdinfo

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

## crostini

## crosvm.log

## dmesg

## drm_gem_objects

## drm_state

## drm_trace

## drm_trace_legacy

## ec_info

## edid-decode

## eventlog

## font_info

## framebuffer

## fwupd_state

## hammerd

## hardware_class

## hardware_verification_report

## hostname

## i915_error_state

## i915_gem_gtt

## i915_gem_objects

## ifconfig

## input_devices

## interrupts

## iw_list

## kernel-crashes

## logcat

## lsblk

## lsmod

## lspci

## lsusb

## mali_memory

## memd clips

## memd.parameters

## meminfo

## memory_spd_info

## mm-esim-status

## mm-status

## modetest

## mount-encrypted

## mountinfo

File system mount information from the init process's mount namespace. See
https://www.kernel.org/doc/html/latest/filesystems/sharedsubtree.html for what
it means.

## netlog

## netstat

## network-devices

## network-services

## nvmap_iovmm

## oemdata

## pagetypeinfo

## pchg_info

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

## ps

## qcom_fw_info

## sensor_info

## slabinfo

## stateful_trim_data

## stateful_trim_state

## storage_info

## swap_info

## syslog

## system_log_stats

## threads

## tlsdate

## top memory

## top thread

## touch_fw_version

## tpm-firmware-updater

## tpm_version

## typecd

## ui_log

## uname

## update_engine.log

## upstart

## uptime

## usb4 devices

## verified boot

## vmlog.1.LATEST

## vmlog.1.PREVIOUS

## vmlog.LATEST

## vmlog.PREVIOUS

## vmstat

## vpd_2.0

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

[FPMCU]: https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/docs/fingerprint/fingerprint.md
[Panic Data]: https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/README.md#Panicinfo
