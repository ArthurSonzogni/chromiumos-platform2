// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSTORAGE_STORAGE_CONTAINER_STORAGE_CONTAINER_FACTORY_H_
#define LIBSTORAGE_STORAGE_CONTAINER_STORAGE_CONTAINER_FACTORY_H_

#include "libstorage/storage_container/storage_container.h"

#include <memory>

#include <base/files/file_path.h>
#include <brillo/brillo_export.h>
#include <libstorage/platform/keyring/keyring.h>
#include <libstorage/platform/platform.h>
#include <metrics/metrics_library.h>

#include "libstorage/storage_container/backing_device_factory.h"
#include "libstorage/storage_container/filesystem_key.h"

namespace libstorage {

// `StorageContainerFactory` abstracts the creation of encrypted containers.
class BRILLO_EXPORT StorageContainerFactory {
 public:
  explicit StorageContainerFactory(Platform* platform,
                                   MetricsLibraryInterface* metrics);
  StorageContainerFactory(
      Platform* platform,
      MetricsLibraryInterface* metrics,
      std::unique_ptr<Keyring> keyring,
      std::unique_ptr<BackingDeviceFactory> backing_device_factory);
  virtual ~StorageContainerFactory() {}

  virtual std::unique_ptr<StorageContainer> Generate(
      const StorageContainerConfig& config,
      const StorageContainerType type,
      const FileSystemKeyReference& key_reference);

  void set_allow_fscrypt_v2(bool allow_fscrypt_v2) {
    allow_fscrypt_v2_ = allow_fscrypt_v2;
  }

 private:
  Platform* platform_;
  MetricsLibraryInterface* metrics_;
  std::unique_ptr<Keyring> keyring_;
  std::unique_ptr<BackingDeviceFactory> backing_device_factory_;
  bool allow_fscrypt_v2_;
};

}  // namespace libstorage

#endif  // LIBSTORAGE_STORAGE_CONTAINER_STORAGE_CONTAINER_FACTORY_H_
