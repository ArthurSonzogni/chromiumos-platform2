// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSTORAGE_STORAGE_CONTAINER_FSCRYPT_CONTAINER_H_
#define LIBSTORAGE_STORAGE_CONTAINER_FSCRYPT_CONTAINER_H_

#include "libstorage/storage_container/storage_container.h"

#include <base/files/file_path.h>
#include <brillo/brillo_export.h>
#include <libstorage/platform/keyring/keyring.h>
#include <libstorage/platform/platform.h>

#include "libstorage/storage_container/filesystem_key.h"

namespace libstorage {

// `FscryptContainer` is a file-level encrypted container which uses fscrypt to
// encrypt the `backing_dir_` transparently.
class BRILLO_EXPORT FscryptContainer : public StorageContainer {
 public:
  FscryptContainer(const base::FilePath& backing_dir,
                   const FileSystemKeyReference& key_reference,
                   bool allow_v2,
                   Platform* platform,
                   Keyring* keyring);
  ~FscryptContainer() = default;

  bool Setup(const FileSystemKey& encryption_key) override;
  bool Teardown() override;
  bool Exists() override;
  bool Reset() override;
  bool Purge() override;
  StorageContainerType GetType() const override {
    return StorageContainerType::kFscrypt;
  }
  base::FilePath GetPath() const override { return GetBackingLocation(); }
  base::FilePath GetBackingLocation() const override;

 private:
  // Deduces whether V1 or V2 policy should be used.
  bool UseV2();

  const base::FilePath backing_dir_;
  FileSystemKeyReference key_reference_;
  bool allow_v2_;
  Platform* platform_;
  Keyring* keyring_;
};

}  // namespace libstorage

#endif  // LIBSTORAGE_STORAGE_CONTAINER_FSCRYPT_CONTAINER_H_
