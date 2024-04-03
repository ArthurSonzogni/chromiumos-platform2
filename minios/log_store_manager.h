// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_LOG_STORE_MANAGER_H_
#define MINIOS_LOG_STORE_MANAGER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include <base/files/file_path.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest_prod.h>
#include <libcrossystem/crossystem.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <minios/proto_bindings/minios.pb.h>
#include <vpd/vpd.h>

#include "minios/cgpt_wrapper.h"
#include "minios/disk_util.h"
#include "minios/log_store_manifest.h"
#include "minios/process_manager.h"

namespace minios {

extern const uint64_t kLogStoreOffset;
extern const uint64_t kMaxLogSize;

// Interface for a log store manager class.
class LogStoreManagerInterface {
 public:
  enum class LogDirection {
    Disk,
    Stateful,
    RemovableDevice,
  };

  virtual ~LogStoreManagerInterface() = default;

  virtual bool Init(
      std::shared_ptr<DiskUtil> disk_util = std::make_shared<DiskUtil>(),
      std::shared_ptr<crossystem::Crossystem> cros_system =
          std::make_shared<crossystem::Crossystem>(),
      std::shared_ptr<CgptWrapperInterface> cgpt_wrapper =
          std::make_shared<CgptWrapper>()) = 0;

  // Save logs to a specified direction. If the direction is not `Disk`, logs
  // will be written to `path`.
  virtual bool SaveLogs(
      LogDirection direction,
      const std::optional<base::FilePath>& path = std::nullopt) = 0;

  // Attempt to read, decrypt and extract logs from a specified direction. If
  // logs are found and successfully unpacked (with the provided key), they will
  // be placed at `dest_directory`. Returns nullopt on error, or true/false to
  // indicate whether logs were fetched.
  virtual std::optional<bool> FetchLogs(
      LogDirection direction,
      const base::FilePath& dest_directory,
      const brillo::SecureBlob& key,
      const std::optional<base::FilePath>& encrypted_archive_path =
          std::nullopt) const = 0;

  // Clear logs on disk.
  virtual bool ClearLogs() const = 0;
};

class LogStoreManager : public LogStoreManagerInterface {
 public:
  LogStoreManager() : LogStoreManager(std::nullopt) {}
  explicit LogStoreManager(std::optional<uint64_t> partition_number)
      : process_manager_(std::make_shared<ProcessManager>()),
        partition_number_(partition_number),
        vpd_(std::make_shared<vpd::Vpd>()) {}

  LogStoreManager(std::unique_ptr<LogStoreManifestInterface> log_store_manifest,
                  std::shared_ptr<ProcessManagerInterface> process_manager,
                  std::shared_ptr<vpd::Vpd> vpd,
                  const base::FilePath& disk_path,
                  uint64_t kernel_size,
                  uint64_t partition_size)
      : log_store_manifest_(std::move(log_store_manifest)),
        process_manager_(process_manager),
        disk_path_(disk_path),
        kernel_size_(kernel_size),
        partition_size_(partition_size),
        vpd_(vpd) {}

  ~LogStoreManager() override = default;

  LogStoreManager(const LogStoreManager&) = delete;
  LogStoreManager& operator=(const LogStoreManager&) = delete;

  bool Init(std::shared_ptr<DiskUtil> disk_util = std::make_shared<DiskUtil>(),
            std::shared_ptr<crossystem::Crossystem> cros_system =
                std::make_shared<crossystem::Crossystem>(),
            std::shared_ptr<CgptWrapperInterface> cgpt_wrapper =
                std::make_shared<CgptWrapper>()) override;

  bool SaveLogs(
      LogDirection direction,
      const std::optional<base::FilePath>& path = std::nullopt) override;

  std::optional<bool> FetchLogs(
      LogDirection direction,
      const base::FilePath& dest_directory,
      const brillo::SecureBlob& key,
      const std::optional<base::FilePath>& encrypted_archive_path =
          std::nullopt) const override;

  bool ClearLogs() const override;

 private:
  FRIEND_TEST(LogStoreManagerEncryptTest, EncryptLogsTest);
  FRIEND_TEST(LogStoreManagerTest, SaveLogsToPathTest);
  FRIEND_TEST(LogStoreManagerTest, SaveLogsToDiskTest);
  FRIEND_TEST(LogStoreManagerInitTest, InitTest);
  FRIEND_TEST(LogStoreManagerInitTest, InitKernelLogStoreOverlapTest);
  FRIEND_TEST(LogStoreManagerInitTest, InitSpecifiedPartitionTest);

  // Helper functions for `SaveLogs`.
  std::optional<EncryptedLogFile> EncryptLogs(
      const base::FilePath& archive_path);
  bool SaveLogsToDisk(const EncryptedLogFile& encrypted_archive);
  bool SaveLogsToPath(const base::FilePath& path,
                      const EncryptedLogFile& encrypted_archive);
  // Helper functions for `FetchLogs`.
  std::optional<bool> ReadLogs(
      LogDirection direction,
      const std::optional<base::FilePath>& encrypted_archive_path,
      EncryptedLogFile& encrypted_archive) const;
  std::optional<EncryptedLogFile> GetEncryptedArchive(
      const base::FilePath& path, uint64_t offset = 0) const;
  bool ExtractLogs(const brillo::SecureBlob& archive,
                   const base::FilePath& dest_directory) const;

  std::unique_ptr<LogStoreManifestInterface> log_store_manifest_;
  std::shared_ptr<ProcessManagerInterface> process_manager_;

  base::FilePath disk_path_;
  std::optional<uint64_t> kernel_size_;
  std::optional<uint64_t> partition_size_;
  // Partition target for saving and fetching logs.
  std::optional<uint64_t> partition_number_;

  std::shared_ptr<vpd::Vpd> vpd_;

  std::optional<brillo::SecureBlob> encrypt_key_;
};

}  // namespace minios

#endif  // MINIOS_LOG_STORE_MANAGER_H_
