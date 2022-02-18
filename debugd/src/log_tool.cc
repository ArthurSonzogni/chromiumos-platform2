// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/log_tool.h"

#include <array>
#include <glob.h>
#include <grp.h>
#include <inttypes.h>
#include <lzma.h>
#include <memory>
#include <pwd.h>
#include <stdint.h>
#include <string>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include <base/base64.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/strings/utf_string_conversion_utils.h>
#include <base/values.h>

#include <chromeos/dbus/service_constants.h>
#include <shill/dbus-proxies.h>

#include "debugd/src/bluetooth_utils.h"
#include "debugd/src/constants.h"
#include "debugd/src/metrics.h"
#include "debugd/src/perf_tool.h"
#include "debugd/src/process_with_output.h"

#include <brillo/files/safe_fd.h>
#include <brillo/files/file_util.h>
#include "brillo/key_value_store.h"
#include <brillo/osrelease_reader.h>
#include <brillo/cryptohome.h>

namespace debugd {

using std::string;

using Strings = std::vector<string>;

namespace {

const char kRoot[] = "root";
const char kShell[] = "/bin/sh";
constexpr char kLpAdmin[] = "lpadmin";
constexpr char kLpGroup[] = "lp";
constexpr char kLsbReleasePath[] = "/etc/lsb-release";
constexpr char kArcBugReportBackupFileName[] = "arc-bugreport.log";
constexpr char kArcBugReportBackupKey[] = "arc-bugreport-backup";
constexpr char kDaemonStoreBaseDir[] = "/run/daemon-store/debugd/";

// Minimum time in seconds needed to allow shill to test active connections.
const int kConnectionTesterTimeoutSeconds = 5;

// Default running perf for 2 seconds.
constexpr const int kPerfDurationSecs = 2;
// TODO(chinglinyu) Remove after crbug/934702 is fixed.
// The following description is added to 'perf-data' as a temporary solution
// before the update of feedback disclosure to users is done in crbug/934702.
constexpr const char kPerfDataDescription[] =
    "perf-data contains performance profiling information about how much time "
    "the system spends on various activities (program execution stack traces). "
    "This might reveal some information about what system features and "
    "resources are being used. The full detail of perf-data can be found in "
    "the PerfDataProto protocol buffer message type in the chromium source "
    "repository.\n";

#define CMD_KERNEL_MODULE_PARAMS(module_name) \
  "cd /sys/module/" #module_name "/parameters 2>/dev/null && grep -sH ^ *"

using Log = LogTool::Log;
constexpr Log::LogType kCommand = Log::kCommand;
constexpr Log::LogType kFile = Log::kFile;
constexpr Log::LogType kGlob = Log::kGlob;

class ArcBugReportLog : public LogTool::Log {
 public:
  ArcBugReportLog()
      : Log(kCommand,
            "arc-bugreport",
            "/usr/bin/nsenter -t1 -m /usr/sbin/android-sh -c "
            "/system/bin/arc-bugreport",
            kRoot,
            kRoot,
            10 * 1024 * 1024 /*10 MiB*/,
            LogTool::Encoding::kUtf8) {}

