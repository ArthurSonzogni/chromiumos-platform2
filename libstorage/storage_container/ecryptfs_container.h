// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSTORAGE_STORAGE_CONTAINER_ECRYPTFS_CONTAINER_H_
#define LIBSTORAGE_STORAGE_CONTAINER_ECRYPTFS_CONTAINER_H_

#include "libstorage/storage_container/storage_container.h"

#include <base/files/file_path.h>
#include <brillo/brillo_export.h>
#include <libstorage/platform/keyring/keyring.h>
#include <libstorage/platform/platform.h>

#include "libstorage/storage_container/filesystem_key.h"

namespace libstorage {

// `EcryptfsContainer` is a file-level encrypted container which uses eCryptFs
// to encrypted the `backing_dir_`.
class BRILLO_EXPORT EcryptfsContainer : public StorageContainer {
 public:
  EcryptfsContainer(const base::FilePath& backing_dir,
                    const FileSystemKeyReference& key_reference,
                    Platform* platform,
                    Keyring* keyring);
  ~EcryptfsContainer() = default;

  bool Setup(const FileSystemKey& encryption_key) override;
  bool Teardown() override;
  bool Exists() override;
  bool Purge() override;
  bool Reset() override;
  StorageContainerType GetType() const override {
    return StorageContainerType::kEcryptfs;
  }
  base::FilePath GetPath() const override { return GetBackingLocation(); }
  base::FilePath GetBackingLocation() const override;

 private:
  const base::FilePath backing_dir_;
  FileSystemKeyReference key_reference_;
  Platform* platform_;
  Keyring* keyring_;
};

}  // namespace libstorage

#endif  // LIBSTORAGE_STORAGE_CONTAINER_ECRYPTFS_CONTAINER_H_
