// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secagentd/file_plugin_initializer.h"

// BPF headers
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <bpf/bpf.h>

#include "base/memory/weak_ptr.h"
#include "secagentd/bpf/bpf_types.h"

// C standard headers
#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#include <cerrno>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

// Protocol headers
#include "secagentd/platform.h"
#include "secagentd/plugins.h"

#define BUF_SIZE 4096
// Define a constant for the {HASH} placeholder
#define HASH_PLACEHOLDER "{HASH}"

namespace {

// Path to monitor
static const std::map<secagentd::FilePathName, secagentd::PathInfo>
    file_path_info_map = {
        {secagentd::FilePathName::USER_FILES_DIR,
         {"/home/chronos/u-", "/MyFiles",
          secagentd::bpf::file_monitoring_mode::READ_AND_READ_WRITE_BOTH,
          cros_xdr::reporting::SensitiveFileType::USER_FILE}},
        {secagentd::FilePathName::COOKIES_DIR,
         {"/home/chronos/u-", "/Cookies",
          secagentd::bpf::file_monitoring_mode::READ_AND_READ_WRITE_BOTH,
          cros_xdr::reporting::SensitiveFileType::USER_WEB_COOKIE}},
        {secagentd::FilePathName::COOKIES_JOURNAL_DIR,
         {"/home/chronos/u-", "/Cookies-journal",
          secagentd::bpf::file_monitoring_mode::READ_AND_READ_WRITE_BOTH,
          cros_xdr::reporting::SensitiveFileType::USER_WEB_COOKIE}},
        {secagentd::FilePathName::SAFE_BROWSING_COOKIES_DIR,
         {"/home/chronos/u-", "/Safe Browsing Cookies",
          secagentd::bpf::file_monitoring_mode::READ_AND_READ_WRITE_BOTH,
          cros_xdr::reporting::SensitiveFileType::USER_WEB_COOKIE}},
        {secagentd::FilePathName::SAFE_BROWSING_COOKIES_JOURNAL_DIR,
         {"/home/chronos/u-", "/Safe Browsing Cookies-journal",
          secagentd::bpf::file_monitoring_mode::READ_AND_READ_WRITE_BOTH,
          cros_xdr::reporting::SensitiveFileType::USER_WEB_COOKIE}},
        {secagentd::FilePathName::USER_SECRET_STASH_DIR,
         {"/home/.shadow/", "/user_secret_stash",
          secagentd::bpf::file_monitoring_mode::READ_AND_READ_WRITE_BOTH,
          cros_xdr::reporting::SensitiveFileType::USER_ENCRYPTED_CREDENTIAL}},
        {secagentd::FilePathName::ROOT,
         {"/", std::nullopt,
          secagentd::bpf::file_monitoring_mode::READ_WRITE_ONLY,
          cros_xdr::reporting::SensitiveFileType::ROOT_FS, std::nullopt,
          secagentd::bpf::device_monitoring_type::MONITOR_ALL_FILES}},
        {secagentd::FilePathName::MOUNTED_ARCHIVE,
         {"/media/archive", std::nullopt,
          secagentd::bpf::file_monitoring_mode::READ_AND_READ_WRITE_BOTH,
          cros_xdr::reporting::SensitiveFileType::USER_FILE}},
        {secagentd::FilePathName::GOOGLE_DRIVE_FS,
         {"/media/fuse/drivefs-", "/",
          secagentd::bpf::file_monitoring_mode::READ_AND_READ_WRITE_BOTH,
          cros_xdr::reporting::SensitiveFileType::USER_GOOGLE_DRIVE_FILE}},
        {secagentd::FilePathName::STATEFUL_PARTITION,
         {"/home/.shadow/", "/auth_factors",
          secagentd::bpf::file_monitoring_mode::READ_WRITE_ONLY,
          cros_xdr::reporting::SensitiveFileType::USER_AUTH_FACTORS_FILE}},
        {secagentd::FilePathName::USB_STORAGE,
         {"/media/removable/", std::nullopt,
          secagentd::bpf::file_monitoring_mode::READ_WRITE_ONLY,
          cros_xdr::reporting::SensitiveFileType::USB_MASS_STORAGE}},
        {secagentd::FilePathName::DEVICE_SETTINGS_POLICY_DIR,
         {"/var/lib/devicesettings/policy", std::nullopt,
          secagentd::bpf::file_monitoring_mode::READ_WRITE_ONLY,
          cros_xdr::reporting::SensitiveFileType::DEVICE_POLICY}},
        {secagentd::FilePathName::DEVICE_SETTINGS_OWNER_KEY,
         {"/var/lib/devicesettings/owner.key", std::nullopt,
          secagentd::bpf::file_monitoring_mode::READ_WRITE_ONLY,
          cros_xdr::reporting::SensitiveFileType::DEVICE_POLICY_PUBLIC_KEY}},
        {secagentd::FilePathName::SESSION_MANAGER_POLICY_DIR,
         {"/run/daemon-store/session_manager/", "/policy/policy",
          secagentd::bpf::file_monitoring_mode::READ_WRITE_ONLY,
          cros_xdr::reporting::SensitiveFileType::USER_POLICY}},
        {secagentd::FilePathName::SESSION_MANAGER_POLICY_KEY,
         {"/run/daemon-store/session_manager/", "/policy/key",
          secagentd::bpf::file_monitoring_mode::READ_WRITE_ONLY,
          cros_xdr::reporting::SensitiveFileType::USER_POLICY_PUBLIC_KEY}},
        {secagentd::FilePathName::CRYPTOHOME_KEY,
         {"/home/.shadow/cryptohome.key", std::nullopt,
          secagentd::bpf::file_monitoring_mode::READ_AND_READ_WRITE_BOTH,
          cros_xdr::reporting::SensitiveFileType::SYSTEM_TPM_PUBLIC_KEY}},
        {secagentd::FilePathName::CRYPTOHOME_ECC_KEY,
         {"/home/.shadow/cryptohome.ecc.key", std::nullopt,
          secagentd::bpf::file_monitoring_mode::READ_AND_READ_WRITE_BOTH,
          cros_xdr::reporting::SensitiveFileType::SYSTEM_TPM_PUBLIC_KEY}},
};

// Path Category -> List of FilePathName enums
const std::map<secagentd::FilePathCategory,
               std::vector<secagentd::FilePathName>>
    file_path_names_by_category = {
        {secagentd::FilePathCategory::USER_PATH,
         {secagentd::FilePathName::USER_FILES_DIR,
          secagentd::FilePathName::COOKIES_DIR,
          secagentd::FilePathName::COOKIES_JOURNAL_DIR,
          secagentd::FilePathName::SAFE_BROWSING_COOKIES_DIR,
          secagentd::FilePathName::SAFE_BROWSING_COOKIES_JOURNAL_DIR,
          secagentd::FilePathName::USER_SECRET_STASH_DIR,
          secagentd::FilePathName::GOOGLE_DRIVE_FS,
          secagentd::FilePathName::STATEFUL_PARTITION,
          secagentd::FilePathName::SESSION_MANAGER_POLICY_DIR,
          secagentd::FilePathName::SESSION_MANAGER_POLICY_KEY}},
        {secagentd::FilePathCategory::SYSTEM_PATH,
         {secagentd::FilePathName::ROOT,
          secagentd::FilePathName::DEVICE_SETTINGS_POLICY_DIR,
          secagentd::FilePathName::DEVICE_SETTINGS_OWNER_KEY,
          secagentd::FilePathName::CRYPTOHOME_KEY,
          secagentd::FilePathName::CRYPTOHOME_ECC_KEY}},
        {secagentd::FilePathCategory::REMOVABLE_PATH,
         {secagentd::FilePathName::MOUNTED_ARCHIVE,
          secagentd::FilePathName::USB_STORAGE}}};
}  // namespace

