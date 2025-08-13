// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLEX_HWIS_FLEX_DEVICE_METRICS_FLEX_DEVICE_METRICS_H_
#define FLEX_HWIS_FLEX_DEVICE_METRICS_FLEX_DEVICE_METRICS_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <base/files/file_path.h>
#include <base/json/json_value_converter.h>
#include <base/time/time.h>
#include <metrics/metrics_library.h>

// Convert from 512-byte disk blocks to MiB. Round down if the size is
// not an even MiB value.
int ConvertBlocksToMiB(int num_blocks);

// Get a partition's label from the `uevent` file in the partition's
// directory under `/sys`.
//
// Arguments:
//   sys_partition_path: Path of a partition directory under /sys.
//                       For example: /sys/class/block/sda/sda2.
//
// Returns the partition's label on success, for example
// "KERN-A". Returns `nullopt` if any error occurs.
std::optional<std::string> GetPartitionLabelFromUevent(
    const base::FilePath& sys_partition_path);

// Get a partition's size in MiB from the `size` file in the partition's
// directory under `/sys`.
//
// Arguments:
//   sys_partition_path: Path of a partition directory under /sys.
//                       For example: /sys/class/block/sda/sda2.
//
// Returns the partition's size in MiB on success, rounded down if
// necessary. Returns `nullopt` if any error occurs.
std::optional<int> GetPartitionSizeInMiB(
    const base::FilePath& sys_partition_path);

// Map from partition label to partition size in MiB. A label may have
// more than one entry since partition labels are not guaranteed to be
// unique.
using MapPartitionLabelToMiBSize = std::multimap<std::string, int>;

// Create a map from partition label to partition size in MiB.
//
// This looks at files in `sys` to get partition info. For example:
// /sys/class/block/sda/
//   -> sda2/
//     -> File `uevent` contains the line "PARTNAME=KERN-A"
//     -> File `size` contains "131072"
//
// Why not use /dev/disk/by-partlabel? There's no defined handling for
// duplicate partition names. An example problem this could cause: a
// user could run Flex from a hard drive, but also have a Flex USB
// installer attached. Both disks would have the same partition names,
// but with different sizes. The by-partlabel directory could contain
// links to either one.
//
// Why not use cgpt? That requires read access to block files under
// /dev. That could be done by running under a user in the "disk" group,
// but doing it without cgpt allows the program to run under a more
// restricted user.
//
// Arguments:
//   root: Path of the filesystem root where `sys` is mounted.
//         Normally this is just `/`, but can be changed for testing.
//   root_disk_device_name: Name of the root disk device. Example: "sda".
//
// Returns a multimap with all partitions for which the size was
// successfully retrieved. A multimap is used because some partitions
// may have the same label, e.g. "reserved".
MapPartitionLabelToMiBSize GetPartitionSizeMap(
    const base::FilePath& root, std::string_view root_disk_device_name);

// Send a sparse metric for the size of each partition in the
// `partition_label` vector.
//
// A sparse metric is used because we want to know exact values. Only a
// few values are actually expected (e.g. the kernel partition should
// always be either 16MiB or 64MiB), but any value is possible.
//
// Partition sizes are read from the `label_to_size_map` multimap. If a
// partition is missing from that map, or if it has multiple entries,
// it's treated as an error.
//
// An error in sending one metric does not prevent other metrics from
// being sent.
//
// Arguments:
//   metrics: Interface to the metrics library.
//   label_to_size_map: Multimap created by `GetPartitionSizeMap`.
//   partition_labels: Vector of partition names to send metrics for.
//
// Returns true on success, false if any error occurs.
bool SendDiskMetrics(MetricsLibraryInterface& metrics,
                     const MapPartitionLabelToMiBSize& label_to_size_map,
                     const std::vector<std::string>& partition_labels);

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class CpuIsaLevel {
  // Unknown ISA level (CPU is probably not x86-64).
  kUnknown = 0,

  // See https://en.wikipedia.org/wiki/X86-64#Microarchitecture_levels
  // for details of the levels.
  kX86_64_V1 = 1,
  kX86_64_V2 = 2,
  kX86_64_V3 = 3,
  kX86_64_V4 = 4,

  kMaxValue = kX86_64_V4,
};

// Get the x86-64 ISA level of the CPU.
CpuIsaLevel GetCpuIsaLevel();

// Send the CPU ISA level metric.
//
// This is an enum metric, see `GetCpuIsaLevel` for details of `isa_level`.
//
// Returns true on success, false if any error occurs.
bool SendCpuIsaLevelMetric(MetricsLibraryInterface& metrics,
                           CpuIsaLevel isa_level);

// Enum representing the method used to boot the device.
//
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class BootMethod {
  // Unknown boot mode (likely an error has occurred).
  kUnknown = 0,

  // Coreboot (ie Chromebook) firmware.
  kCoreboot = 1,
  // 32-bit UEFI environment.
  kUefi32 = 2,
  // 64-bit UEFI environment.
  kUefi64 = 3,
  // BIOS/Legacy boot.
  kBios = 4,

  kMaxValue = kBios,
};

// Get the method used to boot the device.
BootMethod GetBootMethod(const base::FilePath& root);

// Send the Boot Method metric.
//
// This is an enum metric, see `GetBootMethod` for details of `boot_method`.
//
// Returns true on success, false if any error occurs.
bool SendBootMethodMetric(MetricsLibraryInterface& metrics,
                          BootMethod boot_method);

// Enum representing the method used to install Flex.
//
// Not all installation methods are tracked.
//
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class InstallMethod { kUnknown = 0, kFlexor = 1, kMaxValue = kFlexor };

