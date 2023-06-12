// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_MANAGEMENT_INSTALL_ATTRIBUTES_FAKE_PLATFORM_H_
#define DEVICE_MANAGEMENT_INSTALL_ATTRIBUTES_FAKE_PLATFORM_H_

#include <sys/stat.h>
#include <sys/types.h>
#include <unordered_map>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/unguessable_token.h>
#include <brillo/blkdev_utils/loop_device_fake.h>
#include <brillo/secure_blob.h>

#include "base/files/scoped_temp_dir.h"
#include "device_management/install_attributes/platform.h"

namespace device_management {

class FakePlatform final : public Platform {
 public:
  FakePlatform();
  ~FakePlatform() override;

  // Prohibit copy/move/assignment.
  FakePlatform(const FakePlatform&) = delete;
  FakePlatform(const FakePlatform&&) = delete;
  FakePlatform& operator=(const FakePlatform&) = delete;
  FakePlatform& operator=(const FakePlatform&&) = delete;

  // Platform API

  bool DeleteFile(const base::FilePath& path) override;
  bool FileExists(const base::FilePath& path) const override;
  bool SyncDirectory(const base::FilePath& path) override;
  bool ReadFile(const base::FilePath& path, brillo::Blob* blob) override;
  bool WriteFileAtomic(const base::FilePath& path,
                       const brillo::Blob& blob,
                       mode_t mode) override;
  bool WriteFileAtomicDurable(const base::FilePath& path,
                              const brillo::Blob& blob,
                              mode_t mode) override;

  // Mappings for fake attributes of files.
  // Lock to protect the mappings. Should be held when reading or writing
  // them, because the calls into platform may happen concurrently.
  mutable base::Lock mappings_lock_;
  mutable std::unordered_map<base::FilePath, std::pair<uid_t, gid_t>>
      file_owners_;
  mutable std::unordered_map<base::FilePath, mode_t> file_mode_;
  mutable std::unordered_map<base::FilePath, int> file_flags_;
  base::ScopedTempDir tmpfs_rootfs_;
  Platform real_platform_;

  void RemoveFakeEntries(const base::FilePath& path);
  bool CreateDirectory(const base::FilePath& path);
  bool DeletePathRecursively(const base::FilePath& path);
  base::FilePath TestFilePath(const base::FilePath& path) const;
};

}  // namespace device_management

#endif  // DEVICE_MANAGEMENT_INSTALL_ATTRIBUTES_FAKE_PLATFORM_H_
