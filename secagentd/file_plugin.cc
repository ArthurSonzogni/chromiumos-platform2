// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_split.h"
#include "base/time/time.h"
#include "secagentd/bpf/bpf_types.h"
#include "secagentd/common.h"
#include "secagentd/device_user.h"
#include "secagentd/metrics_sender.h"
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

using secagentd::FilePlugin;

static constexpr size_t bytes_per_kib{1024};
static constexpr size_t bytes_per_mib{bytes_per_kib * 1024};
const char kDeviceSettingsBasePath[] = "/var/lib/devicesettings/";
static const std::map<std::string, std::string> kBlocklistBinariesPathMap = {
    {"dlp", "/usr/sbin/dlp"}, {"secagentd", "/usr/sbin/secagentd"}};

const std::vector<secagentd::FilePathName> kDeviceSettingMatchOptions{
    secagentd::FilePathName::DEVICE_SETTINGS_OWNER_KEY,
    secagentd::FilePathName::DEVICE_SETTINGS_POLICY_DIR};

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
          secagentd::FilePathCategory::SYSTEM_PATH, false, std::nullopt,
          secagentd::bpf::device_monitoring_type::MONITOR_ALL_FILES}},
        {secagentd::FilePathName::MOUNTED_ARCHIVE,
         {"/media/archive", std::nullopt,
          secagentd::bpf::file_monitoring_mode::READ_AND_READ_WRITE_BOTH,
          cros_xdr::reporting::SensitiveFileType::USER_FILE,
          secagentd::FilePathCategory::REMOVABLE_PATH, false}},
        {secagentd::FilePathName::GOOGLE_DRIVE_FS,
         {"/media/fuse/", "drivefs",
          secagentd::bpf::file_monitoring_mode::READ_AND_READ_WRITE_BOTH,
          cros_xdr::reporting::SensitiveFileType::USER_GOOGLE_DRIVE_FILE,
          secagentd::FilePathCategory::REMOVABLE_PATH, false}},
        {secagentd::FilePathName::STATEFUL_PARTITION,
         {"/home/.shadow/", "/auth_factors",
          secagentd::bpf::file_monitoring_mode::READ_WRITE_ONLY,
          cros_xdr::reporting::SensitiveFileType::USER_AUTH_FACTORS_FILE,
          secagentd::FilePathCategory::USER_PATH}},
        {secagentd::FilePathName::USB_STORAGE,
         {"/media/removable/", std::nullopt,
          secagentd::bpf::file_monitoring_mode::READ_WRITE_ONLY,
          cros_xdr::reporting::SensitiveFileType::USB_MASS_STORAGE,
          secagentd::FilePathCategory::REMOVABLE_PATH, false}},
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
         {"/run/daemon-store/session_manager/", "/policy",
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
          secagentd::FilePathName::USB_STORAGE,
          secagentd::FilePathName::GOOGLE_DRIVE_FS}}};

// Function to match a path prefix to FilePathName
std::optional<std::pair<const secagentd::FilePathName, secagentd::PathInfo>>
MatchNonUserPathToFilePathName(
    const std::string& path,
    const std::vector<secagentd::FilePathName>& matchOptions) {
  for (const auto& pathname : matchOptions) {
    auto it = kFilePathInfoMap.find(pathname);
    if (it != kFilePathInfoMap.end()) {
      std::string pathPrefix = it->second.pathPrefix;
      if (it->second.pathSuffix.has_value()) {
        pathPrefix += it->second.pathSuffix.value();
      }
      if (path.find(pathPrefix) == 0) {
        return *it;
      }
    }
  }
  return std::nullopt;
}

const std::optional<std::string> ConstructOptionalUserhash(
    const std::string& userhash) {
  if (userhash.empty() || userhash == secagentd::device_user::kUnknown ||
      userhash == secagentd::device_user::kGuest) {
    return std::nullopt;
  }
  return userhash;
}

static uint64_t UserspaceToKernelDeviceId(const struct statx& fileStatx) {
  // Combine the major and minor numbers to form the kernel-space device ID
  return ((fileStatx.stx_dev_major << 20) | fileStatx.stx_dev_minor);
}

static uint64_t UserspaceToKernelDeviceId(uint64_t dev_t) {
  // This function converts a user-space device ID (64 bits) to a kernel-space
  // device ID (32 bits). In the kernel, the device ID is structured with the
  // major number occupying the upper 20 bits and the minor number occupying
  // the lower 12 bits. By shifting the major number left by 20 bits, we
  // combine the major and minor numbers into a single 32-bit identifier,
  // adhering to the kernel's requirements for device identification.
  return ((major(dev_t) << 20) | minor(dev_t));
}

static uint64_t KernelToUserspaceDeviceId(uint64_t kernel_dev) {
  // Extract major and minor numbers from the kernel-space device ID
  uint32_t major = (kernel_dev >> 20) & 0xfff;  // Major number (12 bits)
  uint32_t minor = kernel_dev & 0xfffff;        // Minor number (20 bits)

  return makedev(major, minor);
}

bool ReadLine(base::File* file,
              std::string* line,
              std::string* remaining_line) {
  if (!file || !line || !remaining_line) {
    return false;  // Invalid arguments
  }

  line->clear();
  const size_t kBufferSize = 1024;
  std::string buffer(kBufferSize, '\0');

  // Handle any leftover data from the previous read
  if (!remaining_line->empty()) {
    size_t newline_pos = remaining_line->find('\n');
    if (newline_pos != std::string::npos) {
      *line = remaining_line->substr(0, newline_pos);
      *remaining_line = remaining_line->substr(newline_pos + 1);
      return true;
    }
    // If no newline, continue appending
    *line = *remaining_line;
    remaining_line->clear();
  }

  // Read new data
  while (true) {
    int bytes_read = file->ReadAtCurrentPos(&buffer[0], kBufferSize);
    if (bytes_read < 0) {
      return false;
    }
    // Check if there is any remaining data to process
    if (bytes_read == 0) {
      // End of file
      if (!line->empty()) {
        return true;
      } else if (!remaining_line->empty()) {
        *line = *remaining_line;
        remaining_line->clear();
        return true;
      }
      return false;
    }

    std::string buffer_data = buffer.substr(0, bytes_read);
    size_t start = 0;
    size_t newline_pos;
    if ((newline_pos = buffer_data.find('\n', start)) != std::string::npos) {
      *line += buffer_data.substr(start, newline_pos - start);
      *remaining_line = buffer_data.substr(newline_pos + 1);
      return true;
    }

    // No newline found, accumulate buffer content
    *line += buffer_data;
  }
}