InstallMethod InstallMethodFromString(std::string_view method);

// Describes state we care about when deciding whether to send an install metric
//
// We should only send a metric when we've just installed, and have a known
// method of installation.
struct InstallState {
  bool just_installed = false;
  InstallMethod method = InstallMethod::kUnknown;
};

// Get the state needed for the install metric.
InstallState GetInstallState(const base::FilePath& root);

// Send the install type metric.
//
// This won't send the metric if `just_installed` is `false` or `InstallMethod`
// is `kUnknown`.
//
// This attempts to delete the `install_type` file before sending, which should
// mean this only sends once per install. If deletion fails the metric won't be
// sent, to avoid issues with deletion causing significant over-reporting.
//
// Returns false if there's a noteworthy failure, something to exit non-0 over.
// Returns true otherwise.
bool MaybeSendInstallMethodMetric(MetricsLibraryInterface& metrics,
                                  const base::FilePath& root,
                                  InstallState install_state);

// Query the fwupdmgr for the update history as a raw json string.
//
// Returns a string on success, std::nullopt if any error occurs.
std::optional<std::string> GetHistoryFromFwupdmgr();

// Enum representing the fwupd update state as defined in
// https://github.com/fwupd/fwupd/blob/240e65e92e53ead489a3ecdff668d6b4eea340fc/libfwupd/fwupd-enums.h#L1185
enum class FwupdUpdateState {
  // Unknown.
  kUnknown = 0,
  // Update is pending.
  kPending = 1,
  // Update was successful.
  kSuccess = 2,
  // Update failed.
  kFailed = 3,
  // Waiting for a reboot to apply.
  kNeedsReboot = 4,
  // Update failed due to transient issue, e.g. AC power required.
  kTransient = 5,

  kMaxValue = kTransient,
};

// The capsule device status [1] resulting from the last update attempt.
// This can provide a more specific failure reason in the case of update
// failure.
//
// [1]:
// https://uefi.org/specs/UEFI/2.11/23_Firmware_Update_and_Reporting.html#id30
enum class FwupdLastAttemptStatus {
  // Update was successful.
  kSuccess = 0,
  // Update was unsuccessful.
  kErrorUnsuccessful = 1,
  // There were insufficient resources to process the capsule.
  kErrorInsufficientResources = 2,
  // Version mismatch.
  kErrorIncorrectVersion = 3,
  // Firmware had invalid format.
  kErrorInvalidFormat = 4,
  // Authentication signing error.
  kErrorAuthError = 5,
  // AC power was not connected during update.
  kErrorPwrEvtAc = 6,
  // Battery level is too low.
  kErrorPwrEvtBatt = 7,
  // Unsatisfied Dependencies.
  kErrorUnsatisfiedDependencies = 8,

  kMinValue = kSuccess,
  kMaxValue = kErrorUnsatisfiedDependencies,
};

// Helpers to ease conversion of json values to their real types.

// Internally, the fwupdmgr stores the timestamp as an int64, however
// the JSON converter only accepts ints. This should work OK up until 2038:
// https://en.wikipedia.org/wiki/Year_2038_problem
bool ValToTime(const base::Value* val, base::Time* result);
bool ValToUpdateState(const base::Value* val, FwupdUpdateState* result);
bool StringToAttemptStatus(std::string_view s, FwupdLastAttemptStatus* result);

// Struct containing the only field we are interested in from
// the `Release` json object contained in the fwupd history response:
// the last attempt status.
struct FwupdRelease {
  FwupdLastAttemptStatus last_attempt_status;

  FwupdRelease() = default;

  static void RegisterJSONConverter(
      base::JSONValueConverter<FwupdRelease>* converter);
};

// The `Device` struct within fwupd's json response
// contains many more fields than those listed below,
// however we only convert the fields we need.
struct FwupdDeviceHistory {
  // Device name.
  std::string name;
  // The fwupd plugin, used to check whether the update was installed
  // with a UEFI plugin.
  std::string plugin;
  // The time when the history entry for the device was created.
  base::Time created;
  // Update state, a per device value.
  FwupdUpdateState update_state;
  // The list of `Release` struct, each containing a `FwupdLastAttemptStatus`
  // which can narrow down failure reasons.
  std::vector<std::unique_ptr<FwupdRelease>> releases;

  FwupdDeviceHistory() = default;

  static void RegisterJSONConverter(
      base::JSONValueConverter<FwupdDeviceHistory>* converter);
};

// Find all update histories in a json string and collect them in `histories`.
//
// Returns true on success, false if the json is not formatted correctly, e.g.
// it is not a json dict or an `update_state` is missing.
bool ParseFwupHistoriesFromJson(std::string_view history_json,
                                std::vector<FwupdDeviceHistory>& histories);

// Filepath to record the last time fwup history metrics were sent.
inline constexpr std::string_view kFwupTimestampFile =
    "/run/flex_device_metrics/last_fwup_report";

// Records the current time to file.
//
// Returns true on success, false if any error occurs.
bool RecordFwupMetricTimestamp(
    const base::FilePath& last_fwup_report = base::FilePath(kFwupTimestampFile),
    base::Time time = base::Time::UnixEpoch());

// Gets the timestamp stored in file.
std::optional<base::Time> GetFwupMetricTimestamp(
    const base::FilePath& last_fwup_report =
        base::FilePath(kFwupTimestampFile));

#endif  // FLEX_HWIS_FLEX_DEVICE_METRICS_FLEX_DEVICE_METRICS_H_
