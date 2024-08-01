// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sysexits.h>

#include <cstdlib>
#include <memory>
#include <optional>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/blkdev_utils/storage_utils.h>
#include <brillo/files/file_util.h>
#include <brillo/flag_helper.h>
#include <brillo/secure_blob.h>
#include <vpd/vpd.h>

#include "minios/log_store_manager.h"
#include "minios/process_manager.h"
#include "minios/utils.h"

namespace minios {

namespace {
constexpr int kMiniOsAPartition = 9;
constexpr int kMiniOsBPartition = 10;
const base::FilePath kChromeOsStateful{"/mnt/stateful_partition/"};

struct ScopedUnmounterTraits {
  static bool InvalidValue() { return false; }
  static void Free(bool is_minios) {
    minios::UnmountStatefulPartition(std::make_shared<ProcessManager>());
  }
};
using ScopedUnmounter = base::ScopedGeneric<bool, ScopedUnmounterTraits>;

}  // namespace

bool EraseStatefulLogs() {
  const auto running_from_minios_opt = IsRunningFromMiniOs();

  // Return failure if we can't determine our environment.
  if (!running_from_minios_opt)
    return false;
  if (running_from_minios_opt.value() &&
      !base::PathExists(kStatefulPath.Append(kUnencryptedMiniosPath)) &&
      !MountStatefulPartition(std::make_shared<ProcessManager>())) {
    LOG(ERROR) << "Failed to mount stateful.";
    return false;
  }
  ScopedUnmounter unmounter(running_from_minios_opt.value());

  const auto source_path =
      (running_from_minios_opt.value() ? kStatefulPath : kChromeOsStateful)
          .Append(kUnencryptedMiniosPath)
          .Append(kLogArchiveFile);

  return brillo::DeleteFile(source_path);
}

bool EraseLogs(
    std::vector<std::shared_ptr<minios::LogStoreManager>> log_store_managers) {
  bool result = true;
  if (!EraseStatefulLogs())
    result = false;
  for (const auto& manager : log_store_managers) {
    if (!manager || !manager->ClearLogs())
      result = false;
  }

  return result;
}

std::optional<bool> RetrieveLogs(
    const std::vector<std::shared_ptr<minios::LogStoreManager>>&
        log_store_managers,
    const std::shared_ptr<minios::LogStoreManager>& stateful_manager,
    const base::FilePath& dest_dir) {
  const auto key = GetLogStoreKey(std::make_shared<vpd::Vpd>());

  if (!key) {
    LOG(WARNING) << "No key found, so no logs to fetch.";
    return false;
  }
  std::optional<bool> logs_retrieved = false;
  for (const auto& manager : log_store_managers) {
    if (!manager) {
      LOG(WARNING) << "Uninitialized manager.";
      logs_retrieved = std::nullopt;
      continue;
    }
    std::optional<bool> result;
    if (manager == stateful_manager) {
      const auto running_from_minios_opt = IsRunningFromMiniOs();
      if (!running_from_minios_opt) {
        LOG(WARNING) << "Could not determine environment.";
        logs_retrieved = std::nullopt;
        continue;
      }
      const auto stateful_source_path =
          (running_from_minios_opt.value() ? kStatefulPath : kChromeOsStateful)
              .Append(kUnencryptedMiniosPath)
              .Append(kLogArchiveFile);
      if (running_from_minios_opt.value() &&
          !base::PathExists(kStatefulPath.Append(kUnencryptedMiniosPath)) &&
          !MountStatefulPartition(std::make_shared<ProcessManager>())) {
        LOG(ERROR) << "Failed to mount stateful.";
        logs_retrieved = std::nullopt;
        continue;
      }
      ScopedUnmounter unmounter(running_from_minios_opt.value());
      result = manager->FetchLogs(LogStoreManager::LogDirection::Stateful,
                                  dest_dir, key.value(), stateful_source_path);

    } else {
      result = manager->FetchLogs(LogStoreManagerInterface::LogDirection::Disk,
                                  dest_dir, key.value());
    }

    if (result && !result.value()) {
      // No logs found, keep searching.
      continue;
    }

    if (!result) {
      LOG(ERROR) << "Error fetching logs.";
      logs_retrieved = std::nullopt;
      continue;
    }
    return true;
  }

  return logs_retrieved;
}

bool ClearKey() {
  const auto vpd = std::make_shared<vpd::Vpd>();
  const auto stored_key = GetLogStoreKey(vpd);

  if (stored_key && stored_key.value() != kNullKey) {
    return ClearLogStoreKey(vpd);
  }
  return true;
}

}  // namespace minios

int main(int argc, char** argv) {
  DEFINE_string(retrieve, "", "Retrieve stored logs to given path.");
  DEFINE_bool(
      erase, false,
      "Erase logs at source after retrieving logs. If specified without "
      "`retrieve`, will erase any unfetched logs on device.");
  DEFINE_bool(clear_key, false,
              "Clear logs store key from device if non-null key is stored.");

  brillo::FlagHelper::Init(argc, argv, "MiniOS Log Retrieval Tool");

  auto log_store_factory = [](std::optional<int> partition) {
    std::shared_ptr<minios::LogStoreManager> manager;
    manager = partition
                  ? std::make_shared<minios::LogStoreManager>(partition.value())
                  : std::make_shared<minios::LogStoreManager>();
    if (!manager->Init(std::make_unique<minios::DiskUtil>(),
                       std::make_unique<crossystem::Crossystem>(),
                       std::make_unique<libstorage::Platform>()))
      manager.reset();
    return manager;
  };

  int exit_code = EXIT_SUCCESS;
  if (!FLAGS_retrieve.empty()) {
    const auto stateful_manager = log_store_factory(std::nullopt),
               slot_a_manager = log_store_factory(minios::kMiniOsAPartition),
               slot_b_manager = log_store_factory(minios::kMiniOsBPartition);

    const auto dest_dir = base::FilePath{FLAGS_retrieve};
    if (!base::DirectoryExists(dest_dir)) {
      LOG(ERROR) << "Invalid retrieval destination=" << dest_dir;
      return EX_USAGE;
    }
    const auto retrieve_result =
        minios::RetrieveLogs({stateful_manager, slot_a_manager, slot_b_manager},
                             stateful_manager, dest_dir);
    if (!retrieve_result)
      exit_code = EXIT_FAILURE;

    if (FLAGS_erase && !minios::EraseLogs({slot_a_manager, slot_b_manager})) {
      exit_code = EXIT_FAILURE;
    }

  } else if (FLAGS_erase) {
    const auto slot_a_manager = log_store_factory(minios::kMiniOsAPartition),
               slot_b_manager = log_store_factory(minios::kMiniOsBPartition);
    if (!minios::EraseLogs({slot_a_manager, slot_b_manager}))
      exit_code = EXIT_FAILURE;
  }
  if (FLAGS_clear_key && !minios::ClearKey()) {
    exit_code = EXIT_FAILURE;
  }

  return exit_code;
}
