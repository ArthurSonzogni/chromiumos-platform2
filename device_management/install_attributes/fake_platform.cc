// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_management/install_attributes/fake_platform.h"

#include <linux/fs.h>
#include <string>
#include "base/files/file_util.h"
#include <sys/stat.h>
#include <sys/types.h>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/unguessable_token.h>
#include <brillo/blkdev_utils/loop_device_fake.h>
#include <brillo/cryptohome.h>
#include <brillo/files/file_util.h>
#include <brillo/secure_blob.h>

namespace device_management {

FakePlatform::FakePlatform() {
  CHECK(tmpfs_rootfs_.CreateUniqueTempDir());
  CHECK(CreateDirectory(tmpfs_rootfs_.GetPath()));
}

FakePlatform::~FakePlatform() {
  DeletePathRecursively(tmpfs_rootfs_.GetPath());
}

// Helpers

bool FakePlatform::CreateDirectory(const base::FilePath& path) {
  DCHECK(path.IsAbsolute()) << "path=" << path;
  return base::CreateDirectory(path);
}

bool FakePlatform::DeletePathRecursively(const base::FilePath& path) {
  DCHECK(path.IsAbsolute()) << "path=" << path;
  return brillo::DeletePathRecursively(path);
}

base::FilePath FakePlatform::TestFilePath(const base::FilePath& path) const {
  CHECK(path.IsAbsolute());
  std::string path_str = path.NormalizePathSeparators().value();
  if (path_str.length() > 0 && path_str[0] == '/') {
    path_str = path_str.substr(1);
  }
  return tmpfs_rootfs_.GetPath().Append(path_str);
}

void FakePlatform::RemoveFakeEntries(const base::FilePath& path) {
  base::AutoLock lock(mappings_lock_);
  file_owners_.erase(path);
  file_mode_.erase(path);
  file_flags_.erase(path);
}

// Platform API

bool FakePlatform::DeleteFile(const base::FilePath& path) {
  RemoveFakeEntries(path);
  return real_platform_.DeleteFile(TestFilePath(path));
}

bool FakePlatform::FileExists(const base::FilePath& path) const {
  return real_platform_.FileExists(TestFilePath(path));
}

bool FakePlatform::SyncDirectory(const base::FilePath& path) {
  return real_platform_.SyncDirectory(TestFilePath(path));
}

bool FakePlatform::ReadFile(const base::FilePath& path, brillo::Blob* blob) {
  return real_platform_.ReadFile(TestFilePath(path), blob);
}

bool FakePlatform::WriteFileAtomic(const base::FilePath& path,
                                   const brillo::Blob& blob,
                                   mode_t mode) {
  return real_platform_.WriteFileAtomic(TestFilePath(path), blob, mode);
}

bool FakePlatform::WriteFileAtomicDurable(const base::FilePath& path,
                                          const brillo::Blob& blob,
                                          mode_t mode) {
  return real_platform_.WriteFileAtomicDurable(TestFilePath(path), blob, mode);
}

}  // namespace device_management
