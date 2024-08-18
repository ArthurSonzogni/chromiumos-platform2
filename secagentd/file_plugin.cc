// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "secagentd/bpf/bpf_types.h"
#include "secagentd/device_user.h"
#include "secagentd/plugins.h"
#include "secagentd/proto/security_xdr_events.pb.h"

// BPF headers
#include <absl/status/statusor.h>
#include <bpf/bpf.h>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "linux/bpf.h"

// C standard headers
#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#include <cerrno>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#define BUF_SIZE 4096
// Define a constant for the {HASH} placeholder
#define HASH_PLACEHOLDER "{HASH}"

namespace {

const char kDeviceSettingsBasePath[] = "/var/lib/devicesettings/";

// Path to monitor
static const std::map<secagentd::FilePathName, secagentd::PathInfo>
    kFilePathInfoMap = {
        {secagentd::FilePathName::USER_FILES_DIR,
         {"/home/chronos/u-", "/MyFiles",
          secagentd::bpf::file_monitoring_mode::READ_AND_READ_WRITE_BOTH,
          cros_xdr::reporting::SensitiveFileType::USER_FILE,
          secagentd::FilePathCategory::USER_PATH}},
        {secagentd::FilePathName::COOKIES_DIR,
         {"/home/chronos/u-", "/Cookies",
          secagentd::bpf::file_monitoring_mode::READ_AND_READ_WRITE_BOTH,
          cros_xdr::reporting::SensitiveFileType::USER_WEB_COOKIE,
          secagentd::FilePathCategory::USER_PATH}},
        {secagentd::FilePathName::COOKIES_JOURNAL_DIR,
         {"/home/chronos/u-", "/Cookies-journal",
          secagentd::bpf::file_monitoring_mode::READ_AND_READ_WRITE_BOTH,
          cros_xdr::reporting::SensitiveFileType::USER_WEB_COOKIE,
          secagentd::FilePathCategory::USER_PATH}},
        {secagentd::FilePathName::SAFE_BROWSING_COOKIES_DIR,
         {"/home/chronos/u-", "/Safe Browsing Cookies",
          secagentd::bpf::file_monitoring_mode::READ_AND_READ_WRITE_BOTH,
          cros_xdr::reporting::SensitiveFileType::USER_WEB_COOKIE,
          secagentd::FilePathCategory::USER_PATH}},
        {secagentd::FilePathName::SAFE_BROWSING_COOKIES_JOURNAL_DIR,
         {"/home/chronos/u-", "/Safe Browsing Cookies-journal",
          secagentd::bpf::file_monitoring_mode::READ_AND_READ_WRITE_BOTH,
          cros_xdr::reporting::SensitiveFileType::USER_WEB_COOKIE,
          secagentd::FilePathCategory::USER_PATH}},
        {secagentd::FilePathName::USER_SECRET_STASH_DIR,
         {"/home/.shadow/", "/user_secret_stash",
          secagentd::bpf::file_monitoring_mode::READ_AND_READ_WRITE_BOTH,
          cros_xdr::reporting::SensitiveFileType::USER_ENCRYPTED_CREDENTIAL,
          secagentd::FilePathCategory::USER_PATH}},
        {secagentd::FilePathName::ROOT,
         {"/", std::nullopt,
          secagentd::bpf::file_monitoring_mode::READ_WRITE_ONLY,
          cros_xdr::reporting::SensitiveFileType::ROOT_FS,
          secagentd::FilePathCategory::SYSTEM_PATH, std::nullopt,
          secagentd::bpf::device_monitoring_type::MONITOR_ALL_FILES}},
        {secagentd::FilePathName::MOUNTED_ARCHIVE,
         {"/media/archive", std::nullopt,
          secagentd::bpf::file_monitoring_mode::READ_AND_READ_WRITE_BOTH,
          cros_xdr::reporting::SensitiveFileType::USER_FILE,
          secagentd::FilePathCategory::REMOVABLE_PATH}},
        {secagentd::FilePathName::GOOGLE_DRIVE_FS,
         {"/media/fuse/drivefs-", "/",
          secagentd::bpf::file_monitoring_mode::READ_AND_READ_WRITE_BOTH,
          cros_xdr::reporting::SensitiveFileType::USER_GOOGLE_DRIVE_FILE,
          secagentd::FilePathCategory::USER_PATH}},
        {secagentd::FilePathName::STATEFUL_PARTITION,
         {"/home/.shadow/", "/auth_factors",
          secagentd::bpf::file_monitoring_mode::READ_WRITE_ONLY,
          cros_xdr::reporting::SensitiveFileType::USER_AUTH_FACTORS_FILE,
          secagentd::FilePathCategory::USER_PATH}},
        {secagentd::FilePathName::USB_STORAGE,
         {"/media/removable/", std::nullopt,
          secagentd::bpf::file_monitoring_mode::READ_WRITE_ONLY,
          cros_xdr::reporting::SensitiveFileType::USB_MASS_STORAGE,
          secagentd::FilePathCategory::REMOVABLE_PATH}},
        {secagentd::FilePathName::DEVICE_SETTINGS_POLICY_DIR,
         {"/var/lib/devicesettings/policy.", std::nullopt,
          secagentd::bpf::file_monitoring_mode::READ_WRITE_ONLY,
          cros_xdr::reporting::SensitiveFileType::DEVICE_POLICY,
          secagentd::FilePathCategory::SYSTEM_PATH}},
        {secagentd::FilePathName::DEVICE_SETTINGS_OWNER_KEY,
         {"/var/lib/devicesettings/owner.key", std::nullopt,
          secagentd::bpf::file_monitoring_mode::READ_WRITE_ONLY,
          cros_xdr::reporting::SensitiveFileType::DEVICE_POLICY_PUBLIC_KEY,
          secagentd::FilePathCategory::SYSTEM_PATH}},
        {secagentd::FilePathName::SESSION_MANAGER_POLICY_DIR,
         {"/run/daemon-store/session_manager/", "/policy/policy",
          secagentd::bpf::file_monitoring_mode::READ_WRITE_ONLY,
          cros_xdr::reporting::SensitiveFileType::USER_POLICY,
          secagentd::FilePathCategory::USER_PATH}},
        {secagentd::FilePathName::SESSION_MANAGER_POLICY_KEY,
         {"/run/daemon-store/session_manager/", "/policy/key",
          secagentd::bpf::file_monitoring_mode::READ_WRITE_ONLY,
          cros_xdr::reporting::SensitiveFileType::USER_POLICY_PUBLIC_KEY,
          secagentd::FilePathCategory::USER_PATH}},
        {secagentd::FilePathName::CRYPTOHOME_KEY,
         {"/home/.shadow/cryptohome.key", std::nullopt,
          secagentd::bpf::file_monitoring_mode::READ_AND_READ_WRITE_BOTH,
          cros_xdr::reporting::SensitiveFileType::SYSTEM_TPM_PUBLIC_KEY,
          secagentd::FilePathCategory::SYSTEM_PATH}},
        {secagentd::FilePathName::CRYPTOHOME_ECC_KEY,
         {"/home/.shadow/cryptohome.ecc.key", std::nullopt,
          secagentd::bpf::file_monitoring_mode::READ_AND_READ_WRITE_BOTH,
          cros_xdr::reporting::SensitiveFileType::SYSTEM_TPM_PUBLIC_KEY,
          secagentd::FilePathCategory::SYSTEM_PATH}},
};

// Path Category -> List of FilePathName enums
const std::map<secagentd::FilePathCategory,
               std::vector<secagentd::FilePathName>>
    kFilePathNamesByCategory = {
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

// Function to match a path prefix to FilePathName
std::optional<std::pair<const secagentd::FilePathName, secagentd::PathInfo>>
MatchPathToFilePathPrefixName(const std::string& path) {
  for (const auto& pair : kFilePathInfoMap) {
    if (path.find(pair.second.pathPrefix) == 0) {
      return pair;
    }
  }
  return std::nullopt;
}

const std::optional<std::string> ConstructOptionalUserhash(
    const std::string& userhash) {
  if (userhash.empty() || userhash == secagentd::device_user::kUnknown) {
    return std::nullopt;
  }
  return userhash;
}
}  // namespace

namespace secagentd {
namespace pb = cros_xdr::reporting;

std::map<std::string, std::vector<bpf::inode_dev_map_key>> userhash_inodes_map =
    {};

FilePlugin::FilePlugin(
    scoped_refptr<BpfSkeletonFactoryInterface> bpf_skeleton_factory,
    scoped_refptr<MessageSenderInterface> message_sender,
    scoped_refptr<ProcessCacheInterface> process_cache,
    scoped_refptr<PoliciesFeaturesBrokerInterface> policies_features_broker,
    scoped_refptr<DeviceUserInterface> device_user,
    uint32_t batch_interval_s)
    : weak_ptr_factory_(this),
      process_cache_(process_cache),
      policies_features_broker_(policies_features_broker),
      device_user_(device_user),
      batch_sender_(std::make_unique<BatchSender<std::string,
                                                 pb::XdrFileEvent,
                                                 pb::FileEventAtomicVariant>>(
          base::BindRepeating(
              [](const cros_xdr::reporting::FileEventAtomicVariant&)
                  -> std::string {
                // TODO(b:282814056): Make hashing function optional
                //  for batch_sender then drop this. Not all users
                //  of batch_sender need the visit functionality.
                return "";
              }),
          message_sender,
          reporting::Destination::CROS_SECURITY_FILE,
          batch_interval_s)),
      bpf_skeleton_helper_(
          std::make_unique<BpfSkeletonHelper<Types::BpfSkeleton::kFile>>(
              bpf_skeleton_factory, batch_interval_s)) {
  CHECK(message_sender != nullptr);
  CHECK(process_cache != nullptr);
  CHECK(bpf_skeleton_factory);
}

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

absl::StatusOr<const struct statx> GetFStat(int dirFd,
                                            const std::string& path) {
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

// Traverses the base directory and applies a callback function to each
// subdirectory.

void TraverseDirectories(
    const std::string& baseDir,
    base::RepeatingCallback<void(const std::string&)> callback,
    bool processSubDirectories,
    bool processFiles) {
  // Check if the base directory exists and is a directory
  if (!std::filesystem::exists(baseDir) ||
      !std::filesystem::is_directory(baseDir)) {
    LOG(ERROR) << "The directory " << baseDir
               << " does not exist or is not a directory.";
    return;
  }

  // Iterate over all entries in the base directory
  for (const auto& entry : std::filesystem::directory_iterator(baseDir)) {
    // Check if the entry is a directory
    if ((entry.is_directory() && processSubDirectories) ||
        (entry.is_regular_file() && processFiles)) {
      // Apply the callback function to the directory path
      callback.Run(entry.path().string());
    }
  }
}

absl::Status PopulatePathsMapByCategory(
    FilePathCategory category,
    const std::optional<std::string>& optionalUserHash,
    std::map<FilePathName, std::vector<PathInfo>>& pathInfoMap) {
  // Verify if the provided category exists in the predefined mappings
  auto categoryIt = kFilePathNamesByCategory.find(category);
  if (categoryIt == kFilePathNamesByCategory.end()) {
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
    auto filePathIt = kFilePathInfoMap.find(pathName);
    if (filePathIt == kFilePathInfoMap.end()) {
      return absl::InvalidArgumentError(
          "Invalid FilePathName: " +
          std::to_string(static_cast<int>(pathName)));
    }
    PathInfo pathInfo = filePathIt->second;

    if (categoryIt->first == FilePathCategory::REMOVABLE_PATH) {
      TraverseDirectories(
          pathInfo.pathPrefix,
          base::BindRepeating(
              [](std::map<FilePathName, std::vector<PathInfo>>* pathInfoMap,
                 PathInfo* pathInfo, FilePathName pathName,
                 const std::string& path) {
                pathInfo->fullResolvedPath = path;
                pathInfoMap->at(pathName).push_back(*pathInfo);
              },
              base::Unretained(&pathInfoMap), base::Unretained(&pathInfo),
              pathName),
          true, false);
    } else if (pathName == FilePathName::DEVICE_SETTINGS_OWNER_KEY ||
               pathName == FilePathName::DEVICE_SETTINGS_POLICY_DIR) {
      if (pathName == FilePathName::DEVICE_SETTINGS_OWNER_KEY) {
        continue;  // Process in the policy
      }

      TraverseDirectories(
          kDeviceSettingsBasePath,
          base::BindRepeating(
              [](std::map<FilePathName, std::vector<PathInfo>>* pathInfoMap,
                 const std::string& path) {
                auto pair = MatchPathToFilePathPrefixName(path);
                if (pair.has_value()) {
                  pair.value().second.fullResolvedPath = path;
                  pathInfoMap->at(pair.value().first)
                      .push_back(pair.value().second);
                }
              },
              base::Unretained(&pathInfoMap)),
          false, true);
    } else if (category == FilePathCategory::USER_PATH) {
      pathInfo.fullResolvedPath = pathInfo.pathPrefix +
                                  optionalUserHash.value() +
                                  pathInfo.pathSuffix.value();
      pathInfoMap[pathName].push_back(pathInfo);
    } else {
      pathInfo.fullResolvedPath = pathInfo.pathPrefix;
      pathInfoMap[pathName].push_back(pathInfo);
    }
  }

  return absl::OkStatus();
}

std::map<FilePathName, std::vector<PathInfo>> ConstructAllPathsMap(
    const std::optional<std::string>& optionalUserHash) {
  std::map<FilePathName, std::vector<PathInfo>> pathInfoMap;

  // Check if userHash is provided for USER_PATH category
  if (optionalUserHash.has_value()) {
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

absl::Status PopulateFlagsMap(int fd) {
  // Array of flag key-value pairs to populate the BPF map
  const std::vector<std::pair<uint32_t, uint64_t>> flagKeyValuePairs = {
      {O_DIRECTORY_FLAG_KEY, O_DIRECTORY},
      {O_TMPFILE_FLAG_KEY, (__O_TMPFILE | O_DIRECTORY)},
      {O_RDONLY_FLAG_KEY, O_RDONLY},
      {O_ACCMODE_FLAG_KEY, O_ACCMODE}};

  base::WeakPtr<PlatformInterface> platform = GetPlatform();
  // Iterate through the key-value pairs and update the BPF map
  for (const auto& flagPair : flagKeyValuePairs) {
    // Attempt to update the BPF map with the current key-value pair

    if (platform->BpfMapUpdateElementByFd(fd, &flagPair.first, &flagPair.second,
                                          BPF_ANY) != 0) {
      return absl::InternalError("Failed to update BPF map.");
    }
  }

  return absl::OkStatus();
}

absl::Status UpdateBPFMapForPathInodes(
    int bpfMapFd,
    const std::map<FilePathName, std::vector<PathInfo>>& pathsMap,
    const std::optional<std::string>& optionalUserhash) {
  // Open the root directory to use with fstatat for file information
  // retrieval
  int root_fd = open("/", O_RDONLY | O_DIRECTORY);
  if (root_fd == -1) {
    return absl::InternalError(strerror(errno));
  }

  base::WeakPtr<PlatformInterface> platform = GetPlatform();
  // Iterate over the map of file paths and their associated information
  for (const auto& [pathName, pathInfoVector] : pathsMap) {
    for (const auto& pathInfo : pathInfoVector) {
      const std::string& path =
          pathInfo.fullResolvedPath.value();  // Current path to process
      secagentd::bpf::file_monitoring_mode monitoringMode =
          pathInfo.monitoringMode;  // Monitoring mode for the path

      // Retrieve file information for the current path using fstatat
      absl::StatusOr<const struct statx> file_statx_result =
          GetFStat(root_fd, path.c_str());
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

      if (platform->BpfMapUpdateElementByFd(bpfMapFd, &bpfMapKey,
                                            &monitoringMode, 0) != 0) {
        LOG(ERROR) << "Failed to update BPF map entry for path " << path
                   << ". Inode: " << bpfMapKey.inode_id
                   << ", Device ID: " << bpfMapKey.dev_id;
        continue;  // Continue processing the next path in the map
      }
      if (pathInfo.pathCategory == FilePathCategory::USER_PATH &&
          optionalUserhash.has_value()) {
        // Add the new BPF map key to the vector
        userhash_inodes_map[optionalUserhash.value()].push_back(bpfMapKey);
      }
      // Log success message for the current path
      LOG(INFO) << "Successfully added entry to BPF map for path " << path
                << ". Inode: " << bpfMapKey.inode_id
                << ", Device ID: " << bpfMapKey.dev_id;
    }
  }

  close(root_fd);  // Close the root directory file descriptor
  return absl::OkStatus();
}

absl::Status AddDeviceIdsToBPFMap(
    int bpfMapFd,
    const std::map<FilePathName, std::vector<PathInfo>>& pathsMap) {
  // Validate BPF map file descriptor
  if (bpfMapFd < 0) {
    return absl::InvalidArgumentError("Invalid BPF map file descriptor.");
  }

  // Open the root directory for use with fstatat
  int root_fd = open("/", O_RDONLY | O_DIRECTORY);
  if (root_fd == -1) {
    return absl::InternalError(strerror(errno));
  }

  base::WeakPtr<PlatformInterface> platform = GetPlatform();
  // Iterate through each path and update the BPF map
  for (const auto& [pathName, pathInfoVector] : pathsMap) {
    for (const auto& pathInfo : pathInfoVector) {
      const std::string& path =
          pathInfo.fullResolvedPath.value();  // Current path to process

      // Retrieve file information for the current path using fstatat
      absl::StatusOr<const struct statx> file_statx_result =
          GetFStat(root_fd, path.c_str());
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

      if (platform->BpfMapUpdateElementByFd(bpfMapFd, &deviceId, &bpfSettings,
                                            BPF_ANY) != 0) {
        LOG(ERROR) << "Failed to update BPF map entry for device ID "
                   << deviceId;
        continue;  // Skip to the next path
      }

      LOG(INFO) << "Added device ID " << deviceId << " with monitoring mode "
                << static_cast<int>(pathInfo.monitoringMode)
                << " with device monitoring type "
                << static_cast<int>(pathInfo.deviceMonitoringType)
                << " to BPF map.";
    }
  }

  close(root_fd);  // Close the root directory file descriptor
  return absl::OkStatus();
}

absl::Status FilePlugin::UpdateBPFMapForPathMaps(
    const std::optional<std::string>& optionalUserhash,
    const std::map<FilePathName, std::vector<PathInfo>>& pathsMap) {
  // Retrieve file descriptor for the 'allowlisted_directory_inodes' BPF map
  absl::StatusOr<int> mapFdResult =
      bpf_skeleton_helper_->FindBpfMapByName("allowlisted_directory_inodes");
  if (!mapFdResult.ok()) {
    LOG(ERROR) << "Failed to find BPF map 'allowlisted_directory_inodes': "
               << mapFdResult.status();
    return mapFdResult.status();
  }

  int directoryInodesMapFd = mapFdResult.value();
  absl::Status status = UpdateBPFMapForPathInodes(directoryInodesMapFd,
                                                  pathsMap, optionalUserhash);
  if (!status.ok()) {
    return status;
  }

  // Retrieve file descriptor for the 'device_file_monitoring_allowlist' BPF
  // map
  mapFdResult = bpf_skeleton_helper_->FindBpfMapByName(
      "device_file_monitoring_allowlist");
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

absl::Status RemoveKeysFromBPFMap(int bpfMapFd, const std::string& userhash) {
  // Locate the entry for the given userhash in the global map
  auto it = userhash_inodes_map.find(userhash);
  if (it == userhash_inodes_map.end()) {
    // Log that no entries were found for the provided userhash
    LOG(INFO) << "No entries found for userhash " << userhash;
    return absl::OkStatus();
  }

  // Retrieve the vector of inode-device keys for the specified userhash
  const std::vector<bpf::inode_dev_map_key>& keysToRemove = it->second;
  base::WeakPtr<PlatformInterface> platform = GetPlatform();
  // Iterate over each key and attempt to remove it from the BPF map
  for (const auto& bpfMapKey : keysToRemove) {
    if (platform->BpfMapDeleteElementByFd(bpfMapFd, &bpfMapKey) != 0) {
      // Log an error if removal fails
      LOG(ERROR) << "Failed to delete BPF map entry for Inode: "
                 << bpfMapKey.inode_id << ", Device ID: " << bpfMapKey.dev_id
                 << ". Error: " << strerror(errno);
      continue;
    }
  }

  // Remove the userhash entry from the global map after processing
  userhash_inodes_map.erase(it);

  return absl::OkStatus();
}

absl::Status FilePlugin::InitializeFileBpfMaps(const std::string& userhash) {
  assert(kFilePathInfoMap.size() ==
         static_cast<int>(FilePathName::FILE_PATH_NAME_COUNT));

  const std::optional<std::string>& optionalUserhash =
      ConstructOptionalUserhash(userhash);
  // Construct the paths map based on the user hash
  std::map<FilePathName, std::vector<PathInfo>> paths_map =
      ConstructAllPathsMap(optionalUserhash);

  // Update map for flags
  absl::StatusOr<int> fd_result =
      bpf_skeleton_helper_->FindBpfMapByName("system_flags_shared");
  if (!fd_result.ok()) {
    return fd_result.status();
  }

  int fd = fd_result.value();
  absl::Status status = PopulateFlagsMap(fd);
  if (!status.ok()) {
    return status;
  }

  // TODO(b/360058671): Add hardlinks processing.

  return UpdateBPFMapForPathMaps(optionalUserhash, paths_map);
}

absl::Status FilePlugin::OnUserLogin(const std::string& userHash) {
  // Create a map to hold path information
  std::map<FilePathName, std::vector<PathInfo>> pathInfoMap;

  // Check if userHash is not empty before processing
  const std::optional<std::string>& optionalUserhash =
      ConstructOptionalUserhash(userHash);
  // Check if userHash is not empty before processing
  if (!optionalUserhash.has_value()) {
    return absl::InvalidArgumentError("User hash is empty");
  }

  // Construct and populate paths for USER_PATH category
  // Construct and populate paths for USER_PATH category
  absl::Status status = PopulatePathsMapByCategory(FilePathCategory::USER_PATH,
                                                   userHash, pathInfoMap);

  if (!status.ok()) {
    return status;
  }

  return UpdateBPFMapForPathMaps(userHash, pathInfoMap);
}

absl::Status FilePlugin::OnUserLogout(const std::string& userHash) {
  const std::optional<std::string>& optionalUserhash =
      ConstructOptionalUserhash(userHash);

  // Check if userHash is not empty before processing
  if (!optionalUserhash.has_value()) {
    return absl::InvalidArgumentError("User hash is empty");
  }

  // Remove inodes for folders for that user
  absl::StatusOr<int> mapFdResult =
      bpf_skeleton_helper_->FindBpfMapByName("allowlisted_directory_inodes");
  if (!mapFdResult.ok()) {
    return mapFdResult.status();
  }

  int directoryInodesMapFd = mapFdResult.value();

  return RemoveKeysFromBPFMap(directoryInodesMapFd, userHash);
  // TODO(princya): Remove device if not used by another directory
  // TODO(princya): Remove hard links from user directory
}

absl::Status FilePlugin::OnDeviceMount(const std::string& mount_point) {
  auto pair = MatchPathToFilePathPrefixName(mount_point);
  if (!pair.has_value()) {
    return absl::InvalidArgumentError("Mount point not matched any known path");
  }

  // Create a map to hold path information
  std::map<FilePathName, std::vector<PathInfo>> pathInfoMap;
  pair.value().second.fullResolvedPath = mount_point;
  pathInfoMap[pair.value().first].push_back(pair.value().second);

  // Update BPF maps with the constructed path information
  return UpdateBPFMapForPathMaps(std::nullopt, pathInfoMap);
}

absl::Status FilePlugin::Activate() {
  struct BpfCallbacks callbacks;
  callbacks.ring_buffer_event_callback = base::BindRepeating(
      &FilePlugin::HandleRingBufferEvent, weak_ptr_factory_.GetWeakPtr());

  absl::Status status = bpf_skeleton_helper_->LoadAndAttach(callbacks);
  if (status == absl::OkStatus()) {
    batch_sender_->Start();
  }
  return status;
}

absl::Status FilePlugin::Deactivate() {
  return bpf_skeleton_helper_->DetachAndUnload();
}

bool FilePlugin::IsActive() const {
  return bpf_skeleton_helper_->IsAttached();
}

std::string FilePlugin::GetName() const {
  return "File";
}

void FilePlugin::HandleRingBufferEvent(const bpf::cros_event& bpf_event) {
  auto atomic_event = std::make_unique<pb::FileEventAtomicVariant>();
  if (bpf_event.type != bpf::kFileEvent) {
    LOG(ERROR) << "Unexpected BPF event type.";
    return;
  }

  // const bpf::cros_file_event& fe = bpf_event.data.file_event;
  // TODO(princya): convert to proto

  device_user_->GetDeviceUserAsync(
      base::BindOnce(&FilePlugin::OnDeviceUserRetrieved,
                     weak_ptr_factory_.GetWeakPtr(), std::move(atomic_event)));
}

void FilePlugin::EnqueueBatchedEvent(
    std::unique_ptr<pb::FileEventAtomicVariant> atomic_event) {
  batch_sender_->Enqueue(std::move(atomic_event));
}

void FilePlugin::OnDeviceUserRetrieved(
    std::unique_ptr<pb::FileEventAtomicVariant> atomic_event,
    const std::string& device_user) {
  atomic_event->mutable_common()->set_device_user(device_user);
  EnqueueBatchedEvent(std::move(atomic_event));
}

std::unique_ptr<cros_xdr::reporting::FileReadEvent> FilePlugin::MakeReadEvent(
    const secagentd::bpf::cros_file_event& close_event) const {
  auto read_proto = std::make_unique<pb::FileReadEvent>();
  return read_proto;
}

std::unique_ptr<cros_xdr::reporting::FileModifyEvent>
FilePlugin::MakeModifyEvent(
    const secagentd::bpf::cros_file_event& close_event) const {
  auto modify_proto = std::make_unique<pb::FileModifyEvent>();
  return modify_proto;
}

std::unique_ptr<cros_xdr::reporting::FileModifyEvent>
FilePlugin::MakeAttributeModifyEvent(
    const secagentd::bpf::cros_file_event& attribute_modify_event) const {
  auto modify_proto = std::make_unique<pb::FileModifyEvent>();
  return modify_proto;
}

}  // namespace secagentd