bool IsDeviceStillMounted(uint64_t kernel_dev) {
  uint64_t user_dev = KernelToUserspaceDeviceId(kernel_dev);
  uint32_t dev_major = major(user_dev), dev_minor = minor(user_dev);
  base::File mountinfo(base::FilePath("/proc/self/mountinfo"),
                       base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!mountinfo.IsValid()) {
    LOG(ERROR) << "Failed to open /proc/self/mountinfo";
    return false;
  }

  std::string line;
  std::string remaining_line;

  while (ReadLine(&mountinfo, &line, &remaining_line)) {
    std::vector<std::string> tokens = base::SplitString(
        line, " ", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);

    // The 3rd token (index 2) in /proc/self/mountinfo represents the
    // major:minor device numbers
    if (tokens.size() > 2) {
      // Extract the device major:minor
      unsigned int major, minor;
      if (sscanf(tokens[2].c_str(), "%u:%u", &major, &minor) == 2) {
        if (major == dev_major && minor == dev_minor) {
          return true;  // Device is still mounted somewhere
        }
      }
    }
  }

  return false;  // Device is not mounted anywhere
}

// Inspired by cros-disks/archive_manager.cc
// TODO(b:363053701): find a better home for this code.
bool IsExternalMedia(const base::FilePath& source_path) {
  std::vector<std::string> parts = source_path.GetComponents();

  if (parts.size() < 2 || parts[0] != "/") {
    return false;
  }

  if (parts[1] == "media") {
    return parts.size() > 4 && (parts[2] == "archive" || parts[2] == "fuse" ||
                                parts[2] == "removable");
  }

  if (parts[1] == "run") {
    return parts.size() > 8 && parts[2] == "arc" && parts[3] == "sdcard" &&
           parts[4] == "write" && parts[5] == "emulated" && parts[6] == "0";
  }

  return false;
}

absl::StatusOr<FilePlugin::HashComputeResult> AsyncHashCompute(
    FilePlugin::HashComputeInput input,
    scoped_refptr<secagentd::ImageCacheInterface> image_cache) {
  // Ready to start calling image_cache with metadata.

  auto& meta = input.meta_data;
  secagentd::ImageCacheInterface::ImageCacheKeyType image_key;
  image_key.mtime.tv_nsec = meta.mtime.tv_nsec;
  image_key.mtime.tv_sec = meta.mtime.tv_sec;

  image_key.ctime.tv_nsec = meta.ctime.tv_nsec;
  image_key.ctime.tv_sec = meta.ctime.tv_sec;

  auto& inode_key = input.key.inode_key;
  image_key.inode = inode_key.inode;
  image_key.inode_device_id = inode_key.device_id;
  bool force_full_sha256 = false;
  base::FilePath file_name(meta.file_name);
  // If the file resides on an exec filesystem or resides in a location where
  // external media is mounted then force the full SHA.
  if (!meta.is_noexec || IsExternalMedia(file_name)) {
    force_full_sha256 = true;
  }
  auto image_result = image_cache->InclusiveGetImage(
      image_key, force_full_sha256, meta.pid_for_setns,
      base::FilePath(file_name));
  if (!image_result.ok()) {
    return absl::InternalError("Failed to hash file");
  }
  FilePlugin::HashComputeResult hash_result{
      .key = input.key,
      .generation = input.generation,
      .hash_result = image_result.value()};
  return hash_result;
}

absl::StatusOr<cros_xdr::reporting::FileImage*> GetMutableImage(
    cros_xdr::reporting::FileEventAtomicVariant& event) {
  switch (event.variant_type_case()) {
    case cros_xdr::reporting::FileEventAtomicVariant::kSensitiveRead:
      return event.mutable_sensitive_read()
          ->mutable_file_read()
          ->mutable_image();
      break;
    case cros_xdr::reporting::FileEventAtomicVariant::kSensitiveModify:
      return event.mutable_sensitive_modify()
          ->mutable_file_modify()
          ->mutable_image_after();
      break;
    case cros_xdr::reporting::FileEventAtomicVariant::VARIANT_TYPE_NOT_SET:
      return absl::InternalError("Event has no variant type");
      break;
  }
}

absl::StatusOr<FilePlugin::InodeKey> GenerateInodeKey(
    cros_xdr::reporting::FileEventAtomicVariant& event) {
  auto result = GetMutableImage(event);
  if (!result.ok()) {
    return result.status();
  }
  return FilePlugin::InodeKey{.inode = result.value()->inode(),
                              .device_id = result.value()->inode_device_id()};
}

absl::StatusOr<FilePlugin::FileEventKey> GenerateFileEventKey(
    cros_xdr::reporting::FileEventAtomicVariant& atomic_event) {
  FilePlugin::FileEventKey key;
  auto result = GenerateInodeKey(atomic_event);
  if (!result.ok()) {
    return result.status();
  }
  key.inode_key = result.value();
  key.event_type = atomic_event.variant_type_case();
  if (atomic_event.has_sensitive_modify()) {
    key.process_uuid = atomic_event.sensitive_modify().process().process_uuid();
  } else if (atomic_event.has_sensitive_read()) {
    key.process_uuid = atomic_event.sensitive_read().process().process_uuid();
  }
  // No need to handle no variant type, GenerateInodeKey returns a status
  // error in this case.
  return key;
}
}  // namespace