namespace secagentd {

static dev_t UserspaceToKernelDeviceId(const struct statx& fileStatx) {
  dev_t userspace_dev =
      makedev(fileStatx.stx_dev_major, fileStatx.stx_dev_minor);
  // Extract the minor number from the user-space device ID
  unsigned minor = (userspace_dev & 0xff) | ((userspace_dev >> 12) & ~0xff);

  // Extract the major number from the user-space device ID
  unsigned major = (userspace_dev >> 8) & 0xfff;

  // Combine the major and minor numbers to form the kernel-space device ID
  dev_t kernel_dev = (static_cast<dev_t>(major) << 20) | minor;

  return kernel_dev;
}

absl::StatusOr<const struct statx> RetrieveFileStatistics(
    int dirFd, const std::string& path) {
  struct statx fileStatx;
  // Retrieve file information for the current path using statx
  base::WeakPtr<PlatformInterface> platform = GetPlatform();
  if (platform->Sys_statx(dirFd, path.c_str(), AT_STATX_DONT_SYNC,
                          STATX_INO | STATX_BASIC_STATS, &fileStatx) == -1) {
    // Check the type of error encountered
    if (errno == ENOENT) {
      // Path does not exist
      return absl::NotFoundError(strerror(errno));
    } else {
      // Other errors (e.g., permission issues, file system errors)
      return absl::InternalError(strerror(errno));
    }
  }
  // File statistics retrieved successfully
  return fileStatx;
}

absl::Status FilePluginInitializer::PopulatePathsMapByCategory(
    FilePathCategory category,
    const std::optional<std::string>& optionalUserHash,
    std::map<FilePathName, PathInfo>& pathInfoMap) {
  // Verify if the provided category exists in the predefined mappings
  auto categoryIt = file_path_names_by_category.find(category);
  if (categoryIt == file_path_names_by_category.end()) {
    return absl::InvalidArgumentError(
        "Invalid FilePathCategory: " +
        std::to_string(static_cast<int>(category)));
  }

  const std::vector<FilePathName>& filePathNames = categoryIt->second;

  // Check if user hash is required for the given category and is provided
  if (category == FilePathCategory::USER_PATH &&
      !optionalUserHash.has_value()) {
    return absl::InvalidArgumentError(
        "Userhash needs to be provided for user path category.");
  }

  // Process each file path name for the specified category
  for (const FilePathName& pathName : filePathNames) {
    // Verify if the provided category exists in the predefined mappings
    auto filePathIt = file_path_info_map.find(pathName);
    if (filePathIt == file_path_info_map.end()) {
      return absl::InvalidArgumentError(
          "Invalid FilePathName: " +
          std::to_string(static_cast<int>(pathName)));
    }
    PathInfo pathInfo = filePathIt->second;

    // Replace the placeholder with the actual user hash if applicable
    if (category == FilePathCategory::USER_PATH) {
      pathInfo.fullResolvedPath = pathInfo.pathPrefix +
                                  optionalUserHash.value() +
                                  pathInfo.pathSuffix.value();
    } else {
      pathInfo.fullResolvedPath = pathInfo.pathPrefix;
    }

    pathInfoMap[pathName] = pathInfo;
  }

  return absl::OkStatus();
}

std::map<FilePathName, PathInfo> FilePluginInitializer::ConstructAllPathsMap(
    const std::optional<std::string>& optionalUserHash) {
  std::map<FilePathName, PathInfo> pathInfoMap;

  // Check if userHash is provided for USER_PATH category
  if (optionalUserHash) {
    // Populate paths for USER_PATH category using the provided userHash
    absl::Status status = PopulatePathsMapByCategory(
        FilePathCategory::USER_PATH, optionalUserHash, pathInfoMap);
    if (!status.ok()) {
      LOG(ERROR) << "Failed to populate paths for USER_PATH category: "
                 << status;
    }
  }

  // Populate paths for SYSTEM_PATH and REMOVABLE_PATH categories without
  // userHash
  absl::Status status = PopulatePathsMapByCategory(
      FilePathCategory::SYSTEM_PATH, std::nullopt, pathInfoMap);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to populate paths for SYSTEM_PATH category: "
               << status;
  }
  status = PopulatePathsMapByCategory(FilePathCategory::REMOVABLE_PATH,
                                      std::nullopt, pathInfoMap);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to populate paths for REMOVABLE_PATH category: "
               << status;
  }

  return pathInfoMap;
}

