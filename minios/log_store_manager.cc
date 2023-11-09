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
#include "minios/log_store_manager_interface.h"
#include "minios/log_store_manifest.h"
#include "minios/utils.h"

namespace minios {

// Offset from end of partition to store encrypted logs.
const uint64_t kLogStoreOffset = 22 * kBlockSize;
// Max allowable size of a log when saving to disk.
const uint64_t kMaxLogSize = 20 * kBlockSize;

const base::FilePath kStatefulArchivePath{std::string{kStatefulPath} +
                                          "logs.tar"};

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

  const auto partition_number = GetMiniOsPriorityPartition(cros_system);
  if (!partition_number) {
    LOG(ERROR) << "Failed to find priority MiniOS partition.";
    return false;
  }
  disk_path_ = brillo::AppendPartition(fixed_drive, partition_number.value());

  const auto cgpt_util = std::make_shared<CgptUtil>(fixed_drive, cgpt_wrapper);
  partition_size_ = GetPartitionSize(partition_number.value(), cgpt_util);

  if (!partition_size_) {
    LOG(ERROR) << "Couldn't determine size of partition="
               << partition_number.value();
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

  std::optional<EncryptedLogFile> encrypted_contents;
  if (direction == LogDirection::Disk || direction == LogDirection::Stateful) {
    encrypted_contents = EncryptLogs(archive_path);
    if (!encrypted_contents || !encrypt_key_) {
      LOG(ERROR) << "Couldn't encrypt logs";
      return false;
    }
    if (!SaveLogStoreKey(process_manager_, encrypt_key_.value())) {
      LOG(ERROR) << "Failed to save key.";
      return false;
    }
  }

  switch (direction) {
    case LogDirection::Disk:
      if (!SaveLogsToDisk(encrypted_contents.value())) {
        LOG(ERROR) << "Failed to save logs to disk.";
        return false;
      }
      return true;
    case LogDirection::Stateful:
      if (!SaveLogsToPath(path.value(), encrypted_contents.value())) {
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

bool LogStoreManager::FetchLogs(
    LogDirection direction,
    const base::FilePath& unencrypted_archive_path,
    const std::optional<base::FilePath>& encrypted_archive_path) const {
  switch (direction) {
    case LogDirection::Disk:
      return FetchDiskLogs(unencrypted_archive_path);
    case LogDirection::Stateful:
      if (!encrypted_archive_path ||
          !base::PathExists(encrypted_archive_path.value())) {
        LOG(ERROR) << "Invalid encrypted archive path.";
        return false;
      }

      return FetchStatefulLogs(unencrypted_archive_path,
                               encrypted_archive_path.value());
    case LogDirection::RemovableDevice:
      LOG(ERROR) << "Fetching logs from removable device not supported.";
      return false;
    default:
      return false;
  }
}

std::optional<EncryptedLogFile> LogStoreManager::EncryptLogs(
    const base::FilePath& archive_path) {
  encrypt_key_ =
      hwsec_foundation::CreateSecureRandomBlob(kLogStoreKeySizeBytes);

  const auto& archive_data = ReadFileToSecureBlob(archive_path);
  if (!archive_data) {
    LOG(ERROR) << "Failed to read log archive.";
    return std::nullopt;
  }
  const auto& encrypted_contents =
      EncryptLogArchiveData(archive_data.value(), encrypt_key_.value());
  if (!encrypted_contents) {
    LOG(ERROR) << "Failed to encrypt data.";
    return std::nullopt;
  }
  return encrypted_contents;
}

bool LogStoreManager::SaveLogsToDisk(
    const EncryptedLogFile& encrypted_contents) {
  if (encrypted_contents.ByteSizeLong() > kMaxLogSize) {
    LOG(WARNING) << "Encrypted compressed logs exceed reserved disk space.";
    return false;
  }

  LogManifest::Entry entry;
  entry.set_offset(partition_size_.value() - kLogStoreOffset);
  entry.set_count(encrypted_contents.ByteSizeLong());

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
                               encrypted_contents.SerializeAsString().c_str()),
                           encrypted_contents.SerializeAsString().size()})) {
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
    const base::FilePath& path, const EncryptedLogFile& encrypted_contents) {
  if (!brillo::WriteStringToFile(path,
                                 encrypted_contents.SerializeAsString())) {
    PLOG(ERROR) << "Failed to write to file=" << path;
    return false;
  }

  return true;
}

bool LogStoreManager::FetchDiskLogs(
    const base::FilePath& unencrypted_archive_path) const {
  if (!log_store_manifest_) {
    LOG(ERROR) << "No log store manifest, unable to fetch logs.";
    return false;
  }
  // If no manifest is present, then no logs are assumed to be stored on this
  // partition.
  const auto manifest = log_store_manifest_->Retreive();
  if (!manifest) {
    LOG(INFO) << "No manifest found, no logs retrieved.";
    return false;
  }

  if (manifest->entry().offset() <= kernel_size_.value()) {
    LOG(ERROR) << "Log store within kernel, log store offset="
               << manifest->entry().offset()
               << " kernel size=" << kernel_size_.value();
    return false;
  }

  const auto retrieved_key = GetLogStoreKey(process_manager_);
  // If the key is zero'd out then the log store is presumed to be cleared,
  // return without fetching.
  if (!retrieved_key || retrieved_key.value() == brillo::SecureBlob{kZeroKey}) {
    LOG(INFO) << "No key found.";
    return false;
  }

  // Read disk at the manifest specified offset into proto and then decrypt
  // contents.
  base::ScopedFD disk_fd(
      HANDLE_EINTR(open(disk_path_.value().c_str(), O_RDONLY)));
  if (!disk_fd.is_valid()) {
    LOG(ERROR) << "Failed to open disk=" << disk_path_;
    return false;
  }

  if (HANDLE_EINTR(lseek(disk_fd.get(), manifest->entry().offset(),
                         SEEK_SET)) != manifest->entry().offset()) {
    PLOG(ERROR) << "Failed to seek=" << disk_path_;
    return false;
  }
  EncryptedLogFile encrypted_contents;
  encrypted_contents.ParseFromFileDescriptor(disk_fd.get());

  const auto decrypted_contents =
      DecryptLogArchiveData(encrypted_contents, retrieved_key.value());
  if (!decrypted_contents) {
    LOG(ERROR) << "Failed to decrypt logs.";
    return false;
  }

  return WriteSecureBlobToFile(unencrypted_archive_path,
                               decrypted_contents.value());
}

bool LogStoreManager::FetchStatefulLogs(
    const base::FilePath& unencrypted_archive_path,
    const base::FilePath& encrypted_archive_path) const {
  const auto retrieved_key = GetLogStoreKey(process_manager_);
  // If the key is zero'd out then the log store is presumed to be cleared,
  // return without fetching.
  if (!retrieved_key || retrieved_key.value() == brillo::SecureBlob{kZeroKey}) {
    LOG(INFO) << "No key found.";
    return false;
  }

  base::ScopedFD encrypted_archive_fd(
      HANDLE_EINTR(open(encrypted_archive_path.value().c_str(), O_RDONLY)));
  if (!encrypted_archive_fd.is_valid()) {
    LOG(ERROR) << "Failed to open archive at: " << encrypted_archive_path;
    return false;
  }

  EncryptedLogFile encrypted_contents;
  encrypted_contents.ParseFromFileDescriptor(encrypted_archive_fd.get());

  const auto decrypted_contents =
      DecryptLogArchiveData(encrypted_contents, retrieved_key.value());
  if (!decrypted_contents) {
    LOG(ERROR) << "Failed to decrypt logs.";
    return false;
  }

  return WriteSecureBlobToFile(unencrypted_archive_path,
                               decrypted_contents.value());
}

bool LogStoreManager::ClearLogs() const {
  if (!log_store_manifest_) {
    LOG(ERROR) << "No log store manifest, unable to clear logs.";
    return false;
  }
  auto manifest = log_store_manifest_->Retreive();
  if (!manifest) {
    LOG(INFO) << "No manifest found on disk, nothing to clear.";
    return true;
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