namespace secagentd {
namespace pb = cros_xdr::reporting;

FilePlugin::FilePlugin(
    scoped_refptr<BpfSkeletonFactoryInterface> bpf_skeleton_factory,
    scoped_refptr<MessageSenderInterface> message_sender,
    scoped_refptr<ProcessCacheInterface> process_cache,
    scoped_refptr<PoliciesFeaturesBrokerInterface> policies_features_broker,
    scoped_refptr<DeviceUserInterface> device_user,
    uint32_t batch_interval_s)
    : FilePlugin(bpf_skeleton_factory,
                 message_sender,
                 process_cache,
                 base::MakeRefCounted<ImageCache>(),
                 policies_features_broker,
                 device_user,
                 batch_interval_s,
                 std::max((batch_interval_s / 10), 1u)) {}

// Constructor for testing only, allows for image cache injection.
FilePlugin::FilePlugin(
    scoped_refptr<BpfSkeletonFactoryInterface> bpf_skeleton_factory,
    scoped_refptr<MessageSenderInterface> message_sender,
    scoped_refptr<ProcessCacheInterface> process_cache,
    scoped_refptr<ImageCacheInterface> image_cache,
    scoped_refptr<PoliciesFeaturesBrokerInterface> policies_features_broker,
    scoped_refptr<DeviceUserInterface> device_user,
    uint32_t batch_interval_s,
    uint32_t async_timeout_s)
    : weak_ptr_factory_(this),
      process_cache_(process_cache),
      image_cache_(image_cache),
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
              bpf_skeleton_factory, batch_interval_s)),
      batch_interval_s_(batch_interval_s),
      async_timeout_s_(async_timeout_s) {
  CHECK(message_sender != nullptr);
  CHECK(process_cache != nullptr);
  CHECK(bpf_skeleton_factory);
  CHECK(async_timeout_s < (batch_interval_s / 2));
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
  base::WeakPtr<PlatformInterface> platform = GetPlatform();
  // Use Platform class to check if the base directory exists and is a directory
  if (!platform->FilePathExists(baseDir) ||
      !platform->IsFilePathDirectory(baseDir)) {
    LOG(ERROR) << "The directory " << baseDir
               << " does not exist or is not a directory.";
    return;
  }

  // Iterate over all entries in the base directory using Platform's
  // DirectoryIterator
  for (const auto& entry : platform->FileSystemDirectoryIterator(baseDir)) {
    // Check if the entry is a directory or a regular file
    if ((entry.is_directory() && processSubDirectories) ||
        (entry.is_regular_file() && processFiles)) {
      // Apply the callback function to the directory path
      callback.Run(entry.path().string());
    }
  }
}

std::unique_ptr<FilePlugin::InodeMonitoringSettingsMap>
TraverseDirectoryHardlink(
    std::unique_ptr<FilePlugin::InodeMonitoringSettingsMap> hard_link_map,
    const base::FilePath& dir_path,
    const PathInfo& pathInfo,
    std::unordered_set<ino_t>& visited_inodes) {
  // FileEnumerator for traversing directories
  base::FileEnumerator enumerator(
      dir_path, false,
      base::FileEnumerator::DIRECTORIES | base::FileEnumerator::FILES);

  for (base::FilePath current = enumerator.Next(); !current.empty();
       current = enumerator.Next()) {
    base::FileEnumerator::FileInfo file_info = enumerator.GetInfo();

    if (file_info.GetName().value() == "." ||
        file_info.GetName().value() == ".") {
      continue;  // Skip the current and parent directories
    }

    // Check if we've already encountered this inode through a hard link
    if (visited_inodes.find(file_info.stat().st_ino) != visited_inodes.end()) {
      continue;  // Skip files or directories we've already processed
    }

    // Add the inode to the set to mark it as processed
    visited_inodes.insert(file_info.stat().st_ino);

    // Check if it's a regular file with multiple hard links
    if (S_ISREG(file_info.stat().st_mode) && file_info.stat().st_nlink > 1) {
      // Create key for BPF map update
      auto key = std::make_unique<secagentd::bpf::inode_dev_map_key>(
          secagentd::bpf::inode_dev_map_key{
              .inode_id = file_info.stat().st_ino,
              .dev_id = UserspaceToKernelDeviceId(file_info.stat().st_dev)});

      auto monitoringSettings =
          std::make_unique<secagentd::bpf::file_monitoring_settings>(
              (uint8_t)pathInfo.fileType, pathInfo.monitoringMode);
      hard_link_map->insert_or_assign(std::move(key),
                                      std::move(monitoringSettings));
    } else if (file_info.IsDirectory()) {
      // Recursively call for directories
      hard_link_map = TraverseDirectoryHardlink(
          std::move(hard_link_map), current, pathInfo, visited_inodes);
    }
  }

  return hard_link_map;
}

std::unique_ptr<FilePlugin::InodeMonitoringSettingsMap> UpdateHardLinksBPFMap(
    const std::map<FilePathName, std::vector<PathInfo>>& pathsMap) {
  std::unique_ptr<FilePlugin::InodeMonitoringSettingsMap> hard_link_map =
      std::make_unique<FilePlugin::InodeMonitoringSettingsMap>();
  for (const auto& [_, pathInfos] : pathsMap) {
    for (const auto& pathInfo : pathInfos) {
      if (!pathInfo.monitorHardLink || !pathInfo.fullResolvedPath.has_value()) {
        continue;  // Skip if hard link monitoring is not enabled or path is not
                   // resolved
      }
      base::FilePath dir_path(pathInfo.fullResolvedPath.value());
      std::unordered_set<ino_t> visited_inodes;
      // Traverse the directory and update the BPF map
      hard_link_map = TraverseDirectoryHardlink(
          std::move(hard_link_map), dir_path, pathInfo, visited_inodes);
    }
  }

  return hard_link_map;
}

void FilePlugin::ProcessHardLinkTaskResult(
    int fd,
    std::unique_ptr<FilePlugin::InodeMonitoringSettingsMap> hard_link_map) {
  // Iterate over the entries in the map
  for (const auto& entry : *hard_link_map) {
    const auto& key = entry.first;                  // The inode key
    const auto& monitoringSettings = entry.second;  // Monitoring settings
    // Update BPF map entry for each key-value pair
    if (bpf_map_update_elem(fd, key.get(), monitoringSettings.get(), 0) != 0) {
      LOG(ERROR) << "Failed to update HardLink BPF map for inode "
                 << key->inode_id << " device id " << key->dev_id;
    }
  }
}