absl::Status FilePluginInitializer::PopulateFlagsMap(int fd) {
  // Array of flag key-value pairs to populate the BPF map
  const std::vector<std::pair<uint32_t, uint64_t>> flagKeyValuePairs = {
      {O_DIRECTORY_FLAG_KEY, O_DIRECTORY},
      {O_TMPFILE_FLAG_KEY, (__O_TMPFILE | O_DIRECTORY)},
      {O_RDONLY_FLAG_KEY, O_RDONLY},
      {O_ACCMODE_FLAG_KEY, O_ACCMODE}};

  // Iterate through the key-value pairs and update the BPF map
  for (const auto& flagPair : flagKeyValuePairs) {
    // Attempt to update the BPF map with the current key-value pair
    base::WeakPtr<PlatformInterface> platform = GetPlatform();

    if (platform->BpfMapUpdateElemBtFD(fd, &flagPair.first, &flagPair.second,
                                       BPF_ANY) != 0) {
      return absl::InternalError("Failed to update BPF map.");
    }
  }

  return absl::OkStatus();
}

absl::Status FilePluginInitializer::UpdateBPFMapForPathInodes(
    int bpfMapFd, const std::map<FilePathName, PathInfo>& pathsMap) {
  // Open the root directory to use with fstatat for file information retrieval
  int root_fd = open("/", O_RDONLY | O_DIRECTORY);
  if (root_fd == -1) {
    return absl::InternalError(strerror(errno));
  }

  // Iterate over the map of file paths and their associated information
  for (const auto& [pathName, pathInfo] : pathsMap) {
    const std::string& path =
        pathInfo.fullResolvedPath.value();  // Current path to process
    secagentd::bpf::file_monitoring_mode monitoringMode =
        pathInfo.monitoringMode;  // Monitoring mode for the path

    // Retrieve file information for the current path using fstatat
    absl::StatusOr<const struct statx> file_statx_result =
        RetrieveFileStatistics(root_fd, path.c_str());
    if (!file_statx_result.ok()) {
      LOG(ERROR) << "Failed to retrieve file statistics for " << path << ": "
                 << file_statx_result.status();
      continue;  // Skip to the next path in the map
    }
    const struct statx& fileStatx = file_statx_result.value();

    // Prepare the BPF map key with inode ID and device ID
    struct bpf::inode_dev_map_key bpfMapKey = {
        .inode_id = fileStatx.stx_ino,
        .dev_id = UserspaceToKernelDeviceId(fileStatx)};

    // Update the BPF map with the inode key and monitoring mode value
    base::WeakPtr<PlatformInterface> platform = GetPlatform();

    if (platform->BpfMapUpdateElemBtFD(bpfMapFd, &bpfMapKey, &monitoringMode,
                                       0) != 0) {
      LOG(ERROR) << "Failed to update BPF map entry for path " << path
                 << ". Inode: " << bpfMapKey.inode_id
                 << ", Device ID: " << bpfMapKey.dev_id;
      continue;  // Continue processing the next path in the map
    }

    // Log success message for the current path
    LOG(INFO) << "Successfully added entry to BPF map for path " << path
              << ". Inode: " << bpfMapKey.inode_id
              << ", Device ID: " << bpfMapKey.dev_id;
  }

  close(root_fd);  // Close the root directory file descriptor
  return absl::OkStatus();
}

