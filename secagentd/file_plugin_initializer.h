// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_FILE_PLUGIN_INITIALIZER_H_
#define SECAGENTD_FILE_PLUGIN_INITIALIZER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "secagentd/bpf/bpf_types.h"
#include "secagentd/plugins.h"

namespace secagentd {
// File path types (from your original code)
enum class FilePathName {
  USER_FILES_DIR,
  COOKIES_DIR,
  COOKIES_JOURNAL_DIR,
  SAFE_BROWSING_COOKIES_DIR,
  SAFE_BROWSING_COOKIES_JOURNAL_DIR,
  USER_SECRET_STASH_DIR,
  ROOT,
  MOUNTED_ARCHIVE,
  GOOGLE_DRIVE_FS,
  STATEFUL_PARTITION,
  USB_STORAGE,
  DEVICE_SETTINGS_POLICY_DIR,
  DEVICE_SETTINGS_OWNER_KEY,
  SESSION_MANAGER_POLICY_DIR,
  SESSION_MANAGER_POLICY_KEY,
  CRYPTOHOME_KEY,
  CRYPTOHOME_ECC_KEY,
  // Add the last element of the enum, used for counting
  FILE_PATH_NAME_COUNT,
};

// Enum for file path categories
enum class FilePathCategory { USER_PATH, SYSTEM_PATH, REMOVABLE_PATH };

// Structure to hold path information
struct PathInfo {
  std::string pathPrefix;  // Store the full path for non-user paths and for
                           // user part before the hash placeholder
  std::optional<std::string> pathSuffix;  // Only for user hash paths. Store the
                                          // part after the hash placeholder
  bpf::file_monitoring_mode monitoringMode;
  cros_xdr::reporting::SensitiveFileType fileType;
  FilePathCategory pathCategory;
  std::optional<std::string> fullResolvedPath;
  bpf::device_monitoring_type deviceMonitoringType =
      bpf::device_monitoring_type::MONITOR_SPECIFIC_FILES;
};
class FilePluginInitializer {
 public:
  // Main initialization function (this is public)
  static absl::Status InitializeFileBpfMaps(
      const std::unique_ptr<BpfSkeletonHelperInterface>& helper,

      const std::string& userhash);

  static absl::Status OnUserLogin(
      const std::unique_ptr<BpfSkeletonHelperInterface>& helper,
      const std::string& userHash);

  static absl::Status OnUserLogout(
      const std::unique_ptr<BpfSkeletonHelperInterface>& bpfHelper,
      const std::string& userHash);

  static absl::Status OnDeviceMount(
      const std::unique_ptr<BpfSkeletonHelperInterface>& bpfHelper,
      const std::string& mount_point);

 private:
  // Function to update the BPF map with inode IDs for files in directories
  // specified in paths_map recursively, limited to a specific device ID
  static absl::Status PopulateFlagsMap(int fd);

  // Updates a BPF map with inode IDs and monitoring modes for files specified
  // in a map of paths.
  static absl::Status UpdateBPFMapForPathInodes(
      int bpfMapFd,
      const std::map<FilePathName, std::vector<PathInfo>>& pathsMap,
      const std::optional<std::string>& optionalUserhash);

  // Removes entries from the BPF map based on inode-device key mappings
  // associated with a specific userhash.
  static absl::Status RemoveKeysFromBPFMap(int bpfMapFd,
                                           const std::string& userhash);

  // Updates a BPF map with device IDs based on the paths and their associated
  // monitoring modes.
  static absl::Status AddDeviceIdsToBPFMap(
      int bpfMapFd,
      const std::map<FilePathName, std::vector<PathInfo>>& pathsMap);

  // Constructs a map of full paths based on the specified file path category
  // and optional user hash.
  static absl::Status PopulatePathsMapByCategory(
      FilePathCategory category,
      const std::optional<std::string>& optionalUserHash,
      std::map<FilePathName, std::vector<PathInfo>>& pathInfoMap);

  // Constructs a map of all path information based on the provided user hash.
  // This includes paths for USER_PATH, SYSTEM_PATH, and REMOVABLE_PATH
  // categories.
  static std::map<FilePathName, std::vector<PathInfo>> ConstructAllPathsMap(
      const std::optional<std::string>& userHash);

  // Updates BPF maps with paths and their associated information.
  // This function updates various BPF maps based on the provided paths and
  // their monitoring modes. It uses a helper interface to retrieve the file
  // descriptors for the BPF maps and performs updates on the maps accordingly.
  // It includes error handling for map retrieval and update operations, with
  // relevant logging for diagnostics.
  static absl::Status UpdateBPFMapForPathMaps(
      const std::optional<std::string>& optionalUserhash,
      const std::unique_ptr<BpfSkeletonHelperInterface>& helper,
      const std::map<FilePathName, std::vector<PathInfo>>& paths_map);
};

}  // namespace secagentd

#endif  // SECAGENTD_FILE_PLUGIN_INITIALIZER_H_