absl::Status PopulatePathsMapByCategory(
    FilePathCategory category,
    const std::optional<std::string>& optionalUserHash,
    std::map<FilePathName, std::vector<PathInfo>>* pathInfoMap) {
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
                if (pathInfo->pathSuffix.has_value() &&
                    !pathInfo->pathSuffix->empty()) {
                  if (!path.starts_with(pathInfo->pathPrefix +
                                        pathInfo->pathSuffix.value())) {
                    return;
                  }
                }
                pathInfo->fullResolvedPath = path;
                (*pathInfoMap)[pathName].push_back(*pathInfo);
              },
              base::Unretained(pathInfoMap), base::Unretained(&pathInfo),
              pathName),
          true, false);
    } else if (pathName == FilePathName::DEVICE_SETTINGS_POLICY_DIR) {
      pathInfo.fullResolvedPath = kDeviceSettingsBasePath;
      (*pathInfoMap)[pathName].push_back(pathInfo);
    } else if (category == FilePathCategory::USER_PATH) {
      pathInfo.fullResolvedPath = pathInfo.pathPrefix +
                                  optionalUserHash.value() +
                                  pathInfo.pathSuffix.value();
      (*pathInfoMap)[pathName].push_back(pathInfo);
    } else {
      pathInfo.fullResolvedPath = pathInfo.pathPrefix;
      (*pathInfoMap)[pathName].push_back(pathInfo);
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
        FilePathCategory::USER_PATH, optionalUserHash, &pathInfoMap);
    if (!status.ok()) {
      LOG(ERROR) << "Failed to populate paths for USER_PATH category: "
                 << status;
    }
  }

  // Populate paths for SYSTEM_PATH and REMOVABLE_PATH categories without
  // userHash
  absl::Status status = PopulatePathsMapByCategory(
      FilePathCategory::SYSTEM_PATH, std::nullopt, &pathInfoMap);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to populate paths for SYSTEM_PATH category: "
               << status;
  }
  status = PopulatePathsMapByCategory(FilePathCategory::REMOVABLE_PATH,
                                      std::nullopt, &pathInfoMap);
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

absl::Status FilePlugin::PopulateProcessBlocklistMap() {
  // Retrieve the BPF map file descriptor for the blocklisted binary inode map
  auto fd_result =
      bpf_skeleton_helper_->FindBpfMapByName("blocklisted_binary_inode_map");
  if (!fd_result.ok()) {
    return fd_result.status();
  }
  auto fd = fd_result.value();

  // Weak pointer to platform interface for updating BPF map
  base::WeakPtr<PlatformInterface> platform = GetPlatform();

  int root_fd = platform->OpenDirectory("/");
  if (root_fd == -1) {
    return absl::InternalError(strerror(errno));
  }

  // Iterate over the blocklisted process map, which contains the binary paths
  for (const auto& [_, binary_path] : kBlocklistBinariesPathMap) {
    // Retrieve file information for the current path using fstatat
    absl::StatusOr<const struct statx> file_statx_result =
        GetFStat(root_fd, binary_path.c_str());
    if (!file_statx_result.ok()) {
      // We always expect to find dlp/secagentd binary in stored location
      NOTREACHED() << "FilePlugin::PopulateProcessBlocklistMap Failed to "
                      "retrieve file stat for "
                   << binary_path << ": " << file_statx_result.status();
    }
    const struct statx& fileStatx = file_statx_result.value();

    // Prepare the BPF map key with inode ID and device ID
    struct bpf::inode_dev_map_key key = {
        .inode_id = fileStatx.stx_ino,
        .dev_id = UserspaceToKernelDeviceId(fileStatx)};

    // Update the BPF map with inode_device_key as the key, and dummy value (1)
    // as the value
    uint32_t dummy_value = 1;
    if (platform->BpfMapUpdateElementByFd(fd, &key, &dummy_value, BPF_ANY) !=
        0) {
      return absl::InternalError(
          absl::StrFormat("Failed to update BPF map with inode %lu and "
                          "device %u for binary: %s",
                          key.inode_id, key.dev_id, binary_path));
    }
  }
  // TODO(princya): Handle close file descriptor more gracefully
  platform->CloseDirectory(
      root_fd);  // Close the root directory file descriptor

  return absl::OkStatus();
}

absl::Status FilePlugin::UpdateBPFMapForPathInodes(
    int bpfMapFd,
    const std::map<FilePathName, std::vector<PathInfo>>& pathsMap,
    const std::optional<std::string>& optionalUserhash) {
  base::WeakPtr<PlatformInterface> platform = GetPlatform();
  // Open the root directory to use with fstatat for file information
  // retrieval
  int root_fd = platform->OpenDirectory("/");
  if (root_fd == -1) {
    return absl::InternalError(strerror(errno));
  }

  // Iterate over the map of file paths and their associated information
  for (const auto& [pathName, pathInfoVector] : pathsMap) {
    for (const auto& pathInfo : pathInfoVector) {
      const std::string& path =
          pathInfo.fullResolvedPath.value();  // Current path to process
      secagentd::bpf::file_monitoring_settings monitoringSettings{
          (uint8_t)pathInfo.fileType, pathInfo.monitoringMode};

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
                                            &monitoringSettings, 0) != 0) {
        LOG(ERROR) << "Failed to update BPF map entry for path " << path
                   << ". Inode: " << bpfMapKey.inode_id
                   << ", Device ID: " << bpfMapKey.dev_id;
        continue;  // Continue processing the next path in the map
      }
      if (pathInfo.pathCategory == FilePathCategory::USER_PATH &&
          optionalUserhash.has_value()) {
        // Add the new BPF map key to the vector
        userhash_inodes_map_[optionalUserhash.value()].push_back(bpfMapKey);
      }
      // Log success message for the current path
      LOG(INFO) << "Successfully added entry to BPF map for path " << path
                << ". Inode: " << bpfMapKey.inode_id
                << ", Device ID: " << bpfMapKey.dev_id;
    }
  }
  platform->CloseDirectory(
      root_fd);  // Close the root directory file descriptor
  return absl::OkStatus();
}

absl::Status AddDeviceIdsToBPFMap(
    int bpfMapFd,
    const std::map<FilePathName, std::vector<PathInfo>>& pathsMap) {
  // Validate BPF map file descriptor
  if (bpfMapFd < 0) {
    return absl::InvalidArgumentError("Invalid BPF map file descriptor.");
  }

  base::WeakPtr<PlatformInterface> platform = GetPlatform();
  // Open the root directory to use with fstatat for file information
  // retrieval
  int root_fd = platform->OpenDirectory("/");
  if (root_fd == -1) {
    return absl::InternalError(strerror(errno));
  }

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
      uint64_t deviceId = UserspaceToKernelDeviceId(fileStatx);

      struct bpf::device_file_monitoring_settings bpfSettings = {
          .device_monitoring_type = pathInfo.deviceMonitoringType,
          .file_monitoring_mode = pathInfo.monitoringMode,
          .sensitive_file_type =
              (uint8_t)pathInfo.fileType,  // Respected only when
                                           // MONITOR_ALL_FILES is selected
      };

      // Choose Read-write over write only for same device, if same device used
      // for multiple filepaths
      struct bpf::device_file_monitoring_settings bpfSettingsOld;
      if (platform->BpfMapLookupElementByFd(bpfMapFd, &deviceId,
                                            &bpfSettingsOld) == 0) {
        if (bpfSettingsOld.file_monitoring_mode ==
            bpf::READ_AND_READ_WRITE_BOTH) {
          bpfSettings.file_monitoring_mode = bpf::READ_AND_READ_WRITE_BOTH;
        }

        if (bpfSettingsOld.device_monitoring_type == bpf::MONITOR_ALL_FILES) {
          bpfSettings.device_monitoring_type = bpf::MONITOR_ALL_FILES;
        }
      }

      // Update BPF map with the device ID and settings

      if (platform->BpfMapUpdateElementByFd(bpfMapFd, &deviceId, &bpfSettings,
                                            BPF_ANY) != 0) {
        LOG(ERROR) << "Failed to update BPF map entry for device ID "
                   << deviceId;
        continue;  // Skip to the next path
      }

      LOG(INFO) << "Added device ID " << deviceId << " with monitoring mode "
                << static_cast<int>(bpfSettings.file_monitoring_mode)
                << " with device monitoring type "
                << static_cast<int>(bpfSettings.device_monitoring_type)
                << " to BPF map.";
    }
  }

  platform->CloseDirectory(
      root_fd);  // Close the root directory file descriptor
  return absl::OkStatus();
}

