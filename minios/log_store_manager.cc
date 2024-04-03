// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/log_store_manager.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/posix/eintr_wrapper.h>
#include <brillo/blkdev_utils/storage_utils.h>
#include <brillo/file_utils.h>
#include <brillo/files/file_util.h>
#include <brillo/secure_blob.h>
#include <libcrossystem/crossystem.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <minios/proto_bindings/minios.pb.h>

#include "minios/cgpt_util.h"
#include "minios/disk_util.h"
#include "minios/log_store_manifest.h"
#include "minios/utils.h"

namespace minios {

// Offset from end of partition to store encrypted logs.
const uint64_t kLogStoreOffset = 22 * kBlockSize;
// Max allowable size of a log when saving to disk.
const uint64_t kMaxLogSize = 20 * kBlockSize;
// Strip `/var/log` folder paths when extracting.
const char kTarStripComponentFlag[] = "--strip-components=2";

bool LogStoreManager::Init(std::shared_ptr<DiskUtil> disk_util,
                           std::shared_ptr<crossystem::Crossystem> cros_system,
                           std::shared_ptr<CgptWrapperInterface> cgpt_wrapper) {
  // Identify the fixed drive along with active MiniOS side to determine the
  // current partition.
  const auto& fixed_drive = disk_util->GetFixedDrive();
  if (fixed_drive.empty()) {
    LOG(ERROR) << "Couldn't find fixed drive.";
    return false;
  }

  if (!partition_number_)
    partition_number_ = GetMiniOsPriorityPartition(cros_system);

  if (!partition_number_) {
    LOG(ERROR) << "Failed to find priority MiniOS partition.";
    return false;
  }
  disk_path_ = brillo::AppendPartition(fixed_drive, partition_number_.value());

  const auto cgpt_util = std::make_shared<CgptUtil>(fixed_drive, cgpt_wrapper);
  partition_size_ = GetPartitionSize(partition_number_.value(), cgpt_util);

  if (!partition_size_) {
    LOG(ERROR) << "Couldn't determine size of partition="
               << partition_number_.value();
    return false;
  }

  // Determine the kernel size and partition size to ensure our disk operations
  // are always at a valid location.
  kernel_size_ = KernelSize(process_manager_, disk_path_);
  if (!kernel_size_) {
    LOG(ERROR) << "Could not determine kernel size on partition: "
               << disk_path_;
    return false;
  }

  // Ensure that the log store does not encroach kernel space on disk.
  if (kernel_size_.value() > (partition_size_.value() - kLogStoreOffset)) {
    LOG(ERROR) << "Kernel overlaps with log store.";
    return false;
  }

  log_store_manifest_ = std::make_unique<LogStoreManifest>(
      disk_path_, kernel_size_.value(), partition_size_.value());

  return true;
}

bool LogStoreManager::SaveLogs(LogDirection direction,
                               const std::optional<base::FilePath>& path) {
  if ((direction == LogDirection::RemovableDevice ||
       direction == LogDirection::Stateful) &&
      !path) {
    LOG(ERROR)
        << "Path must be specified when saving logs to stateful or device.";
    return false;
  }

  base::ScopedTempDir archive_folder;
  base::FilePath archive_path;
  if (!archive_folder.CreateUniqueTempDir() || !archive_folder.IsValid()) {
    LOG(ERROR) << "Failed to create temp dir.";
    return false;
  }
  if (!base::CreateTemporaryFileInDir(archive_folder.GetPath(),
                                      &archive_path)) {
    LOG(ERROR) << "Failed to create temporary file in folder="
               << archive_folder.GetPath();
    return false;
  }

  if (CompressLogs(process_manager_, archive_path) != 0) {
    LOG(ERROR) << "Failed to compress logs.";
    return false;
  }

  std::optional<EncryptedLogFile> encrypted_archive;
  if (direction == LogDirection::Disk || direction == LogDirection::Stateful) {
    encrypted_archive = EncryptLogs(archive_path);
    if (!encrypted_archive || !encrypt_key_) {
      LOG(ERROR) << "Couldn't encrypt logs";
      return false;
    }
    if (!SaveLogStoreKey(vpd_, encrypt_key_.value())) {
      LOG(ERROR) << "Failed to save key.";
      return false;
    }
  }

  switch (direction) {
    case LogDirection::Disk:
      if (!SaveLogsToDisk(encrypted_archive.value())) {
        LOG(ERROR) << "Failed to save logs to disk.";
        return false;
      }
      return true;
    case LogDirection::Stateful:
      if (!SaveLogsToPath(path.value(), encrypted_archive.value())) {
        LOG(ERROR) << "Failed to save logs to stateful path=" << path->value();
        return false;
      }
      return true;
    case LogDirection::RemovableDevice:
      if (!base::Move(archive_path, path.value())) {
        PLOG(ERROR) << "Failed to move file from=" << archive_path
                    << " to=" << path.value();
        return false;
      }
      return true;
    default:
      return false;
  }
}

std::optional<bool> LogStoreManager::FetchLogs(
    LogDirection direction,
    const base::FilePath& dest_directory,
    const brillo::SecureBlob& key,
    const std::optional<base::FilePath>& encrypted_archive_path) const {
  EncryptedLogFile encrypted_archive;
  // If read resulted in errors, or no logs were found, return immediately.
  if (const auto read_result =
          ReadLogs(direction, encrypted_archive_path, encrypted_archive);
      !read_result || !read_result.value()) {
    return read_result;
  }

  // If the key is zero'd out then the log store is presumed to be cleared,
  // return without decrypting.
  if (key == kNullKey) {
    LOG(INFO) << "No key found.";
    return false;
  }
  const auto& archive = DecryptLogArchive(encrypted_archive, key);
  // If logs can't be decrypted, report that no logs were fetched.
  if (!archive) {
    return false;
  }
  return ExtractLogs(archive.value(), dest_directory);
}

std::optional<EncryptedLogFile> LogStoreManager::EncryptLogs(
    const base::FilePath& archive_path) {
  encrypt_key_ =
      hwsec_foundation::CreateSecureRandomBlob(kLogStoreKeySizeBytes);

  const auto& archive = ReadFileToSecureBlob(archive_path);
  if (!archive) {
    LOG(ERROR) << "Failed to read log archive.";
    return std::nullopt;
  }
  const auto& encrypted_archive =
      EncryptLogArchive(archive.value(), encrypt_key_.value());
  if (!encrypted_archive) {
    LOG(ERROR) << "Failed to encrypt logs.";
    return std::nullopt;
  }
  return encrypted_archive;
}

bool LogStoreManager::SaveLogsToDisk(
    const EncryptedLogFile& encrypted_archive) {
  if (encrypted_archive.ByteSizeLong() > kMaxLogSize) {
    LOG(WARNING) << "Encrypted compressed logs exceed reserved disk space.";
    return false;
  }

  LogManifest::Entry entry;
  entry.set_offset(partition_size_.value() - kLogStoreOffset);
  entry.set_count(encrypted_archive.ByteSizeLong());

  if (!log_store_manifest_) {
    LOG(ERROR) << "No log store manifest, unable to store logs to disk.";
    return false;
  }
  log_store_manifest_->Generate(entry);

  base::File file{disk_path_, base::File::FLAG_OPEN | base::File::FLAG_WRITE};
  if (!file.IsValid()) {
    PLOG(ERROR) << "Couldn't open file=" << disk_path_;
    return false;
  }
  if (!file.WriteAndCheck(partition_size_.value() - kLogStoreOffset,
                          {reinterpret_cast<const uint8_t*>(
                               encrypted_archive.SerializeAsString().c_str()),
                           encrypted_archive.SerializeAsString().size()})) {
    PLOG(ERROR) << "Failed to write to file=" << disk_path_;
    return false;
  }

  if (!log_store_manifest_->Write()) {
    LOG(ERROR) << "Failed to write manifest to disk.";
    return false;
  }
  return true;
}

bool LogStoreManager::SaveLogsToPath(
    const base::FilePath& path, const EncryptedLogFile& encrypted_archive) {
  if (!brillo::WriteStringToFile(path, encrypted_archive.SerializeAsString())) {
    PLOG(ERROR) << "Failed to write to file=" << path;
    return false;
  }

  return true;
}

std::optional<EncryptedLogFile> LogStoreManager::GetEncryptedArchive(
    const base::FilePath& path, uint64_t offset) const {
  base::ScopedFD fd(HANDLE_EINTR(open(path.value().c_str(), O_RDONLY)));
  if (!fd.is_valid()) {
    LOG(ERROR) << "Failed to open archive at: " << path;
    return std::nullopt;
  }
  if (offset > 0 && lseek(fd.get(), offset, SEEK_SET) != offset) {
    PLOG(ERROR) << "Failed to seek=" << path;
    return std::nullopt;
  }

  EncryptedLogFile encrypted_archive;
  encrypted_archive.ParseFromFileDescriptor(fd.get());
  return encrypted_archive;
}

bool LogStoreManager::ExtractLogs(const brillo::SecureBlob& archive,
                                  const base::FilePath& dest_directory) const {
  base::ScopedTempDir archive_folder;
  base::FilePath archive_path;
  if (!archive_folder.CreateUniqueTempDir() || !archive_folder.IsValid()) {
    LOG(ERROR) << "Failed to create temp dir.";
    return false;
  }
  if (!base::CreateTemporaryFileInDir(archive_folder.GetPath(),
                                      &archive_path)) {
    LOG(ERROR) << "Failed to create temporary file in folder="
               << archive_folder.GetPath();
    return false;
  }
  if (!WriteSecureBlobToFile(archive_path, archive)) {
    LOG(ERROR) << "Failed to write blob to=" << archive_path;
    return false;
  }
  if (!ExtractArchive(process_manager_, archive_path, dest_directory,
                      {kTarStripComponentFlag})) {
    LOG(ERROR) << "Extracting logs failed.";
    return false;
  }
  return true;
}

std::optional<bool> LogStoreManager::ReadLogs(
    LogDirection direction,
    const std::optional<base::FilePath>& encrypted_archive_path,
    EncryptedLogFile& encrypted_archive) const {
  std::optional<EncryptedLogFile> read_archive;
  switch (direction) {
    case LogDirection::Disk: {
      if (!log_store_manifest_) {
        LOG(ERROR) << "No log store manifest, unable to fetch logs.";
        return std::nullopt;
      }
      // If no manifest is present, then no logs are assumed to be stored on
      // this partition.
      const auto& manifest = log_store_manifest_->Retrieve();
      if (!manifest) {
        LOG(INFO) << "No manifest found, no logs retrieved.";
        return false;
      }

      if (manifest->entry().offset() <= kernel_size_.value()) {
        LOG(ERROR) << "Log store within kernel offset, log store offset="
                   << manifest->entry().offset()
                   << " kernel size=" << kernel_size_.value();
        return std::nullopt;
      }
      read_archive =
          GetEncryptedArchive(disk_path_, manifest->entry().offset());
      break;
    }
    case LogDirection::Stateful:
      if (!encrypted_archive_path) {
        LOG(ERROR) << "Path must be specified for fetching stateful logs.";
        return std::nullopt;
      }
      // If no logs are present at specified path, assume they were already
      // cleared.
      if (!base::PathExists(encrypted_archive_path.value())) {
        LOG(INFO) << "No logs present at=" << encrypted_archive_path.value();
        return false;
      }

      read_archive = GetEncryptedArchive(encrypted_archive_path.value());
      break;
    case LogDirection::RemovableDevice:
      LOG(ERROR) << "Fetching logs from removable device not supported.";
      return std::nullopt;
  }
  if (!read_archive) {
    LOG(ERROR) << "Failed to fetch encrypted archive.";
    return std::nullopt;
  }
  encrypted_archive = read_archive.value();
  return true;
}

bool LogStoreManager::ClearLogs() const {
  if (!log_store_manifest_) {
    LOG(ERROR) << "No log store manifest, unable to clear logs.";
    return false;
  }
  auto manifest = log_store_manifest_->Retrieve();
  if (!manifest) {
    LOG(INFO) << "No manifest found on disk, nothing to clear.";
    return true;
  }

  if (manifest->entry().offset() <= kernel_size_.value()) {
    LOG(ERROR)
        << "Skipping clear. Log store within kernel offset, log store offset="
        << manifest->entry().offset()
        << " kernel size=" << kernel_size_.value();
    return false;
  }

  const std::string clear_data(
      partition_size_.value() - manifest->entry().offset(), '0');

  base::File file{disk_path_, base::File::FLAG_OPEN | base::File::FLAG_WRITE};
  if (!file.IsValid()) {
    PLOG(ERROR) << "Couldn't open file=" << disk_path_;
    return false;
  }
  if (!file.WriteAndCheck(manifest->entry().offset(),
                          {reinterpret_cast<const uint8_t*>(clear_data.c_str()),
                           clear_data.size()})) {
    PLOG(ERROR) << "Failed to clear disk.";
    return false;
  }

  log_store_manifest_->Clear();
  return true;
}

}  // namespace minios
