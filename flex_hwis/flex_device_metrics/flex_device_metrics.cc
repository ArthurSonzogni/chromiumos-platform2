// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_device_metrics/flex_device_metrics.h"

#include <unistd.h>

#include <memory>
#include <optional>
#include <utility>

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <brillo/files/file_util.h>
#include <brillo/process/process.h>

constexpr char kInstallTypeFile[] =
    "mnt/stateful_partition/unencrypted/install_metrics/install_type";

int ConvertBlocksToMiB(int num_blocks) {
  const int bytes_per_block = 512;
  const int bytes_per_mib = 1024 * 1024;
  return (num_blocks * bytes_per_block) / bytes_per_mib;
}

std::optional<std::string> GetPartitionLabelFromUevent(
    const base::FilePath& sys_partition_path) {
  const auto uevent_path = sys_partition_path.Append("uevent");
  std::string uevent;
  if (!ReadFileToStringWithMaxSize(uevent_path, &uevent,
                                   /*max_size=*/4096)) {
    PLOG(ERROR) << "Failed to read " << uevent_path;
    return std::nullopt;
  }

  for (const auto& line : base::SplitStringPiece(
           uevent, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    const auto index = line.find_first_of('=');
    if (index == std::string::npos) {
      continue;
    }

    const auto& key = line.substr(0, index);
    const auto& val = line.substr(index + 1);

    if (key == "PARTNAME") {
      return std::string(val);
    }
  }

  return std::nullopt;
}

std::optional<int> GetPartitionSizeInMiB(
    const base::FilePath& sys_partition_path) {
  const auto size_path = sys_partition_path.Append("size");
  std::string size_str;
  if (!ReadFileToStringWithMaxSize(size_path, &size_str,
                                   /*max_size=*/32)) {
    PLOG(ERROR) << "Failed to read " << size_path;
    return std::nullopt;
  }

  const auto size_str_trimmed =
      TrimWhitespaceASCII(size_str, base::TRIM_TRAILING);

  int partition_size_in_blocks = 0;
  if (!base::StringToInt(size_str_trimmed, &partition_size_in_blocks)) {
    LOG(ERROR) << "TODO: conversion error";
    return std::nullopt;
  }

  return ConvertBlocksToMiB(partition_size_in_blocks);
}

MapPartitionLabelToMiBSize GetPartitionSizeMap(
    const base::FilePath& root, std::string_view root_disk_device_name) {
  MapPartitionLabelToMiBSize label_to_size_map;

  const auto sys_block_root_path =
      root.Append("sys/block").Append(root_disk_device_name);
  base::FileEnumerator enumerator(sys_block_root_path, /*recursive=*/false,
                                  base::FileEnumerator::DIRECTORIES);

  for (base::FilePath subdir = enumerator.Next(); !subdir.empty();
       subdir = enumerator.Next()) {
    // Ignore directories that don't look like partitions.
    if (!subdir.BaseName().value().starts_with(root_disk_device_name)) {
      continue;
    }

    // Get the partition label, e.g. "EFI-SYSTEM".
    const auto partition_label = GetPartitionLabelFromUevent(subdir);
    if (!partition_label.has_value()) {
      continue;
    }

    // Get the partition's size in MiB.
    const auto partition_size_in_mib = GetPartitionSizeInMiB(subdir);
    if (!partition_size_in_mib.has_value()) {
      continue;
    }

    label_to_size_map.insert(
        std::make_pair(partition_label.value(), partition_size_in_mib.value()));
  }

  return label_to_size_map;
}

bool SendDiskMetrics(MetricsLibraryInterface& metrics,
                     const MapPartitionLabelToMiBSize& label_to_size_map,
                     const std::vector<std::string>& partition_labels) {
  bool success = true;
  for (const auto& partition_label : partition_labels) {
    const auto count = label_to_size_map.count(partition_label);
    if (count != 1) {
      LOG(ERROR) << "Unexpected number of \"" << partition_label
                 << "\" partitions: " << count;
      success = false;
      continue;
    }

    const auto iter = label_to_size_map.find(partition_label);
    const int partition_size_in_mib = iter->second;

    // Send the metric.
    const std::string metric_name =
        std::string("Platform.FlexPartitionSize.") + partition_label;
    if (!metrics.SendSparseToUMA(metric_name, partition_size_in_mib)) {
      LOG(ERROR) << "Failed to send metric " << metric_name;
      success = false;
      continue;
    }
  }

  return success;
}

CpuIsaLevel GetCpuIsaLevel() {
  if (__builtin_cpu_supports("x86-64-v4")) {
    return CpuIsaLevel::kX86_64_V4;
  } else if (__builtin_cpu_supports("x86-64-v3")) {
    return CpuIsaLevel::kX86_64_V3;
  } else if (__builtin_cpu_supports("x86-64-v2")) {
    return CpuIsaLevel::kX86_64_V2;
  } else if (__builtin_cpu_supports("x86-64")) {
    return CpuIsaLevel::kX86_64_V1;
  } else {
    LOG(ERROR) << "CPU does not support any expected x86-64 ISA level";
    return CpuIsaLevel::kUnknown;
  }
}

bool SendCpuIsaLevelMetric(MetricsLibraryInterface& metrics,
                           CpuIsaLevel isa_level) {
  return metrics.SendEnumToUMA("Platform.FlexCpuIsaLevel", isa_level);
}

BootMethod GetBootMethod(const base::FilePath& root) {
  const auto vpd_sysfs_path = root.Append("sys/firmware/vpd/");
  if (base::PathExists(vpd_sysfs_path)) {
    return BootMethod::kCoreboot;
  }

  const auto efi_sysfs_path = root.Append("sys/firmware/efi/");
  if (!base::PathExists(efi_sysfs_path)) {
    return BootMethod::kBios;
  }

  const auto uefi_bitness_path = efi_sysfs_path.Append("fw_platform_size");
  std::string uefi_bitness_str;
  if (!ReadFileToStringWithMaxSize(uefi_bitness_path, &uefi_bitness_str,
                                   /*max_size=*/3)) {
    PLOG(ERROR) << "Failed to read " << uefi_bitness_path;
    return BootMethod::kUnknown;
  }

  const auto uefi_bitness_str_trimmed =
      TrimWhitespaceASCII(uefi_bitness_str, base::TRIM_TRAILING);

  if (uefi_bitness_str_trimmed == "64") {
    return BootMethod::kUefi64;
  } else if (uefi_bitness_str_trimmed == "32") {
    return BootMethod::kUefi32;
  }

  LOG(ERROR) << "Device boot method could not be determined.";
  return BootMethod::kUnknown;
}

bool SendBootMethodMetric(MetricsLibraryInterface& metrics,
                          BootMethod boot_method) {
  return metrics.SendEnumToUMA("Platform.FlexBootMethod", boot_method);
}

InstallMethod InstallMethodFromString(std::string_view method) {
  if (method == "flexor") {
    return InstallMethod::kFlexor;
  }

  return InstallMethod::kUnknown;
}

InstallState GetInstallState(const base::FilePath& root) {
  InstallState state;

  const auto install_type_path = root.Append(kInstallTypeFile);
  if (!base::PathExists(install_type_path)) {
    return state;
  } else {
    // We still have our install_type file,
    // so we're still in the "just installed" phase.
    state.just_installed = true;
  }

  // Try to read the file, but if we can't that's fine: we'll try again later.
  std::string content;
  size_t max_len = 32;
  if (!base::ReadFileToStringWithMaxSize(install_type_path, &content,
                                         max_len)) {
    LOG(WARNING) << "Install type file is present but could not be read.";
    return state;
  }

  std::string_view method =
      base::TrimWhitespaceASCII(content, base::TRIM_TRAILING);
  LOG(INFO) << "Successfully read install method: " << method;
  state.method = InstallMethodFromString(method);

  return state;
}

bool MaybeSendInstallMethodMetric(MetricsLibraryInterface& metrics,
                                  const base::FilePath& root,
                                  InstallState state) {
  if (!state.just_installed || state.method == InstallMethod::kUnknown) {
    return true;
  }

  const auto install_type_path = root.Append(kInstallTypeFile);
  // Only return true if we manage to delete, to avoid double-sends.
  // If it stays we'll send next time.
  if (brillo::DeleteFile(install_type_path)) {
    return metrics.SendEnumToUMA("Platform.FlexInstallMethod", state.method);
  }

  return false;
}

std::optional<std::string> GetHistoryFromFwupdmgr() {
  brillo::ProcessImpl fwupdmgr_process;
  fwupdmgr_process.RedirectOutputToMemory(true);
  fwupdmgr_process.AddArg("/usr/bin/fwupdmgr");
  fwupdmgr_process.AddArg("get-history");
  fwupdmgr_process.AddArg("--json");

  int fwupdmgr_rc = fwupdmgr_process.Run();
  if (fwupdmgr_rc == 0) {
    return fwupdmgr_process.GetOutputString(STDOUT_FILENO);
  } else {
    LOG(ERROR) << "Failed to run `fwupdmgr get-history`.";
    return std::nullopt;
  }
}

bool ValToTime(const base::Value* val, base::Time* result) {
  std::optional<int> time_as_int = val->GetIfInt();
  if (time_as_int.has_value()) {
    *result = base::Time::FromSecondsSinceUnixEpoch((time_as_int.value()));
    return true;
  }
  return false;
}

bool ValToUpdateState(const base::Value* val, FwupdUpdateState* result) {
  std::optional<int> state_as_int = val->GetIfInt();
  if (state_as_int.has_value() && state_as_int.value() >= 0 &&
      state_as_int.value() <= static_cast<int>(FwupdUpdateState::kMaxValue)) {
    *result = static_cast<FwupdUpdateState>(state_as_int.value());
    return true;
  }
  return false;
}

bool StringToAttemptStatus(std::string_view s, FwupdLastAttemptStatus* result) {
  int status = 0;
  if (!base::HexStringToInt(s, &status)) {
    return false;
  }

  if (status >= static_cast<int>(FwupdLastAttemptStatus::kMinValue) &&
      status <= static_cast<int>(FwupdLastAttemptStatus::kMaxValue)) {
    *result = static_cast<FwupdLastAttemptStatus>(status);
    return true;
  }
  return false;
}

void FwupdRelease::RegisterJSONConverter(
    base::JSONValueConverter<FwupdRelease>* converter) {
  converter->RegisterCustomField<FwupdLastAttemptStatus>(
      "FwupdLastAttemptStatus", &FwupdRelease::last_attempt_status,
      &StringToAttemptStatus);
}

void FwupdDeviceHistory::RegisterJSONConverter(
    base::JSONValueConverter<FwupdDeviceHistory>* converter) {
  converter->RegisterStringField("Name", &FwupdDeviceHistory::name);
  converter->RegisterStringField("Plugin", &FwupdDeviceHistory::plugin);
  converter->RegisterCustomValueField<base::Time>(
      "Created", &FwupdDeviceHistory::created, &ValToTime);
  converter->RegisterCustomValueField<FwupdUpdateState>(
      "UpdateState", &FwupdDeviceHistory::update_state, &ValToUpdateState);
  converter->RegisterRepeatedMessage("Releases", &FwupdDeviceHistory::releases);
}

bool ParseFwupHistoriesFromJson(std::string_view history_json,
                                std::vector<FwupdDeviceHistory>& histories) {
  std::optional<base::Value::Dict> response_dict =
      base::JSONReader::ReadDict(history_json);
  if (!response_dict.has_value()) {
    LOG(ERROR) << "fwupdmgr response not formatted as json dictionary.";
    return false;
  }

  base::Value::List* devices = response_dict.value().FindList("Devices");
  if (!devices) {
    LOG(ERROR) << "List of devices not found in fwupdmgr response.";
    return false;
  }

  base::JSONValueConverter<FwupdDeviceHistory> converter;
  for (const base::Value& device : *devices) {
    FwupdDeviceHistory history;
    if (!converter.Convert(device, &history)) {
      LOG(ERROR)
          << "Failed to convert value into device update history struct.";
      return false;
    }
    histories.push_back(std::move(history));
  }
  return true;
}

bool RecordFwupMetricTimestamp(base::Time time,
                               const base::FilePath& last_fwup_report) {
  if (!base::WriteFile(base::FilePath(last_fwup_report),
                       base::ToString(time).append("\n"))) {
    LOG(ERROR) << "Failed to write timestamp of last fwup history metric "
                  "submission to "
               << last_fwup_report;
    return false;
  }
  return true;
}

std::optional<base::Time> GetFwupMetricTimestamp(
    const base::FilePath& last_fwup_report) {
  std::string time_str;
  if (!base::ReadFileToString(base::FilePath(last_fwup_report), &time_str)) {
    LOG(ERROR) << "Failed to read fwup history metric timestamp from "
               << last_fwup_report;
    return std::nullopt;
  }
  base::Time time;
  base::TrimWhitespaceASCII(time_str, base::TRIM_TRAILING, &time_str);
  if (!base::Time::FromString(time_str.c_str(), &time)) {
    LOG(ERROR) << "Failed to convert string to fwup history metric timestamp.";
    return std::nullopt;
  }
  return time;
}

std::optional<UpdateResult> AttemptStatusToUpdateResult(
    FwupdLastAttemptStatus status) {
  switch (status) {
    case FwupdLastAttemptStatus::kSuccess:
      return UpdateResult::kGenericFailure;
    case FwupdLastAttemptStatus::kErrorUnsuccessful:
      return UpdateResult::kErrorUnsuccessful;
    case FwupdLastAttemptStatus::kErrorInsufficientResources:
      return UpdateResult::kErrorInsufficientResources;
    case FwupdLastAttemptStatus::kErrorIncorrectVersion:
      return UpdateResult::kErrorIncorrectVersion;
    case FwupdLastAttemptStatus::kErrorInvalidFormat:
      return UpdateResult::kErrorInvalidFormat;
    case FwupdLastAttemptStatus::kErrorAuthError:
      return UpdateResult::kErrorAuthError;
    case FwupdLastAttemptStatus::kErrorPwrEvtAc:
      return UpdateResult::kErrorPwrEvtAc;
    case FwupdLastAttemptStatus::kErrorPwrEvtBatt:
      return UpdateResult::kErrorPwrEvtBatt;
    case FwupdLastAttemptStatus::kErrorUnsatisfiedDependencies:
      return UpdateResult::kErrorUnsatisfiedDependencies;
  }
  LOG(ERROR) << "Unexpected value for FwupdLastAttemptStatus: "
             << static_cast<int>(status);
  return std::nullopt;
}

std::optional<UpdateResult> UpdateStateToUpdateResult(FwupdUpdateState state) {
  switch (state) {
    case FwupdUpdateState::kUnknown:
      return UpdateResult::kUnknown;
    case FwupdUpdateState::kPending:
      return UpdateResult::kPending;
    case FwupdUpdateState::kSuccess:
      return UpdateResult::kSuccess;
    case FwupdUpdateState::kFailed:
      LOG(ERROR) << "No associated update result for kFailed update state.";
      return std::nullopt;
    case FwupdUpdateState::kNeedsReboot:
      return UpdateResult::kNeedsReboot;
    case FwupdUpdateState::kTransient:
      return UpdateResult::kTransient;
  }
  LOG(ERROR) << "Unexpected value for FwupdUpdateState "
             << static_cast<int>(state);
  return std::nullopt;
}

bool SendFwupMetric(MetricsLibraryInterface& metrics,
                    const FwupdDeviceHistory& history) {
  if (history.update_state == FwupdUpdateState::kFailed) {
    bool r = true;
    for (const std::unique_ptr<FwupdRelease>& release : history.releases) {
      std::optional<UpdateResult> status =
          AttemptStatusToUpdateResult(release->last_attempt_status);
      if (!status.has_value() ||
          !metrics.SendEnumToUMA("Platform.FlexUefiCapsuleUpdateResult",
                                 status.value())) {
        LOG(ERROR)
            << "Failed to send FlexUefiCapsuleUpdateResult metric for device "
            << history.name;
        r = false;
      }
    }
    return r;
  } else {
    std::optional<UpdateResult> state =
        UpdateStateToUpdateResult(history.update_state);
    if (!state.has_value() ||
        !metrics.SendEnumToUMA("Platform.FlexUefiCapsuleUpdateResult",
                               state.value())) {
      LOG(ERROR)
          << "Failed to send FlexUefiCapsuleUpdateResult metric for device "
          << history.name;
      return false;
    } else {
      return true;
    }
  }
}