absl::Status FilePluginInitializer::AddDeviceIdsToBPFMap(
    int bpfMapFd, const std::map<FilePathName, PathInfo>& pathsMap) {
  // Validate BPF map file descriptor
  if (bpfMapFd < 0) {
    return absl::InvalidArgumentError("Invalid BPF map file descriptor.");
  }

  // Open the root directory for use with fstatat
  int root_fd = open("/", O_RDONLY | O_DIRECTORY);
  if (root_fd == -1) {
    return absl::InternalError(strerror(errno));
  }

  // Iterate through each path and update the BPF map
  for (const auto& [pathName, pathInfo] : pathsMap) {
    const std::string& path =
        pathInfo.fullResolvedPath.value();  // Current path to process

    // Retrieve file information for the current path using fstatat
    absl::StatusOr<const struct statx> file_statx_result =
        RetrieveFileStatistics(root_fd, path.c_str());
    if (!file_statx_result.ok()) {
      LOG(ERROR) << "Failed to retrieve file statistics for " << path << ": "
                 << file_statx_result.status();
      continue;  // Skip to the next path in the map
    }
    const struct statx& fileStatx = file_statx_result.value();

    // Convert userspace device ID to kernel device ID
    dev_t deviceId = UserspaceToKernelDeviceId(fileStatx);

    struct bpf::device_file_monitoring_settings bpfSettings = {
        .device_monitoring_type = pathInfo.deviceMonitoringType,
        .file_monitoring_mode = pathInfo.monitoringMode,
    };

    // Update BPF map with the device ID and settings
    base::WeakPtr<PlatformInterface> platform = GetPlatform();

    if (platform->BpfMapUpdateElemBtFD(bpfMapFd, &deviceId, &bpfSettings,
                                       BPF_ANY) != 0) {
      LOG(ERROR) << "Failed to update BPF map entry for device ID " << deviceId;
      continue;  // Skip to the next path
    }

    LOG(INFO) << "Added device ID " << deviceId << " with monitoring mode "
              << static_cast<int>(pathInfo.monitoringMode)
              << " with device monitoring type "
              << static_cast<int>(pathInfo.deviceMonitoringType)
              << " to BPF map.";
  }

  close(root_fd);  // Close the root directory file descriptor
  return absl::OkStatus();
}

