// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSTORAGE_STORAGE_CONTAINER_STORAGE_CONTAINER_H_
#define LIBSTORAGE_STORAGE_CONTAINER_STORAGE_CONTAINER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>

#if USE_DEVICE_MAPPER
#include <brillo/blkdev_utils/device_mapper.h>
#endif

#include <brillo/brillo_export.h>
#include <libstorage/platform/platform.h>

#include "libstorage/storage_container/backing_device.h"
#include "libstorage/storage_container/filesystem_key.h"

namespace libstorage {

// Type of encrypted containers.
enum class StorageContainerType {
  kUnknown = 0,
  kEcryptfs,
  kFscrypt,
  kDmcrypt,
  kEphemeral,
  kUnencrypted,
  kExt4,
  kEcryptfsToFscrypt,
  kEcryptfsToDmcrypt,
  kFscryptToDmcrypt,
};

struct DmcryptConfig {
  BackingDeviceConfig backing_device_config;
  std::string dmcrypt_device_name;
  std::string dmcrypt_cipher;
  uint32_t iv_offset;
};

struct UnencryptedConfig {
  BackingDeviceConfig backing_device_config;
};

// Recovery option when the filesystem is not clean
enum class RecoveryType {
  kDoNothing = 0,
  kEnforceCleaning,
  kPurge,
};

struct Ext4FileSystemConfig {
  std::vector<std::string> mkfs_opts;
  std::vector<std::string> tune2fs_opts;
  StorageContainerType backend_type;
  RecoveryType recovery;
};

struct StorageContainerConfig {
  base::FilePath backing_dir;
  Ext4FileSystemConfig filesystem_config;
  DmcryptConfig dmcrypt_config;
  UnencryptedConfig unencrypted_config;
};

// An encrypted container is an abstract class that represents an encrypted
// backing storage medium. Since encrypted containers can be used in both
// daemons and one-shot calls, the implementation of each encrypted container
// leans towards keeping the container as stateless as possible.
// TODO(dlunev): rename abstraction to StorageContainer.
class BRILLO_EXPORT StorageContainer {
 public:
  virtual ~StorageContainer() {}

  // Removes the encrypted container's backing storage.
  virtual bool Purge() = 0;
  // Sets up the encrypted container, including creating the container if
  // needed.
  virtual bool Setup(const FileSystemKey& encryption_key) = 0;
  // Evict all copies of encryption keys from memory. Returns whether key
  // eviction has been done.
  virtual bool EvictKey() { return false; }
  // Restore the in-memory encryption keys. Returns whether key restoration
  // has been done.
  virtual bool RestoreKey(const FileSystemKey& encryption_key) { return false; }
  // Tears down the container, removing the encryption key if it was added.
  virtual bool Teardown() = 0;
  // Checks if the container exists on disk.
  virtual bool Exists() = 0;
  // Checks if the encryption keys in memory are valid.
  virtual bool IsDeviceKeyValid() { return false; }
  // Resize the container
  // Return false when resizing failed.
  // size of 0 resize to the size of the underlaying container / backing device.
  virtual bool Resize(int64_t size_in_bytes) { return false; }
  // Gets the type of the encrypted container.
  virtual StorageContainerType GetType() const = 0;
  // Resets the backing storage of the container. While Purge removes the
  // entire container, Reset() set the container back to a pristine condition
  // doesn't require the backing storage to be set up again.
  virtual bool Reset() = 0;
  // Marks the container for lazy teardown; once the last reference to the
  // container is dropped, the constructs of the container are automatically
  // torn down and the container can be safely purged afterwards.
  virtual bool SetLazyTeardownWhenUnused() { return false; }
  virtual bool IsLazyTeardownSupported() const { return false; }
  // Returns the container location if any.
  virtual base::FilePath GetPath() const = 0;
  // Returns the backing location if any.
  virtual base::FilePath GetBackingLocation() const = 0;

  static bool IsMigratingType(StorageContainerType type) {
    return type == StorageContainerType::kEcryptfsToFscrypt ||
           type == StorageContainerType::kEcryptfsToDmcrypt ||
           type == StorageContainerType::kFscryptToDmcrypt;
  }
};

}  // namespace libstorage

#endif  // LIBSTORAGE_STORAGE_CONTAINER_STORAGE_CONTAINER_H_