  virtual ~ArcBugReportLog() = default;
};

// NOTE: IF YOU ADD AN ENTRY TO THIS LIST, PLEASE:
// * add a row to http://go/cros-feedback-audit and fill it out
// * email cros-feedback-app@
// (Eventually we'll have a better process, but for now please do this.)
// clang-format off
const std::array kCommandLogs {
  // We need to enter init's mount namespace because it has /home/chronos
  // mounted which is where the consent knob lives.  We don't have that mount
  // in our own mount namespace (by design).  https://crbug.com/884249
  Log{kCommand, "CLIENT_ID",
    "/usr/bin/nsenter -t1 -m /usr/bin/metrics_client -i", kRoot, kDebugfsGroup},
  // We consistently use UTC in feedback reports.
  Log{kCommand, "LOGDATE", "/bin/date --utc; /bin/date"},
  Log{kFile, "amdgpu_gem_info", "/sys/kernel/debug/dri/0/amdgpu_gem_info",
    SandboxedProcess::kDefaultUser, kDebugfsGroup},
  Log{kFile, "amdgpu_gtt_mm", "/sys/kernel/debug/dri/0/amdgpu_gtt_mm",
    SandboxedProcess::kDefaultUser, kDebugfsGroup},
  Log{kFile, "amdgpu_vram_mm", "/sys/kernel/debug/dri/0/amdgpu_vram_mm",
    SandboxedProcess::kDefaultUser, kDebugfsGroup},
  // Show du and ls results for dirs under /home/root/*/android-data/data/.
  // We need to enter init's mount namespace to access /home/root. Also, we use
  // neither ARC container's mount namespace (with android-sh) nor
  // /opt/google/containers/android/rootfs/android-data/ so that we can get
  // results even when the container is down.
  Log{kCommand, "android_app_storage", "/usr/bin/nsenter -t1 -m /bin/sh -c \""
    "du -h --one-file-system --max-depth 3 /home/root/*/android-data/data/;"
    "find /home/root/*/android-data/data/ -xdev -type d -maxdepth 3 "
    "-exec ls -dlZ --time-style='+' {} + | tr -s ' ' '\t' \"",
    kRoot, kDebugfsGroup},
#if USE_ARCVM
  Log{kCommand, "arcvm_console_output", "/usr/bin/vm_pstore_dump", "crosvm",
    "crosvm", Log::kDefaultMaxBytes, LogTool::Encoding::kAutodetect,
    true /* access_root_mount_ns */},
#endif  // USE_ARCVM
  Log{kCommand, "atmel_tp_deltas",
    "/opt/google/touch/scripts/atmel_tools.sh tp d", kRoot, kRoot},
  Log{kCommand, "atmel_tp_refs",
    "/opt/google/touch/scripts/atmel_tools.sh tp r", kRoot, kRoot},
  Log{kCommand, "atmel_ts_deltas",
    "/opt/google/touch/scripts/atmel_tools.sh ts d", kRoot, kRoot},
  Log{kCommand, "atmel_ts_refs",
    "/opt/google/touch/scripts/atmel_tools.sh ts r", kRoot, kRoot},
  Log{kFile, "atrus_logs", "/var/log/atrus.log"},
  Log{kCommand, "audit_log", "/usr/libexec/debugd/helpers/audit_log_filter",
    kRoot, kDebugfsGroup},
  Log{kFile, "authpolicy", "/var/log/authpolicy.log"},
  Log{kFile, "bio_crypto_init.LATEST",
    "/var/log/bio_crypto_init/bio_crypto_init.LATEST"},
  Log{kFile, "bio_crypto_init.PREVIOUS",
    "/var/log/bio_crypto_init/bio_crypto_init.PREVIOUS"},
  Log{kFile, "bio_fw_updater.LATEST", "/var/log/biod/bio_fw_updater.LATEST"},
  Log{kFile, "bio_fw_updater.PREVIOUS",
    "/var/log/biod/bio_fw_updater.PREVIOUS"},
  Log{kFile, "biod.LATEST", "/var/log/biod/biod.LATEST"},
  Log{kFile, "biod.PREVIOUS", "/var/log/biod/biod.PREVIOUS"},
  Log{kFile, "bios_info", "/var/log/bios_info.txt"},
  Log{kCommand, "bios_log", "cat /sys/firmware/log "
    "/proc/device-tree/chosen/ap-console-buffer 2>/dev/null"},
  Log{kFile, "bios_times", "/var/log/bios_times.txt"},
  // Slow or non-responsive block devices could cause this command to stall. Use
  // a timeout to prevent this command from blocking log fetching. This command
  // is expected to take O(100ms) in the normal case.
  Log{kCommand, "blkid", "timeout -s KILL 5s /sbin/blkid", kRoot, kRoot},
  Log{kCommand, "bootstat_summary", "/usr/bin/bootstat_summary",
    SandboxedProcess::kDefaultUser, SandboxedProcess::kDefaultGroup,
    Log::kDefaultMaxBytes, LogTool::Encoding::kAutodetect,
    true /* access_root_mount_ns */},
  Log{kCommand, "bt_usb_disconnects",
    "/usr/libexec/debugd/helpers/bt_usb_disconnect_helper",
    SandboxedProcess::kDefaultUser, kDebugfsGroup},
  Log{kFile, "buddyinfo", "/proc/buddyinfo"},
  Log{kCommand, "cbi_info", "/usr/share/userfeedback/scripts/cbi_info", kRoot,
    kRoot},
  Log{kFile, "cheets_log", "/var/log/arc.log"},
  Log{kFile, "chrome_system_log", "/var/log/chrome/chrome"},
  Log{kFile, "chrome_system_log.PREVIOUS", "/var/log/chrome/chrome.PREVIOUS"},
  Log{kCommand, "chromeos-pgmem", "/usr/bin/chromeos-pgmem", kRoot, kRoot},
  Log{kFile, "clobber-state.log", "/var/log/clobber-state.log"},
  Log{kFile, "clobber.log", "/var/log/clobber.log"},
  // There might be more than one record, so grab them all.
  // Plus, for <linux-3.19, it's named "console-ramoops", but for newer
  // versions, it's named "console-ramoops-#".
  Log{kGlob, "console-ramoops", "/sys/fs/pstore/console-ramoops*",
    SandboxedProcess::kDefaultUser, kPstoreAccessGroup },
  Log{kFile, "cpuinfo", "/proc/cpuinfo"},
  Log{kFile, "cr50_version", "/var/cache/cr50-version"},
  Log{kFile, "cros_ec.log", "/var/log/cros_ec.log",
    SandboxedProcess::kDefaultUser, SandboxedProcess::kDefaultGroup,
    Log::kDefaultMaxBytes, LogTool::Encoding::kUtf8},
  Log{kFile, "cros_ec.previous", "/var/log/cros_ec.previous",
    SandboxedProcess::kDefaultUser, SandboxedProcess::kDefaultGroup,
    Log::kDefaultMaxBytes, LogTool::Encoding::kUtf8},
  Log{kFile, "cros_ec_panicinfo", "/sys/kernel/debug/cros_ec/panicinfo",
    SandboxedProcess::kDefaultUser, kDebugfsGroup, Log::kDefaultMaxBytes,
    LogTool::Encoding::kBase64},
  Log{kCommand, "cros_ec_pdinfo",
    "for port in 0 1 2 3 4 5 6 7 8; do "
      "echo \"-----------\"; "
      // stderr output just tells us it failed
      "ectool usbpd \"${port}\" 2>/dev/null || break; "
    "done", kRoot, kRoot},
  Log{kFile, "cros_fp.log", "/var/log/cros_fp.log",
    SandboxedProcess::kDefaultUser, SandboxedProcess::kDefaultGroup,
    Log::kDefaultMaxBytes, LogTool::Encoding::kUtf8},
  Log{kFile, "cros_fp.previous", "/var/log/cros_fp.previous",
    SandboxedProcess::kDefaultUser, SandboxedProcess::kDefaultGroup,
    Log::kDefaultMaxBytes, LogTool::Encoding::kUtf8},
  Log{kCommand, "cros_fp_panicinfo", "ectool --name=cros_fp panicinfo",
    kRoot, kRoot},
  Log{kFile, "cros_ish.log", "/var/log/cros_ish.log",
    SandboxedProcess::kDefaultUser, SandboxedProcess::kDefaultGroup,
    Log::kDefaultMaxBytes, LogTool::Encoding::kUtf8},
  Log{kFile, "cros_ish.previous", "/var/log/cros_ish.previous",
    SandboxedProcess::kDefaultUser, SandboxedProcess::kDefaultGroup,
    Log::kDefaultMaxBytes, LogTool::Encoding::kUtf8},
  Log{kFile, "cros_scp.log", "/var/log/cros_scp.log",
    SandboxedProcess::kDefaultUser, SandboxedProcess::kDefaultGroup,
    64 * 1024, LogTool::Encoding::kUtf8},
  Log{kFile, "cros_scp.previous", "/var/log/cros_scp.previous",
    SandboxedProcess::kDefaultUser, SandboxedProcess::kDefaultGroup,
    64 * 1024, LogTool::Encoding::kUtf8},
  Log{kCommand, "cros_tp console", "/usr/sbin/ectool --name=cros_tp console",
    kRoot, kRoot},
  Log{kCommand, "cros_tp frame", "/usr/sbin/ectool --name=cros_tp tpframeget",
    kRoot, kRoot},
  Log{kFile, "cros_tp version", "/sys/class/chromeos/cros_tp/version"},
  Log{kCommand, "crostini", "/usr/bin/cicerone_client --get_info"},
  Log{kCommand, "crosvm.log", "nsenter -t1 -m /bin/sh -c 'tail -n+1"
    " /run/daemon-store/crosvm/*/log/*.log.1"
    " /run/daemon-store/crosvm/*/log/*.log'", kRoot, kRoot},
  Log{kGlob, "display-debug", "/var/log/display_debug/*",
    kRoot, kRoot,
    4 * 1024 * 1024, LogTool::Encoding::kUtf8},
  // dmesg: add full timestamps to dmesg to match other logs.
  // 'dmesg' needs CAP_SYSLOG.
  Log{kCommand, "dmesg", "TZ=UTC /bin/dmesg --raw --time-format iso",
    kRoot, kRoot},
  Log{kGlob, "drm_gem_objects", "/sys/kernel/debug/dri/?/gem",
    SandboxedProcess::kDefaultUser, kDebugfsGroup},
  Log{kGlob, "drm_state", "/sys/kernel/debug/dri/?/state",
    SandboxedProcess::kDefaultUser, kDebugfsGroup},
  Log{kFile, "drm_trace", "/sys/kernel/debug/tracing/instances/drm/trace",
    SandboxedProcess::kDefaultUser, kDebugfsGroup},
  // TODO(seanpaul): Once we've finished moving over to the upstream tracefs
  //                 implementation, remove drm_trace_legacy. Tracked in
  //                 b/163580546.
  Log{kFile, "drm_trace_legacy", "/sys/kernel/debug/dri/trace",
    SandboxedProcess::kDefaultUser, kDebugfsGroup},
  Log{kFile, "ec_info", "/var/log/ec_info.txt"},
  Log{kCommand, "edid-decode",
    "for f in /sys/class/drm/card?-*/edid; do "
      "echo \"----------- ${f}\"; "
      // edid-decode's stderr output is redundant, so silence it.
      "edid-decode --skip-hex-dump \"${f}\" 2>/dev/null; "
    "done"},
  Log{kFile, "eventlog", "/var/log/eventlog.txt"},
  Log{kCommand, "folder_size_dump",
    "/usr/libexec/debugd/helpers/folder_size_dump --system",
    kRoot, kRoot, 1 * 1024 * 1024 /* 1 MiB*/, LogTool::Encoding::kUtf8, true},
  Log{kCommand, "font_info", "/usr/share/userfeedback/scripts/font_info"},
  Log{kGlob, "framebuffer", "/sys/kernel/debug/dri/?/framebuffer",
    SandboxedProcess::kDefaultUser, kDebugfsGroup},
  Log{kCommand, "fwupd_state",
    "/usr/bin/fwupdmgr get-devices --json | sed '/\"Serial\" :/d'",
    kRoot, kRoot},
  Log{kFile, "hammerd", "/var/log/hammerd.log"},
  Log{kCommand, "hardware_class", "/usr/bin/crossystem hwid"},
  Log{kFile, "hardware_verification_report",
    "/var/cache/hardware_verifier.result"},
  Log{kCommand, "hostname", "/bin/hostname"},
  Log{kCommand, "i915_error_state",
    "/usr/bin/xz -c /sys/kernel/debug/dri/0/i915_error_state 2>/dev/null",
    SandboxedProcess::kDefaultUser, kDebugfsGroup, Log::kDefaultMaxBytes,
    LogTool::Encoding::kBase64},
  Log{kFile, "i915_gem_gtt", "/sys/kernel/debug/dri/0/i915_gem_gtt",
    SandboxedProcess::kDefaultUser, kDebugfsGroup},
  Log{kFile, "i915_gem_objects", "/sys/kernel/debug/dri/0/i915_gem_objects",
    SandboxedProcess::kDefaultUser, kDebugfsGroup},
  Log{kCommand, "ifconfig", "/bin/ifconfig -a"},
  Log{kFile, "input_devices", "/proc/bus/input/devices"},
  // Hardware capabilities of the wiphy device.
  Log{kFile, "interrupts", "/proc/interrupts"},
  Log{kCommand, "iw_list", "/usr/sbin/iw list"},
#if USE_IWLWIFI_DUMP
  Log{kCommand, "iwlmvm_module_params", CMD_KERNEL_MODULE_PARAMS(iwlmvm)},
  Log{kCommand, "iwlwifi_module_params", CMD_KERNEL_MODULE_PARAMS(iwlwifi)},
#endif  // USE_IWLWIFI_DUMP
  Log{kGlob, "kernel-crashes", "/var/spool/crash/kernel.*.kcrash",
    SandboxedProcess::kDefaultUser, "crash-access"},
  Log{kCommand, "lpstat", "/usr/bin/lpstat -l -r -v -a -p -o",
    kLpAdmin, kLpGroup},
  Log{kCommand, "lsblk", "timeout -s KILL 5s lsblk -a", kRoot, kRoot,
    Log::kDefaultMaxBytes, LogTool::Encoding::kAutodetect,
    true /* access_root_mount_ns */},
  Log{kCommand, "lsmod", "lsmod"},
  Log{kCommand, "lsusb", "lsusb && lsusb -t"},
  Log{kCommand, "lvs", "lvs --all --readonly --reportformat json -o lv_all",
    kRoot, kRoot, 1 * 1024 * 1024 /* 1 MiB */, LogTool::Encoding::kUtf8, true},
  Log{kFile, "mali_memory", "/sys/kernel/debug/mali0/gpu_memory",
    SandboxedProcess::kDefaultUser, kDebugfsGroup},
  Log{kGlob, "memd clips", "/var/log/memd/memd.clip*"},
  Log{kFile, "memd.parameters", "/var/log/memd/memd.parameters"},
  Log{kFile, "meminfo", "/proc/meminfo"},
  Log{kCommand, "memory_spd_info",
    // mosys may use 'i2c-dev', which may not be loaded yet.
    "modprobe i2c-dev 2>/dev/null && mosys -l memory spd print all 2>/dev/null",
    kRoot, kDebugfsGroup},
  // The sed command finds the EDID blob (starting the line after "value:") and
  // replaces the serial number with all zeroes.
  //
  // The EDID is printed as a hex dump over several lines, each line containing
  // the contents of 16 bytes. The first 16 bytes are broken down as follows:
  //   uint64_t fixed_pattern;      // Always 00 FF FF FF FF FF FF 00.
  //   uint16_t manufacturer_id;    // Manufacturer ID, encoded as PNP IDs.
  //   uint16_t product_code;       // Manufacturer product code, little-endian.
  //   uint32_t serial_number;      // Serial number, little-endian.
  // Source: https://en.wikipedia.org/wiki/EDID#EDID_1.3_data_format
  //
  // The subsequent substitution command looks for the fixed pattern followed by
  // two 32-bit fields (manufacturer + product, serial number). It replaces the
  // latter field with 8 bytes of zeroes.
  //
  // TODO(crbug.com/731133): Remove the sed command once modetest itself can
  // remove serial numbers.
  Log{kCommand, "modetest",
    "(modetest; modetest -M evdi; modetest -M udl) | "
    "sed -E '/EDID/ {:a;n;/value:/!ba;n;"
    "s/(00f{12}00)([0-9a-f]{8})([0-9a-f]{8})/\\1\\200000000/}'",
    kRoot, kRoot},
  Log{kFile, "mount-encrypted", "/var/log/mount-encrypted.log"},
  Log{kFile, "mountinfo", "/proc/1/mountinfo"},
  Log{kCommand, "netlog",
    "/usr/share/userfeedback/scripts/getmsgs /var/log/net.log",
    SandboxedProcess::kDefaultUser, SandboxedProcess::kDefaultGroup,
    Log::kDefaultMaxBytes, LogTool::Encoding::kUtf8},
  Log{kFile, "nvmap_iovmm", "/sys/kernel/debug/nvmap/iovmm/allocations",
    SandboxedProcess::kDefaultUser, kDebugfsGroup},
  Log{kCommand, "oemdata", "/usr/share/cros/oemdata.sh", kRoot, kRoot},
  Log{kFile, "pagetypeinfo", "/proc/pagetypeinfo", kRoot},
  Log{kCommand, "pchg_info", "/usr/share/userfeedback/scripts/pchg_info",
    kRoot, kRoot},
  Log{kFile, "platform_identity_customization_id",
    "/run/chromeos-config/v1/identity/customization-id"},
  Log{kFile, "platform_identity_model", "/run/chromeos-config/v1/name"},
  Log{kFile, "platform_identity_name",
    "/run/chromeos-config/v1/identity/platform-name"},
  Log{kFile, "platform_identity_sku",
    "/run/chromeos-config/v1/identity/sku-id"},
  Log{kFile, "platform_identity_whitelabel_tag",
    "/run/chromeos-config/v1/identity/whitelabel-tag"},
  Log{kCommand, "power_supply_info", "/usr/bin/power_supply_info"},
  Log{kCommand, "power_supply_sysfs", "/usr/bin/print_sysfs_power_supply_data"},
  Log{kFile, "powerd.LATEST", "/var/log/power_manager/powerd.LATEST"},
  Log{kFile, "powerd.PREVIOUS", "/var/log/power_manager/powerd.PREVIOUS"},
  Log{kFile, "powerd.out", "/var/log/powerd.out"},
  Log{kFile, "powerwash_count", "/var/log/powerwash_count"},
  Log{kCommand, "ps", "/bin/ps auxZ"},
  Log{kCommand, "pvs", "pvs --all --readonly --reportformat json -o pv_all",
    kRoot, kRoot, 1 * 1024 * 1024 /* 1 MiB*/, LogTool::Encoding::kUtf8, true},
  Log{kGlob, "qcom_fw_info", "/sys/kernel/debug/qcom_socinfo/*/*",
    SandboxedProcess::kDefaultUser, kDebugfsGroup},
  Log{kCommand, "sensor_info", "/usr/share/userfeedback/scripts/sensor_info"},
  // /proc/slabinfo is owned by root and has 0400 permission.
  Log{kFile, "slabinfo", "/proc/slabinfo", kRoot, kRoot},
  Log{kFile, "stateful_trim_data", "/var/lib/trim/stateful_trim_data"},
  Log{kFile, "stateful_trim_state", "/var/lib/trim/stateful_trim_state"},
  Log{kFile, "storage_info", "/var/log/storage_info.txt"},
  Log{kCommand, "swap_info", "/usr/share/cros/init/swap.sh status 2>/dev/null",
    SandboxedProcess::kDefaultUser, kDebugfsGroup},
  Log{kCommand, "syslog",
    "/usr/share/userfeedback/scripts/getmsgs /var/log/messages"},
  Log{kCommand, "system_log_stats",
    "echo 'BLOCK_SIZE=1024'; "
    "find /var/log/ -type f -exec du --block-size=1024 {} + | sort -n -r",
    kRoot, kRoot},
  Log{kCommand, "threads",
    "/bin/ps -T axo pid,ppid,spid,pcpu,ni,stat,time,comm"},
  Log{kFile, "tlsdate", "/var/log/tlsdate.log"},
  Log{kCommand, "top memory",
    "/usr/bin/top -o \"+%MEM\" -w128 -bcn 1 | head -n 57"},
  Log{kCommand, "top thread", "/usr/bin/top -Hbc -w128 -n 1 | head -n 40"},
  Log{kCommand, "touch_fw_version",
    "grep -aE"
    " -e 'synaptics: Touchpad model'"
    " -e 'chromeos-[a-z]*-touch-[a-z]*-update'"
    " /var/log/messages | tail -n 20"},
  Log{kCommand, "tpm-firmware-updater",
    "/usr/share/userfeedback/scripts/getmsgs "
    "/var/log/tpm-firmware-updater.log"},
  // TODO(jorgelo,mnissler): Don't run this as root.
  // On TPM 1.2 devices this will likely require adding a new user to the 'tss'
  // group.
  // On TPM 2.0 devices 'get_version_info' uses D-Bus and therefore can run as
  // any user.
  Log{kCommand, "tpm_version", "/usr/sbin/tpm-manager get_version_info", kRoot,
    kRoot},
  // Type-C data from the type-c connector class, VID/PIDs are obfuscated.
  Log{kCommand, "typec_connector_class",
    "/usr/libexec/debugd/helpers/typec_connector_class_helper"},
  // typecd logs average around 56K. VID/PIDs are obfuscated from the printed
  // PD identity information.
  Log{kFile, "typecd", "/var/log/typecd.log"},
  Log{kFile, "ui_log", "/var/log/ui/ui.LATEST"},
  Log{kCommand, "uname", "/bin/uname -a"},
  Log{kCommand, "update_engine.log",
    "cat $(ls -1tr /var/log/update_engine | tail -5 | sed"
    " s.^./var/log/update_engine/.)"},
  Log{kFile, "upstart", "/var/log/upstart.log"},
  Log{kCommand, "uptime", "/usr/bin/cut -d' ' -f1 /proc/uptime"},
  Log{kCommand, "usb4 devices",
    "/usr/libexec/debugd/helpers/usb4_devinfo_helper", kRoot, kDebugfsGroup},
  Log{kFile, "verified boot", "/var/log/debug_vboot_noisy.log"},
  Log{kFile, "vmlog.1.LATEST", "/var/log/vmlog/vmlog.1.LATEST"},
  Log{kFile, "vmlog.1.PREVIOUS", "/var/log/vmlog/vmlog.1.PREVIOUS"},
  Log{kFile, "vmlog.LATEST", "/var/log/vmlog/vmlog.LATEST"},
  Log{kFile, "vmlog.PREVIOUS", "/var/log/vmlog/vmlog.PREVIOUS"},
  Log{kFile, "vmstat", "/proc/vmstat"},
  Log{kFile, "vpd_2.0", "/var/log/vpd_2.0.txt"},
  Log{kCommand, "zram block device stat names",
    "echo read_ios read_merges read_sectors read_ticks write_ios "
    "write_merges write_sectors write_ticks in_flight io_ticks "
    "time_in_queue discard_ios dicard_merges discard_sectors discard_ticks "
    "flush_ios flush_ticks"},
  Log{kFile, "zram block device stat values", "/sys/block/zram0/stat"},
  Log{kCommand, "zram new stats names",
    "echo orig_size compr_size used_total limit used_max zero_pages migrated"},
  Log{kFile, "zram new stats values", "/sys/block/zram0/mm_stat"},
  // Stuff pulled out of the original list. These need access to the running X
  // session, which we'd rather not give to debugd, or return info specific to
  // the current session (in the setsid(2) sense), which is not useful for
  // debugd
  // Log{kCommand, "env", "set"},
  // Log{kCommand, "setxkbmap", "/usr/bin/setxkbmap -print -query"},
  // Log{kCommand, "xrandr", "/usr/bin/xrandr --verbose}
};
// clang-format on

// NOTE: IF YOU ADD AN ENTRY TO THIS LIST, PLEASE:
// * add a row to http://go/cros-feedback-audit and fill it out
// * email cros-feedback-app@
// (Eventually we'll have a better process, but for now please do this.)
const std::array kCommandLogsVerbose{
    // PCI config space accesses are limited without CAP_SYS_ADMIN.
    Log{kCommand, "lspci_verbose", "/usr/sbin/lspci -vvvnn", kRoot, kRoot},
};

// NOTE: IF YOU ADD AN ENTRY TO THIS LIST, PLEASE:
// * add a row to http://go/cros-feedback-audit and fill it out
// * email cros-feedback-app@
// (Eventually we'll have a better process, but for now please do this.)
const std::array kCommandLogsShort{
    Log{kCommand, "lspci", "/usr/sbin/lspci"},
};

// Extra logs are logs such as netstat and logcat which should appear in
// chrome://system but not in feedback reports. Open sockets may have privacy
// implications, and logcat is already incorporated via arc-bugreport.
// NOTE: IF YOU ADD AN ENTRY TO THIS LIST, PLEASE:
// * add a row to http://go/cros-feedback-audit and fill it out
// * email cros-feedback-app@
// (Eventually we'll have a better process, but for now please do this.)
//
// clang-format off
const std::array kExtraLogs {
  Log{kCommand, "logcat",
    "/usr/bin/nsenter -t1 -m /usr/sbin/android-sh -c '/system/bin/logcat -d'",
    kRoot, kRoot, Log::kDefaultMaxBytes, LogTool::Encoding::kUtf8},
#if USE_CELLULAR
  Log{kCommand, "mm-esim-status", "/usr/bin/modem esim status"},
  Log{kCommand, "mm-status", "/usr/bin/modem status"},
#endif  // USE_CELLULAR
  // --processes requires root.
  Log{kCommand, "netstat",
    "/sbin/ss --all --query inet --numeric --processes", kRoot, kRoot},
  Log{kCommand, "network-devices", "/usr/bin/connectivity show devices"},
  Log{kCommand, "network-services", "/usr/bin/connectivity show services"},
  // This includes unfiltered user PII, so do not include in feedback reports.
  Log{kCommand, "user_folder_size_dump",
    "/usr/libexec/debugd/helpers/folder_size_dump --user",
    kRoot, kRoot, 1 * 1024 * 1024 /* 1 MiB*/, LogTool::Encoding::kUtf8, true},
  Log{kCommand, "wifi_status_no_anonymize",
    "/usr/bin/network_diag --wifi-internal --no-log"},
};
// clang-format on

// NOTE: IF YOU ADD AN ENTRY TO THIS LIST, PLEASE:
// * add a row to http://go/cros-feedback-audit and fill it out
// * email cros-feedback-app@
// (Eventually we'll have a better process, but for now please do this.)
// clang-format off
const std::array kFeedbackLogs {
  Log{kFile, "auth_failure", "/var/log/tcsd/auth_failure.permanent"},
  Log{kCommand, "borealis_frames", "timeout -s KILL 5s /usr/bin/borealis-sh "
    "-- /usr/bin/get-frame-log.sh", kRoot, kRoot},
  Log{kCommand, "borealis_xwindump", "timeout -s KILL 5s /usr/bin/borealis-sh "
    "-- /usr/bin/xwindump.py", kRoot, kRoot},
  Log{kGlob, "iwlwifi_firmware_version",
    "/sys/kernel/debug/iwlwifi/*/iwlmvm/fw_ver", kRoot, kRoot},
  Log{kCommand, "iwlwifi_sysasserts",
    "croslog --show-cursor=false --identifier=kernel --priority=err"
    "  --grep='iwlwifi.*ADVANCED_SYSASSERT' --quiet | tail -n 3"},
  Log{kCommand, "iwlwifi_sysasserts_count",
    "croslog --show-cursor=false --identifier=kernel --priority=err"
    "  --grep='iwlwifi.*ADVANCED_SYSASSERT' | wc -l"},
#if USE_CELLULAR
  Log{kCommand, "mm-esim-status", "/usr/bin/modem esim status_feedback"},
  Log{kCommand, "mm-status", "/usr/bin/modem status-feedback"},
#endif  // USE_CELLULAR
  Log{kCommand, "network-devices",
      "/usr/bin/connectivity show-feedback devices"},
  Log{kCommand, "network-services",
      "/usr/bin/connectivity show-feedback services"},
  Log{kCommand, "shill_connection_diagnostic",
    "croslog --show-cursor=false --identifier=shill"
    "  --grep='Connection issue:' --quiet | tail -n 3"},
  Log{kCommand, "wifi_connection_attempts",
    "croslog --show-cursor=false --identifier=kernel"
    "  --grep='(authenticate|associate) with' | wc -l"},
  Log{kCommand, "wifi_connection_timeouts",
    "croslog --show-cursor=false --identifier=kernel"
    "  --grep='(authentication|association).*timed out' | wc -l"},
  Log{kCommand, "wifi_driver_errors",
    "croslog --show-cursor=false --identifier=kernel --priority=err"
    "  --grep='(iwlwifi|mwifiex|ath10k)' --quiet | tail -n 3"},
  Log{kCommand, "wifi_driver_errors_count",
    "croslog --show-cursor=false --identifier=kernel --priority=err"
    "  --grep='(iwlwifi|mwifiex|ath10k)' | wc -l"},
  Log{kCommand, "wifi_status",
      "/usr/bin/network_diag --wifi-internal --no-log --anonymize"},
};
// clang-format on

// Fills |dictionary| with the contents of the logs in |logs|.
template <std::size_t N>
void GetLogsInDictionary(const std::array<Log, N>& logs,
                         base::Value* dictionary) {
  for (const Log& log : logs) {
    dictionary->SetStringKey(log.GetName(), log.GetLogData());
  }
}

// Serializes the |dictionary| into the file with the given |fd| in a JSON
// format.
void SerializeLogsAsJSON(const base::Value& dictionary,
                         const base::ScopedFD& fd) {
  string logs_json;
  base::JSONWriter::WriteWithOptions(
      dictionary, base::JSONWriter::OPTIONS_PRETTY_PRINT, &logs_json);
  base::WriteFileDescriptor(fd.get(), logs_json);
}

template <std::size_t N>
bool GetNamedLogFrom(const string& name,
                     const std::array<Log, N>& logs,
                     string* result) {
  for (const Log& log : logs) {
    if (name == log.GetName()) {
      *result = log.GetLogData();
      return true;
    }
  }
  *result = "<invalid log name>";
  return false;
}

template <std::size_t N>
void GetLogsFrom(const std::array<Log, N>& logs, LogTool::LogMap* map) {
  for (const Log& log : logs)
    (*map)[log.GetName()] = log.GetLogData();
}

void GetLsbReleaseInfo(LogTool::LogMap* map) {
  const base::FilePath lsb_release(kLsbReleasePath);
  brillo::KeyValueStore store;
  if (!store.Load(lsb_release)) {
    // /etc/lsb-release might not be present (cros deploying a new
    // configuration or no fields set at all). Just print a debug
    // message and continue.
    DLOG(INFO) << "Could not load fields from " << lsb_release.value();
  } else {
    for (const auto& key : store.GetKeys()) {
      // The DEVICETYPE from /etc/lsb-release may not be correct on some
      // unibuild devices, so filter it out.
      if (key != "DEVICETYPE") {
        std::string value;
        store.GetString(key, &value);
        (*map)[key] = value;
      }
    }
  }
}

void GetOsReleaseInfo(LogTool::LogMap* map) {
  brillo::OsReleaseReader reader;
  reader.Load();
  for (const auto& key : reader.GetKeys()) {
    std::string value;
    reader.GetString(key, &value);
    (*map)["os-release " + key] = value;
  }
}

void PopulateDictionaryValue(const LogTool::LogMap& map,
                             base::Value* dictionary) {
  for (const auto& kv : map) {
    dictionary->SetStringKey(kv.first, kv.second);
  }
}

bool CompressXzBuffer(const std::vector<uint8_t>& in_buffer,
                      std::vector<uint8_t>* out_buffer) {
  size_t out_size = lzma_stream_buffer_bound(in_buffer.size());
  out_buffer->resize(out_size);
  size_t out_pos = 0;

  lzma_ret ret = lzma_easy_buffer_encode(
      LZMA_PRESET_DEFAULT, LZMA_CHECK_CRC64, nullptr, in_buffer.data(),
      in_buffer.size(), out_buffer->data(), &out_pos, out_size);

  if (ret != LZMA_OK) {
    out_buffer->clear();
    return false;
  }

  out_buffer->resize(out_pos);
  return true;
}

void GetPerfData(LogTool::LogMap* map) {
  // Run perf to collect system-wide performance profile when user triggers
  // feedback report. Perf runs at sampling frequency of ~500 hz (499 is used
  // to avoid sampling periodic system activities), with callstack in each
  // sample (-g).
  std::vector<std::string> perf_args = {
      "perf", "record", "-a", "-g", "-F", "499",
  };
  std::vector<uint8_t> perf_data;
  int32_t status;

  debugd::PerfTool perf_tool;
  if (!perf_tool.GetPerfOutput(kPerfDurationSecs, perf_args, &perf_data,
                               nullptr, &status, nullptr))
    return;

  // XZ compress the profile data.
  std::vector<uint8_t> perf_data_xz;
  if (!CompressXzBuffer(perf_data, &perf_data_xz))
    return;

  // Base64 encode the compressed data.
  std::string perf_data_str(reinterpret_cast<const char*>(perf_data_xz.data()),
                            perf_data_xz.size());
  (*map)["perf-data"] = std::string(kPerfDataDescription) +
                        LogTool::EncodeString(std::move(perf_data_str),
                                              LogTool::Encoding::kBase64);
}

}  // namespace

Log::Log(Log::LogType type,
         std::string name,
         std::string data,
         std::string user,
         std::string group,
         int64_t max_bytes,
         LogTool::Encoding encoding,
         bool access_root_mount_ns)
    : type_(type),
      name_(name),
      data_(data),
      user_(user),
      group_(group),
      max_bytes_(max_bytes),
      encoding_(encoding),
      access_root_mount_ns_(access_root_mount_ns) {}

std::string Log::GetName() const {
  return name_;
}

std::string Log::GetLogData() const {
  // The reason this code uses a switch statement on a type enum rather than
  // using inheritance/virtual dispatch is so that all of the Log objects can
  // be constructed statically. Switching to heap allocated subclasses of Log
  // makes the code that declares all of the log entries much more verbose
  // and harder to understand.
  std::string output;
  switch (type_) {
    case kCommand:
      output = GetCommandLogData();
      break;
    case kFile:
      output = GetFileLogData();
      break;
    case kGlob:
      output = GetGlobLogData();
      break;
    default:
      DCHECK(false) << "unknown log type";
      return "<unknown log type>";
  }

  if (output.empty())
    return "<empty>";

  return LogTool::EncodeString(std::move(output), encoding_);
}

// TODO(ellyjones): sandbox. crosbug.com/35122
std::string Log::GetCommandLogData() const {
  DCHECK_EQ(type_, kCommand);
  if (type_ != kCommand)
    return "<log type mismatch>";
  std::string tailed_cmdline =
      base::StringPrintf("%s | tail -c %" PRId64, data_.c_str(), max_bytes_);
  ProcessWithOutput p;
  if (minijail_disabled_for_test_)
    p.set_use_minijail(false);
  if (!user_.empty() && !group_.empty())
    p.SandboxAs(user_, group_);
  if (access_root_mount_ns_)
    p.AllowAccessRootMountNamespace();
  if (!p.Init())
    return "<not available>";
  p.AddArg(kShell);
  p.AddStringOption("-c", tailed_cmdline);
  if (p.Run())
    return "<not available>";
  std::string output;
  p.GetOutput(&output);
  return output;
}

// static
std::string Log::GetFileData(const base::FilePath& path,
                             int64_t max_bytes,
                             const std::string& user,
                             const std::string& group) {
  uid_t old_euid = geteuid();
  uid_t new_euid = UidForUser(user);
  gid_t old_egid = getegid();
  gid_t new_egid = GidForGroup(group);

  if (new_euid == -1 || new_egid == -1) {
    return "<not available>";
  }

  // Make sure to set group first, since if we set user first we lose root
  // and therefore the ability to set our effective gid to arbitrary gids.
  if (setegid(new_egid)) {
    PLOG(ERROR) << "Failed to set effective group id to " << new_egid;
    return "<not available>";
  }
  if (seteuid(new_euid)) {
    PLOG(ERROR) << "Failed to set effective user id to " << new_euid;
    if (setegid(old_egid))
      PLOG(ERROR) << "Failed to restore effective group id to " << old_egid;
    return "<not available>";
  }

  std::string contents;
  // Handle special files that don't properly report length/allow lseek.
  if (base::FilePath("/dev").IsParent(path) ||
      base::FilePath("/proc").IsParent(path) ||
      base::FilePath("/sys").IsParent(path)) {
    if (!base::ReadFileToString(path, &contents))
      contents = "<not available>";
    if (contents.size() > max_bytes)
      contents.erase(0, contents.size() - max_bytes);
  } else {
    base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
    if (!file.IsValid()) {
      contents = "<not available>";
    } else {
      int64_t length = file.GetLength();
      if (length > max_bytes) {
        file.Seek(base::File::FROM_END, -max_bytes);
        length = max_bytes;
      }
      std::vector<char> buf(length);
      int read = file.ReadAtCurrentPos(buf.data(), buf.size());
      if (read < 0) {
        PLOG(ERROR) << "Could not read from file " << path.value();
      } else {
        contents = std::string(buf.begin(), buf.begin() + read);
      }
    }
  }

  // Make sure we restore our old euid/egid before returning.
  if (seteuid(old_euid))
    PLOG(ERROR) << "Failed to restore effective user id to " << old_euid;

  if (setegid(old_egid))
    PLOG(ERROR) << "Failed to restore effective group id to " << old_egid;

  return contents;
}

std::string Log::GetFileLogData() const {
  DCHECK_EQ(type_, kFile);
  if (type_ != kFile)
    return "<log type mismatch>";

  return GetFileData(base::FilePath(data_), max_bytes_, user_, group_);
}

std::string Log::GetGlobLogData() const {
  DCHECK_EQ(type_, kGlob);
  if (type_ != kGlob)
    return "<log type mismatch>";

  // NB: base::FileEnumerator requires a directory to walk, and a pattern to
  // match against each result.  Here we accept full paths with globs in them.
  glob_t g;
  // NB: Feel free to add GLOB_BRACE if a user comes up.
  int gret = glob(data_.c_str(), 0, nullptr, &g);
  if (gret == GLOB_NOMATCH) {
    globfree(&g);
    return "<no matches>";
  } else if (gret) {
    globfree(&g);
    PLOG(ERROR) << "glob " << data_ << " failed";
    return "<not available>";
  }

  // The results array will hold 2 entries per file: the filename, and the
  // results of reading that file.
  size_t output_size = 0;
  std::vector<std::string> results;
  results.reserve(g.gl_pathc * 2);

  for (size_t pathc = 0; pathc < g.gl_pathc; ++pathc) {
    const base::FilePath path(g.gl_pathv[pathc]);
    std::string contents = GetFileData(path, max_bytes_, user_, group_);
    // NB: The 3 represents the bytes we add in the output string below.
    output_size += path.value().size() + contents.size() + 3;
    results.push_back(path.value());
    results.push_back(contents);
  }
  globfree(&g);

  // Combine the results into a single string.  We have a header with the
  // filename followed by that file's contents.  Very basic format.
  std::string output;
  output.reserve(output_size);
  for (auto iter = results.begin(); iter != results.end(); ++iter) {
    output += *iter + ":\n";
    ++iter;
    output += *iter + "\n";
  }

  return output;
}

void Log::DisableMinijailForTest() {
  minijail_disabled_for_test_ = true;
}

// static
uid_t Log::UidForUser(const std::string& user) {
  struct passwd entry;
  struct passwd* result;
  std::vector<char> buf(1024);
  getpwnam_r(user.c_str(), &entry, &buf[0], buf.size(), &result);
  if (!result) {
    LOG(ERROR) << "User not found: " << user;
    return -1;
  }
  return entry.pw_uid;
}

// static
gid_t Log::GidForGroup(const std::string& group) {
  struct group entry;
  struct group* result;
  std::vector<char> buf(1024);
  getgrnam_r(group.c_str(), &entry, &buf[0], buf.size(), &result);
  if (!result) {
    LOG(ERROR) << "Group not found: " << group;
    return -1;
  }
  return entry.gr_gid;
}

LogTool::LogTool(
    scoped_refptr<dbus::Bus> bus,
    std::unique_ptr<org::chromium::CryptohomeMiscInterfaceProxyInterface>
        cryptohome_proxy,
    std::unique_ptr<LogTool::Log> arc_bug_report_log,
    const base::FilePath& daemon_store_base_dir)
    : bus_(bus),
      cryptohome_proxy_(std::move(cryptohome_proxy)),
      arc_bug_report_log_(std::move(arc_bug_report_log)),
      daemon_store_base_dir_(daemon_store_base_dir) {}

LogTool::LogTool(scoped_refptr<dbus::Bus> bus)
    : LogTool(
          bus,
          std::make_unique<org::chromium::CryptohomeMiscInterfaceProxy>(bus),
          std::make_unique<ArcBugReportLog>(),
          base::FilePath(kDaemonStoreBaseDir)) {}

bool LogTool::IsUserHashValid(const std::string& userhash) {
  return brillo::cryptohome::home::IsSanitizedUserName(userhash) &&
         base::PathExists(daemon_store_base_dir_.Append(userhash));
}

void LogTool::CreateConnectivityReport(bool wait_for_results) {
  // Perform ConnectivityTrial to report connection state in feedback log.
  auto shill = std::make_unique<org::chromium::flimflam::ManagerProxy>(bus_);
  // Give the connection trial time to test the connection and log the results
  // before collecting the logs for feedback.
  // TODO(silberst): Replace the simple approach of a single timeout with a more
  // coordinated effort.
  if (shill && shill->CreateConnectivityReport(nullptr) && wait_for_results)
    sleep(kConnectionTesterTimeoutSeconds);
}

std::optional<string> LogTool::GetLog(const string& name) {
  string result;
  if (GetNamedLogFrom(name, kCommandLogs, &result) ||
      GetNamedLogFrom(name, kCommandLogsShort, &result) ||
      GetNamedLogFrom(name, kExtraLogs, &result) ||
      GetNamedLogFrom(name, kFeedbackLogs, &result)) {
    return std::make_optional(result);
  }
  return std::nullopt;
}

LogTool::LogMap LogTool::GetAllLogs() {
  Stopwatch sw("Perf.GetAllLogs");
  CreateConnectivityReport(false);
  LogMap result;
  GetLogsFrom(kCommandLogsShort, &result);
  GetLogsFrom(kCommandLogs, &result);
  GetLogsFrom(kExtraLogs, &result);
  GetLsbReleaseInfo(&result);
  GetOsReleaseInfo(&result);
  return result;
}

LogTool::LogMap LogTool::GetAllDebugLogs() {
  Stopwatch sw("Perf.GetAllDebugLogs");
  CreateConnectivityReport(true);
  LogMap result;
  GetLogsFrom(kCommandLogsShort, &result);
  GetLogsFrom(kCommandLogs, &result);
  GetLogsFrom(kExtraLogs, &result);
  result[arc_bug_report_log_->GetName()] = GetArcBugReport("", nullptr);
  GetLsbReleaseInfo(&result);
  GetOsReleaseInfo(&result);
  return result;
}

template <std::size_t N>
std::vector<std::string> GetTitlesFrom(const std::array<Log, N>& logs) {
  std::vector<std::string> result;
  for (const Log& log : logs) {
    result.push_back(log.GetName());
  }
  return result;
}

std::vector<std::vector<std::string>> GetAllDebugTitlesForTest() {
  std::vector<std::vector<std::string>> result;
  result.push_back(GetTitlesFrom(kCommandLogsShort));
  result.push_back(GetTitlesFrom(kCommandLogs));
  result.push_back(GetTitlesFrom(kExtraLogs));
  return result;
}

void LogTool::GetBigFeedbackLogs(const base::ScopedFD& fd,
                                 const std::string& username) {
  Stopwatch sw("Perf.GetBigFeedbackLogs");
  GetBluetoothBqr();
  CreateConnectivityReport(true);
  LogMap map;
  GetPerfData(&map);
  base::Value dictionary(base::Value::Type::DICTIONARY);
  GetLogsInDictionary(kCommandLogsVerbose, &dictionary);
  GetLogsInDictionary(kCommandLogs, &dictionary);
  GetLogsInDictionary(kFeedbackLogs, &dictionary);
  bool is_backup;
  std::string arc_bug_report = GetArcBugReport(username, &is_backup);
  dictionary.SetStringKey(kArcBugReportBackupKey,
                          (is_backup ? "true" : "false"));
  dictionary.SetStringKey(arc_bug_report_log_->GetName(), arc_bug_report);
  GetLsbReleaseInfo(&map);
  GetOsReleaseInfo(&map);
  PopulateDictionaryValue(map, &dictionary);
  SerializeLogsAsJSON(dictionary, fd);
}

std::string GetSanitizedUsername(
    org::chromium::CryptohomeMiscInterfaceProxyInterface* cryptohome_proxy,
    const std::string& username) {
  if (username.empty()) {
    return std::string();
  }

  user_data_auth::GetSanitizedUsernameRequest request;
  user_data_auth::GetSanitizedUsernameReply reply;
  request.set_username(username);

  brillo::ErrorPtr error;
  if (!cryptohome_proxy->GetSanitizedUsername(request, &reply, &error)) {
    LOG(ERROR) << "Failed to call GetSanitizedUsername, error: "
               << error->GetMessage();
    return std::string();
  }

  return reply.sanitized_username();
}

std::string LogTool::GetArcBugReport(const std::string& username,
                                     bool* is_backup) {
  if (is_backup) {
    *is_backup = true;
  }
  std::string userhash =
      GetSanitizedUsername(cryptohome_proxy_.get(), username);

  std::string contents;
  if (userhash.empty() ||
      arc_bug_report_backups_.find(userhash) == arc_bug_report_backups_.end() ||
      !base::ReadFileToString(daemon_store_base_dir_.Append(userhash).Append(
                                  kArcBugReportBackupFileName),
                              &contents)) {
    // If |userhash| was not empty, but was not found in the backup set
    // or the file did not exist, attempt to delete the file.
    if (!userhash.empty()) {
      DeleteArcBugReportBackup(username);
    }
    if (is_backup) {
      *is_backup = false;
    }
    contents = arc_bug_report_log_->GetLogData();
  }

  return contents;
}

void LogTool::BackupArcBugReport(const std::string& username) {
  DLOG(INFO) << "Backing up ARC bug report";

  const std::string userhash =
      GetSanitizedUsername(cryptohome_proxy_.get(), username);
  if (!IsUserHashValid(userhash)) {
    LOG(ERROR) << "Invalid userhash '" << userhash << "'";
    return;
  }

  brillo::SafeFD backupDir(
      brillo::SafeFD::Root()
          .first.OpenExistingDir(daemon_store_base_dir_.Append(userhash))
          .first);
  if (!backupDir.is_valid()) {
    LOG(ERROR) << "Failed to open ARC bug report backup dir at "
               << daemon_store_base_dir_.Append(userhash).value();
    return;
  }

  brillo::SafeFD backupFile(
      brillo::OpenOrRemakeFile(&backupDir, kArcBugReportBackupFileName).first);
  if (!backupFile.is_valid()) {
    LOG(ERROR) << "Failed to open ARC bug report file at "
               << daemon_store_base_dir_.Append(userhash)
                      .Append(kArcBugReportBackupFileName)
                      .value();
    return;
  }

  const std::string logData = arc_bug_report_log_->GetLogData();

  if (backupFile.Write(logData.c_str(), logData.length()) ==
      brillo::SafeFD::Error::kNoError) {
    arc_bug_report_backups_.insert(userhash);
  } else {
    PLOG(ERROR) << "Failed to back up ARC bug report";
  }
}

void LogTool::DeleteArcBugReportBackup(const std::string& username) {
  DLOG(INFO) << "Deleting the ARC bug report backup";

  const std::string userhash =
      GetSanitizedUsername(cryptohome_proxy_.get(), username);
  if (!IsUserHashValid(userhash)) {
    LOG(ERROR) << "Invalid userhash '" << userhash << "'";
    return;
  }

  brillo::SafeFD backupDir(
      brillo::SafeFD::Root()
          .first.OpenExistingDir(daemon_store_base_dir_.Append(userhash))
          .first);
  if (!backupDir.is_valid()) {
    LOG(ERROR) << "Failed to open ARC bug report backup dir at "
               << daemon_store_base_dir_.Append(userhash).value();
    return;
  }

  arc_bug_report_backups_.erase(userhash);

  if (base::PathExists(daemon_store_base_dir_.Append(userhash).Append(
          kArcBugReportBackupFileName)) &&
      backupDir.Unlink(kArcBugReportBackupFileName) !=
          brillo::SafeFD::Error::kNoError) {
    PLOG(ERROR) << "Failed to delete ARC bug report backup at "
                << daemon_store_base_dir_.Append(userhash)
                       .Append(kArcBugReportBackupFileName)
                       .value();
  }
}

void LogTool::GetJournalLog(const base::ScopedFD& fd) {
  Log journal(kCommand, "journal.export", "journalctl -n 10000 -o export",
              "syslog", "syslog", 10 * 1024 * 1024, LogTool::Encoding::kBinary);
  std::string output = journal.GetLogData();
  base::WriteFileDescriptor(fd.get(), output);
}

// static
string LogTool::EncodeString(string value, LogTool::Encoding source_encoding) {
  if (source_encoding == LogTool::Encoding::kBinary)
    return value;

  if (source_encoding == LogTool::Encoding::kAutodetect) {
    if (base::IsStringUTF8(value))
      return value;
    source_encoding = LogTool::Encoding::kBase64;
  }

  if (source_encoding == LogTool::Encoding::kUtf8) {
    string output;
    const char* src = value.data();
    int32_t src_len = static_cast<int32_t>(value.length());

    output.reserve(value.size());
    for (int32_t char_index = 0; char_index < src_len; char_index++) {
      uint32_t code_point;
      if (!base::ReadUnicodeCharacter(src, src_len, &char_index, &code_point) ||
          !base::IsValidCharacter(code_point)) {
        // Replace invalid characters with U+FFFD REPLACEMENT CHARACTER.
        code_point = 0xFFFD;
      }
      base::WriteUnicodeCharacter(code_point, &output);
    }
    return output;
  }

  base::Base64Encode(value, &value);
  return "<base64>: " + value;
}

}  // namespace debugd