absl::Status FilePlugin::UpdateBPFMapForPathMaps(
    const std::optional<std::string>& optionalUserhash,
    const std::map<FilePathName, std::vector<PathInfo>>& pathsMap) {
  // Retrieve file descriptor for the 'predefined_allowed_inodes' BPF map
  absl::StatusOr<int> mapFdResult =
      bpf_skeleton_helper_->FindBpfMapByName("predefined_allowed_inodes");
  if (!mapFdResult.ok()) {
    LOG(ERROR) << "Failed to find BPF map 'predefined_allowed_inodes': "
               << mapFdResult.status();
    return mapFdResult.status();
  }

  int directoryInodesMapFd = mapFdResult.value();
  absl::Status status = UpdateBPFMapForPathInodes(directoryInodesMapFd,
                                                  pathsMap, optionalUserhash);
  if (!status.ok()) {
    return status;
  }

  // Retrieve file descriptor for the 'device_monitoring_allowlist' BPF map
  mapFdResult =
      bpf_skeleton_helper_->FindBpfMapByName("device_monitoring_allowlist");
  if (!mapFdResult.ok()) {
    return mapFdResult.status();
  }

  int deviceMonitoringMapFd = mapFdResult.value();
  status = AddDeviceIdsToBPFMap(deviceMonitoringMapFd, pathsMap);
  if (!status.ok()) {
    return status;
  }

  mapFdResult =
      bpf_skeleton_helper_->FindBpfMapByName("allowlisted_hardlink_inodes");

  if (!mapFdResult.ok()) {
    return mapFdResult.status();
  }

  async_io_task_tracker_.PostTaskAndReplyWithResult(
      async_io_task_.get(), FROM_HERE,
      base::BindOnce(&UpdateHardLinksBPFMap, pathsMap),
      base::BindOnce(&FilePlugin::ProcessHardLinkTaskResult,
                     weak_ptr_factory_.GetWeakPtr(), mapFdResult.value()));

  return absl::OkStatus();
}

