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

#include "minios/disk_util.h"
#include "minios/log_store_manager_interface.h"
#include "minios/log_store_manifest_interface.h"
#include "minios/process_manager.h"
#include "minios/process_manager_interface.h"

namespace minios {

extern const uint64_t kLogStoreOffset;
extern const uint64_t kMaxLogSize;
extern const base::FilePath kStatefulArchivePath;

class LogStoreManager : public LogStoreManagerInterface {
 public:
  LogStoreManager() : process_manager_(std::make_shared<ProcessManager>()) {}

  LogStoreManager(std::unique_ptr<LogStoreManifestInterface> log_store_manifest,
                  std::shared_ptr<ProcessManagerInterface> process_manager,
                  const base::FilePath& disk_path,
                  uint64_t kernel_size,
                  uint64_t partition_size)
      : log_store_manifest_(std::move(log_store_manifest)),
        process_manager_(process_manager),
        disk_path_(disk_path),
        kernel_size_(kernel_size),
        partition_size_(partition_size) {}

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

  bool FetchLogs(LogDirection direction,
                 const base::FilePath& unencrypted_archive_path,
                 const std::optional<base::FilePath>& encrypted_archive_path =
                     std::nullopt) const override;

  bool ClearLogs() const override;

 private:
  FRIEND_TEST(LogStoreManagerEncryptTest, EncryptLogsTest);
  FRIEND_TEST(LogStoreManagerTest, SaveLogsToPathTest);
  FRIEND_TEST(LogStoreManagerTest, SaveLogsToDiskTest);
  FRIEND_TEST(LogStoreManagerInitTest, InitTest);
  FRIEND_TEST(LogStoreManagerInitTest, InitKernelLogStoreOverlapTest);

  std::optional<EncryptedLogFile> EncryptLogs(
      const base::FilePath& archive_path);
  bool SaveLogsToDisk(const EncryptedLogFile& encrypted_contents);
  bool SaveLogsToPath(const base::FilePath& path,
                      const EncryptedLogFile& encrypted_contents);
  bool FetchDiskLogs(const base::FilePath& unencrypted_archive_path) const;
  bool FetchStatefulLogs(const base::FilePath& unencrypted_archive_path,
                         const base::FilePath& encrypted_archive_path) const;

  std::unique_ptr<LogStoreManifestInterface> log_store_manifest_;
  std::shared_ptr<ProcessManagerInterface> process_manager_;

  base::FilePath disk_path_;
  std::optional<uint64_t> kernel_size_;
  std::optional<uint64_t> partition_size_;

  std::optional<brillo::SecureBlob> encrypt_key_;
};

}  // namespace minios

#endif  // MINIOS_LOG_STORE_MANAGER_H_