absl::Status FilePluginInitializer::UpdateBPFMapForPathMaps(
    const std::string& userHash,
    const std::unique_ptr<BpfSkeletonHelperInterface>& bpfHelper,

    const std::map<FilePathName, PathInfo>& pathsMap) {
  // Retrieve file descriptor for the 'allowlisted_directory_inodes' BPF map
  absl::StatusOr<int> mapFdResult =
      bpfHelper->FindBpfMapByName("allowlisted_directory_inodes");
  if (!mapFdResult.ok()) {
    LOG(ERROR) << "Failed to find BPF map 'allowlisted_directory_inodes': "
               << mapFdResult.status();
    return mapFdResult.status();
  }

  int directoryInodesMapFd = mapFdResult.value();
  absl::Status status =
      UpdateBPFMapForPathInodes(directoryInodesMapFd, pathsMap);
  if (!status.ok()) {
    return status;
  }

  // Retrieve file descriptor for the 'device_file_monitoring_allowlist' BPF
  // map
  mapFdResult = bpfHelper->FindBpfMapByName("device_file_monitoring_allowlist");
  if (!mapFdResult.ok()) {
    return mapFdResult.status();
  }

  int deviceMonitoringMapFd = mapFdResult.value();
  status = AddDeviceIdsToBPFMap(deviceMonitoringMapFd, pathsMap);
  if (!status.ok()) {
    return status;
  }
  return absl::OkStatus();
}

absl::Status FilePluginInitializer::InitializeFileBpfMaps(
    const std::unique_ptr<BpfSkeletonHelperInterface>& helper,

    const std::string& userhash) {
  assert(file_path_info_map.size() ==
         static_cast<int>(FilePathName::FILE_PATH_NAME_COUNT));
  // Construct the paths map based on the user hash
  std::map<FilePathName, PathInfo> paths_map = ConstructAllPathsMap(userhash);

  // Update map for flags
  absl::StatusOr<int> fd_result =
      helper->FindBpfMapByName("system_flags_shared");
  if (!fd_result.ok()) {
    return fd_result.status();
  }

  int fd = fd_result.value();
  absl::Status status = PopulateFlagsMap(fd);
  if (!status.ok()) {
    return status;
  }

  // TODO(b/360058671): Add hardlinks processing.

  return UpdateBPFMapForPathMaps(userhash, helper, paths_map);
}
}  // namespace secagentd