absl::Status FilePlugin::RemoveKeysFromBPFMapOnUnmount(int bpfMapFd,
                                                       uint64_t dev) {
  base::WeakPtr<PlatformInterface> platform = GetPlatform();
  if (platform->BpfMapDeleteElementByFd(bpfMapFd, &dev) != 0) {
    return absl::InternalError(
        absl::StrCat("Failed to delete BPF map entry for Device ID: ", dev,
                     ". Error: ", strerror(errno)));
  }
  return absl::OkStatus();
}
absl::Status FilePlugin::RemoveKeysFromBPFMapOnLogout(
    int bpfMapFd, const std::string& userhash) {
  // Locate the entry for the given userhash in the global map
  auto it = userhash_inodes_map_.find(userhash);
  if (it == userhash_inodes_map_.end()) {
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
  userhash_inodes_map_.erase(it);

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

  status = PopulateProcessBlocklistMap();
  if (!status.ok()) {
    LOG(ERROR) << "Failed to add blocklisted process inodes "
               << status.message();
  }

  // TODO(b/360058671): Add hardlinks processing.

  return UpdateBPFMapForPathMaps(optionalUserhash, paths_map);
}

void FilePlugin::OnUserLogin(const std::string& device_user,
                             const std::string& userHash) {
  // Create a map to hold path information
  std::map<FilePathName, std::vector<PathInfo>> pathInfoMap;

  // Check if userHash is not empty before processing
  const std::optional<std::string>& optionalUserhash =
      ConstructOptionalUserhash(userHash);
  // Check if userHash is not empty before processing
  if (!optionalUserhash.has_value()) {
    LOG(ERROR) << "FilePlugin::OnUserLogin: " << "User hash is empty";
    return;
  }

  // Construct and populate paths for USER_PATH category
  absl::Status status = PopulatePathsMapByCategory(FilePathCategory::USER_PATH,
                                                   userHash, &pathInfoMap);

  if (!status.ok()) {
    LOG(ERROR) << "FilePlugin::OnUserLogin: Error Populating paths"
               << status.message();
  }

  status = UpdateBPFMapForPathMaps(userHash, pathInfoMap);
  if (!status.ok()) {
    LOG(ERROR) << "FilePlugin::OnUserLogin: Error Populating BPF Maps"
               << status.message();
  }
}

void FilePlugin::OnUserLogout(const std::string& userHash) {
  const std::optional<std::string>& optionalUserhash =
      ConstructOptionalUserhash(userHash);

  // Check if userHash is not empty before processing
  if (!optionalUserhash.has_value()) {
    return;
  }

  // Remove inodes for folders for that user
  absl::StatusOr<int> mapFdResult =
      bpf_skeleton_helper_->FindBpfMapByName("predefined_allowed_inodes");
  if (!mapFdResult.ok()) {
    LOG(ERROR) << "Failed to find predefined_allowed_inodes bpf map "
               << mapFdResult.status().message();
    return;
  }

  int directoryInodesMapFd = mapFdResult.value();

  absl::Status status =
      RemoveKeysFromBPFMapOnLogout(directoryInodesMapFd, userHash);

  if (!status.ok()) {
    LOG(WARNING) << "Failed to remove File monitoring paths from bpf_map. "
                 << status.message();
  }

  // TODO(princya): Remove device if not used by another directory
  // TODO(princya): Remove hard links from user directory
}

void FilePlugin::OnMountEvent(const secagentd::bpf::mount_data& data) {
  std::string destination_path = data.dest_device_path;

  auto pair = MatchNonUserPathToFilePathName(
      destination_path,
      kFilePathNamesByCategory.at(FilePathCategory::REMOVABLE_PATH));
  if (!pair.has_value()) {
    return;
  }

  // Create a map to hold path information
  std::map<FilePathName, std::vector<PathInfo>> pathInfoMap;
  pair.value().second.fullResolvedPath = destination_path;
  pathInfoMap[pair.value().first].push_back(pair.value().second);

  // Update BPF maps with the constructed path information
  auto status = UpdateBPFMapForPathMaps(std::nullopt, pathInfoMap);
  if (!status.ok()) {
    // TODO(b/362014987): Add error metrics.
    LOG(ERROR) << "Failed to add the new mount path to monitoring";
  }
}

void FilePlugin::OnUnmountEvent(
    const secagentd::bpf::umount_event& umount_event) {
  auto pair = MatchNonUserPathToFilePathName(
      umount_event.dest_device_path,
      kFilePathNamesByCategory.at(FilePathCategory::REMOVABLE_PATH));
  if (!pair.has_value()) {
    LOG(INFO) << "Mount point not matched any known path. Path: "
              << umount_event.dest_device_path;
    return;
  }

  if (umount_event.ref_count != 0 && umount_event.active_counter != 0 &&
      IsDeviceStillMounted(umount_event.device_id)) {
    return;
  }

  // Remove inodes for folders for that user
  absl::StatusOr<int> mapFdResult =
      bpf_skeleton_helper_->FindBpfMapByName("device_monitoring_allowlist");
  if (!mapFdResult.ok()) {
    LOG(ERROR) << "Unable to find bpf map device_monitoring_allowlist by name: "
               << mapFdResult.status().message();
    return;
  }
  int deviceMapFd = mapFdResult.value();

  absl::Status status =
      RemoveKeysFromBPFMapOnUnmount(deviceMapFd, umount_event.device_id);
  if (!status.ok()) {
    LOG(ERROR) << status.message();
  }
}

void FilePlugin::OnSessionStateChange(const std::string& state) {
  std::string sanitized_username;
  if (state == kInit) {
    device_user_->GetDeviceUserAsync(base::BindOnce(
        &FilePlugin::OnUserLogin, weak_ptr_factory_.GetWeakPtr()));
  } else if (state == kStarted) {
    OnUserLogin("", device_user_->GetSanitizedUsername());
  } else if (state == kStopping || state == kStopped) {
    OnUserLogout(device_user_->GetSanitizedUsername());
  }
}

absl::Status FilePlugin::Activate() {
  struct BpfCallbacks callbacks;
  callbacks.ring_buffer_event_callback = base::BindRepeating(
      &FilePlugin::HandleRingBufferEvent, weak_ptr_factory_.GetWeakPtr());

  absl::Status status = bpf_skeleton_helper_->LoadAndAttach(callbacks);
  if (status != absl::OkStatus()) {
    return status;
  }
  stage_async_task_timer_.Start(
      FROM_HERE, base::Seconds(std::max(batch_interval_s_, 1u)),
      base::BindRepeating(&FilePlugin::StageEventsForAsyncProcessing,
                          weak_ptr_factory_.GetWeakPtr()));

  device_user_->RegisterSessionChangeListener(base::BindRepeating(
      &FilePlugin::OnSessionStateChange, weak_ptr_factory_.GetWeakPtr()));

  std::string username = device_user_->GetSanitizedUsername();
  if (InitializeFileBpfMaps(username) != absl::OkStatus()) {
    return absl::InternalError("InitializeFileBpfMaps failed");
  }
  return status;
}

absl::Status FilePlugin::Deactivate() {
  OnAsyncHashComputeTimeout();
  stage_async_task_timer_.Stop();
  return bpf_skeleton_helper_->DetachAndUnload();
}

bool FilePlugin::IsActive() const {
  return bpf_skeleton_helper_->IsAttached();
}

std::string FilePlugin::GetName() const {
  return "File";
}

void FilePlugin::HandleRingBufferEvent(const bpf::cros_event& bpf_event) {
  if (bpf_event.type != bpf::kFileEvent) {
    LOG(ERROR) << "Unexpected BPF event type.";
    return;
  }

  auto atomic_event = std::make_unique<pb::FileEventAtomicVariant>();
  atomic_event->mutable_common()->set_create_timestamp_us(
      base::Time::Now().InMillisecondsSinceUnixEpoch() *
      base::Time::kMicrosecondsPerMillisecond);

  const bpf::cros_file_event& fe = bpf_event.data.file_event;
  if (fe.type == bpf::kFileCloseEvent) {
    if (fe.mod_type == secagentd::bpf::FMOD_READ_ONLY_OPEN) {
      atomic_event->set_allocated_sensitive_read(
          MakeFileReadEvent(fe.data.file_detailed_event).release());
    } else if (fe.mod_type == secagentd::bpf::FMOD_READ_WRITE_OPEN) {
      atomic_event->set_allocated_sensitive_modify(
          MakeFileModifyEvent(fe.data.file_detailed_event).release());
    }
  } else if (fe.type == bpf::kFileAttributeModifyEvent) {
    atomic_event->set_allocated_sensitive_modify(
        MakeFileAttributeModifyEvent(fe.data.file_detailed_event).release());
  } else if (fe.type == bpf::kFileMountEvent) {
    if (fe.mod_type == bpf::FMOD_MOUNT) {
      OnMountEvent(fe.data.mount_event);
      return;
    } else {
      OnUnmountEvent(fe.data.umount_event);
      return;
    }
  }

  std::unique_ptr<FileEventValue> fev = std::make_unique<FileEventValue>();
  auto& image_info = fe.data.file_detailed_event.image_info;
  auto& file_attr = image_info.after_attr;
  fev->meta_data.is_noexec = image_info.file_system_noexec;
  fev->meta_data.pid_for_setns = image_info.pid_for_setns;
  fev->meta_data.mtime.tv_sec = file_attr.mtime.tv_sec;
  fev->meta_data.mtime.tv_nsec = file_attr.mtime.tv_nsec;
  fev->meta_data.ctime.tv_sec = file_attr.ctime.tv_sec;
  fev->meta_data.ctime.tv_nsec = file_attr.ctime.tv_nsec;
  fev->event = std::move(atomic_event);
  auto result = GetMutableImage(*fev->event);
  if (!result.ok()) {
    return;
  }
  fev->meta_data.file_name = result.value()->pathname();

  device_user_->GetDeviceUserAsync(
      base::BindOnce(&FilePlugin::OnDeviceUserRetrieved,
                     weak_ptr_factory_.GetWeakPtr(), std::move(fev)));
}

void FilePlugin::CollectEvent(std::unique_ptr<FileEventValue> fev) {
  auto& event = *fev->event;

  auto result = GenerateFileEventKey(event);
  if (!result.ok()) {
    LOG(ERROR) << result.status();
    return;
  }
  const FileEventKey& key = result.value();

  FileEventMap& event_map = current_events_->event_map;
  OrderedEvents& ordered_events = *(current_events_->ordered_events);
  auto it = event_map.find(key);
  if (it == event_map.end()) {
    event_map[key] = fev->GetWeakPtr();
    ordered_events.push_back(std::move(fev));
    return;
  }
  if (ordered_events.empty()) {
    LOG(ERROR) << "Unexpected empty ordered events";
    return;
  }

  if (event.has_sensitive_modify() &&
      it->second->event->has_sensitive_modify()) {
    auto received_modify =
        event.mutable_sensitive_modify()->mutable_file_modify();
    auto stored_modify =
        it->second->event->mutable_sensitive_modify()->mutable_file_modify();
    // Writes and change attributes unconditionally coalesce together.
    stored_modify->set_allocated_image_after(
        received_modify->release_image_after());
    // Also coalesce metadata.
    it->second->meta_data = fev->meta_data;

    const auto& stored_modify_type = stored_modify->modify_type();
    // If the existing modify type is write or modify and the incoming
    // modify type differs then promote the stored type to write-and-modify.
    if (stored_modify_type !=
            pb::FileModify_ModifyType_WRITE_AND_MODIFY_ATTRIBUTE &&
        stored_modify_type != received_modify->modify_type()) {
      // If the stored type is unknown then promote it to the incoming
      // modify type.
      if (stored_modify_type == pb::FileModify_ModifyType_MODIFY_TYPE_UNKNOWN) {
        stored_modify->set_modify_type(received_modify->modify_type());
      } else {
        stored_modify->set_modify_type(
            pb::FileModify_ModifyType::
                FileModify_ModifyType_WRITE_AND_MODIFY_ATTRIBUTE);
      }
    }
    // Attributes before will be the earliest attributes.
    // For example if there are multiple modify attributes then the
    // before attributes will be the attributes before the series of modify
    // attributes occurred and the image_after will contain the attributes
    // after all the modify attributes have finished.
    if (!stored_modify->has_attributes_before() &&
        received_modify->has_attributes_before()) {
      stored_modify->set_allocated_attributes_before(
          received_modify->release_attributes_before());
    }
  } else if (event.has_sensitive_read() &&
             it->second->event->has_sensitive_read()) {
    auto received_read = event.mutable_sensitive_read()->mutable_file_read();
    auto stored_read =
        it->second->event->mutable_sensitive_read()->mutable_file_read();
    stored_read->set_allocated_image(received_read->release_image());
    it->second->meta_data = fev->meta_data;
  } else {
    LOG(WARNING) << "Unexpected file event received with no attached"
                 << " variant. Dropping event.";
  }
}
void FilePlugin::OnAsyncHashComputeTimeout() {
  // Cancel all tasks that have not yet started running.
  async_io_task_tracker_.TryCancelAll();
  // TODO(b:362014987): Record the number of SHA256s that were aborted.
  for (std::unique_ptr<FileEventValue>& e : *staged_events_->ordered_events) {
    batch_sender_->Enqueue(std::move(e->event));
  }
  batch_sender_->Flush();
  staged_events_->Reset(0);
}

void FilePlugin::OnDeviceUserRetrieved(
    std::unique_ptr<FileEventValue> file_event_value,
    const std::string& device_user,
    const std::string& device_userhash) {
  file_event_value->event->mutable_common()->set_device_user(device_user);
  CollectEvent(std::move(file_event_value));
}

// Fills out the file image information in the proto.
// This function does not fill out the SHA256 information or
// the provenance information.
void FilePlugin::FillFileImageInfo(
    cros_xdr::reporting::FileImage* file_image,
    const secagentd::bpf::cros_file_image& image_info,
    bool use_after_modification_attribute) {
  if (use_after_modification_attribute) {
    file_image->set_pathname(std::string(image_info.path));
    file_image->set_mnt_ns(image_info.mnt_ns);
    file_image->set_inode_device_id(
        KernelToUserspaceDeviceId(image_info.device_id));
    file_image->set_inode(image_info.inode);
    file_image->set_mode(image_info.after_attr.mode);
    file_image->set_canonical_gid(image_info.after_attr.gid);
    file_image->set_canonical_uid(image_info.after_attr.uid);
  } else {
    file_image->set_mode(image_info.before_attr.mode);
    file_image->set_canonical_gid(image_info.before_attr.gid);
    file_image->set_canonical_uid(image_info.before_attr.uid);
  }
}

std::unique_ptr<cros_xdr::reporting::FileReadEvent>
FilePlugin::MakeFileReadEvent(
    const secagentd::bpf::cros_file_detailed_event& file_detailed_event) {
  auto read_event_proto = std::make_unique<pb::FileReadEvent>();
  auto* file_read_proto = read_event_proto->mutable_file_read();

  ProcessCache::FillProcessTree(
      read_event_proto.get(), file_detailed_event.process_info,
      file_detailed_event.has_full_process_info, process_cache_, device_user_);

  //  optional SensitiveFileType sensitive_file_type = 1;
  // optional FileProvenance file_provenance = 2;
  file_read_proto->set_sensitive_file_type(static_cast<pb::SensitiveFileType>(
      file_detailed_event.image_info.sensitive_file_type));

  FillFileImageInfo(file_read_proto->mutable_image(),
                    file_detailed_event.image_info, true);

  return read_event_proto;
}

std::unique_ptr<cros_xdr::reporting::FileModifyEvent>
FilePlugin::MakeFileModifyEvent(
    const secagentd::bpf::cros_file_detailed_event& file_detailed_event) {
  auto modify_event_proto = std::make_unique<pb::FileModifyEvent>();
  auto* file_modify_proto = modify_event_proto->mutable_file_modify();

  ProcessCache::FillProcessTree(
      modify_event_proto.get(), file_detailed_event.process_info,
      file_detailed_event.has_full_process_info, process_cache_, device_user_);
  file_modify_proto->set_modify_type(cros_xdr::reporting::FileModify::WRITE);

  file_modify_proto->set_sensitive_file_type(static_cast<pb::SensitiveFileType>(
      file_detailed_event.image_info.sensitive_file_type));
  // optional FileProvenance file_provenance = 2;

  FillFileImageInfo(file_modify_proto->mutable_image_after(),
                    file_detailed_event.image_info, true);

  return modify_event_proto;
}

std::unique_ptr<cros_xdr::reporting::FileModifyEvent>
FilePlugin::MakeFileAttributeModifyEvent(
    const secagentd::bpf::cros_file_detailed_event& file_detailed_event) {
  auto modify_event_proto = std::make_unique<pb::FileModifyEvent>();
  auto* file_modify_proto = modify_event_proto->mutable_file_modify();

  file_modify_proto->set_modify_type(
      cros_xdr::reporting::FileModify::MODIFY_ATTRIBUTE);

  ProcessCache::FillProcessTree(
      modify_event_proto.get(), file_detailed_event.process_info,
      file_detailed_event.has_full_process_info, process_cache_, device_user_);

  file_modify_proto->set_sensitive_file_type(static_cast<pb::SensitiveFileType>(
      file_detailed_event.image_info.sensitive_file_type));
  // optional FileProvenance file_provenance = 2;

  FillFileImageInfo(file_modify_proto->mutable_image_after(),
                    file_detailed_event.image_info, true);
  FillFileImageInfo(file_modify_proto->mutable_attributes_before(),
                    file_detailed_event.image_info, false);

  return modify_event_proto;
}

void FilePlugin::StageEventsForAsyncProcessing() {
  /* This collects the EventKeys that need a SHA256 computed on them.
   * The algorithm is as follows:
   * For a given inode there is a vector of keytypes that need their SHAs
   * filled asynchronously.
   * ordered_events_ is a chronologically ordered vector of event keys where
   * ordered_events_.back() is the most recent event key.
   * We iterate through ordered_events_ (from the past to the present) and
   * If a event key is encountered is a read then the key will be added
   * to a event key vector associated with the inode.
   * If an event key corresponds to an event that modifies the contents of the
   * file then the event key vector for the inode will be cleared and the
   * event key will be added to the vector.
   *
   * The desired effect is reduce the likelihood that SHA256s are incorrect
   * as much as possible.
   */

  absl::flat_hash_map<InodeKey, std::vector<HashComputeInput>> hash_jobs;
  staged_events_.swap(current_events_);
  // advance the generation.
  current_events_->Reset(staged_events_->generation + 1);

  for (const auto& event_info : *(staged_events_->ordered_events)) {
    auto result = GenerateFileEventKey(*event_info->event);
    if (!result.ok()) {
      LOG(WARNING) << "Unable to defer SHA256 for a file key generation failed:"
                   << result.status();
      continue;
    }
    auto& event_key = result.value();
    auto& inode_key = event_key.inode_key;
    if (event_key.event_type == pb::FileEventAtomicVariant::kSensitiveModify) {
      auto modify_type =
          event_info->event->sensitive_modify().file_modify().modify_type();
      // An event that modifies a file aborts all the preceding SHA256s
      // on that file.
      if (modify_type == pb::FileModify::WRITE ||
          modify_type == pb::FileModify::WRITE_AND_MODIFY_ATTRIBUTE) {
        hash_jobs[inode_key].clear();
      }
    }
    hash_jobs[inode_key].push_back(
        HashComputeInput{.key = event_key,
                         .generation = staged_events_->generation,
                         .meta_data = event_info->meta_data});
  }  // For ordered events.

  for (const auto& [_, jobs] : hash_jobs) {
    for (const auto& job : jobs) {
      // TODO(b:362014987): Add metrics about the total time it takes to
      // calculate a SHA256. Need to record start time of jobs in flight
      // and then the time the result takes to come back.
      async_io_task_tracker_.PostTaskAndReplyWithResult(
          async_io_task_.get(), FROM_HERE,
          base::BindOnce(&AsyncHashCompute, std::move(job), image_cache_),
          base::BindOnce(&FilePlugin::ReceiveHashComputeResults,
                         weak_ptr_factory_.GetWeakPtr()));
    }
  }
  async_abort_timer_.Start(
      FROM_HERE, base::Seconds(async_timeout_s_),
      base::BindOnce(&FilePlugin::OnAsyncHashComputeTimeout,
                     weak_ptr_factory_.GetWeakPtr()));
}

void FilePlugin::ReceiveHashComputeResults(
    absl::StatusOr<HashComputeResult> hash_result) {
  // TODO(jasonling): Add logic to guarantee that this method is only
  //  ever executed on the same sequence that the object was created on.
  if (!hash_result.ok()) {
    // TODO(b:362014987): record metrics on SHA256 failures.
    return;
  }
  auto& result = hash_result.value();
  MetricsSender::GetInstance().IncrementCountMetric(
      metrics::kSHA256SizeMiB, result.hash_result.file_size / (bytes_per_mib));
  int64_t compute_time_ms = result.hash_result.compute_time.InMilliseconds();
  MetricsSender::GetInstance().IncrementCountMetric(
      metrics::kSHA256ComputeTime100ms, compute_time_ms % 100 < 50
                                            ? compute_time_ms / 100
                                            : compute_time_ms / 100 + 1);
  if (result.generation == staged_events_->generation) {
    auto it = staged_events_->event_map.find(result.key);
    if (it == staged_events_->event_map.end()) {
      LOG(ERROR)
          << "Hash compute result received for the current staged"
          << " generation but the corresponding event couldn't be found.";
      return;
    }
    base::WeakPtr<FileEventValue>& fev = it->second;
    if (!fev) {
      // This should never happens, this means that the event map and
      // ordered event vector are not coherent.
      // TODO(b:362014987): Add metrics.
      LOG(ERROR) << "keytype is associated with a destroyed event";
      return;
    }
    pb::FileEventAtomicVariant& pb_event = *fev->event;
    // Update the SHA256
    auto image_result = GetMutableImage(pb_event);
    if (!image_result.ok()) {
      LOG(ERROR) << image_result.status();
      return;
    }
    image_result.value()->set_sha256(result.hash_result.sha256);
    image_result.value()->set_partial_sha256(
        result.hash_result.sha256_is_partial);
  }
}

void FilePlugin::CollectedEvents::Reset(uint64_t generation_in) {
  generation = generation_in;
  event_map.clear();
  ordered_events->clear();
}

}  // namespace secagentd
