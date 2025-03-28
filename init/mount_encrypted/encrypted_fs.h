// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_MOUNT_ENCRYPTED_ENCRYPTED_FS_H_
#define INIT_MOUNT_ENCRYPTED_ENCRYPTED_FS_H_

#include <inttypes.h>
#include <sys/stat.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <brillo/brillo_export.h>
#include <brillo/secure_blob.h>
#include <libstorage/platform/platform.h>
#include <libstorage/storage_container/filesystem_key.h>
#include <libstorage/storage_container/storage_container.h>
#include <libstorage/storage_container/storage_container_factory.h>

namespace mount_encrypted {

// Teardown stage: for granular teardowns
enum class TeardownStage {
  kTeardownUnbind,
  kTeardownContainer,
};

// BindMount represents a bind mount to be setup from
// source directories within the encrypted mount.
// EncryptedFs is responsible for setting up the bind mount
// once it sets up the encrypted mount.
struct BindMount {
  base::FilePath src;  // Location of bind source.
  base::FilePath dst;  // Destination of bind.
  uid_t owner;
  gid_t group;
  mode_t mode;
  bool submount;  // Submount is bound already.
};

// EncryptedFs sets up, tears down and cleans up encrypted
// stateful mounts. Given a root directory, the class
// sets up an encrypted mount at <root_dir>/ENCRYPTED_MOUNT.
class BRILLO_EXPORT EncryptedFs {
 public:
  // Set up the encrypted filesystem..
  EncryptedFs(const base::FilePath& rootdir,
              const base::FilePath& statefulmnt,
              uint64_t fs_size,
              const std::string& dmcrypt_name,
              std::unique_ptr<libstorage::StorageContainer> container,
              libstorage::Platform* platform);
  ~EncryptedFs() = default;

  static std::unique_ptr<EncryptedFs> Generate(
      const base::FilePath& rootdir,
      const base::FilePath& statefulmnt,
      libstorage::Platform* platform,
      libstorage::StorageContainerFactory* storage_container_factory);

  // Setup mounts the encrypted mount by:
  // 1. Create a sparse file at <rootdir>/STATEFUL_MNT/encrypted.block
  // 2. Mounting a loop device on top of the sparse file.
  // 3. Mounting a dmcrypt device with the loop device as the backing
  //    device and the provided encryption key.
  // 4. Formatting the dmcrypt device as ext4 and mounting it at the
  //    mount_point.
  // If a sparse file already exists, Setup assumes that the stateful
  // mount has already been setup and attempts to mount the
  // | ext4 | dmcrypt | loopback | tower on top of the sparse file.
  // Parameters
  //   encryption_key - dmcrypt encryption key.
  //   rebuild - cleanup and recreate the encrypted mount.
  bool Setup(const libstorage::FileSystemKey& encryption_key, bool rebuild);
  // Teardown - stepwise unmounts the | ext4 | dmcrypt | loopback | tower
  // on top of the sparse file.
  bool Teardown();
  // CheckStates - Checks validity for the stateful mount before mounting.
  bool CheckStates();
  // ReportInfo - Reports the paths and bind mounts.
  bool ReportInfo() const;

 private:
  friend class EncryptedFsTest;
  FRIEND_TEST(EncryptedFsTest, RebuildStateful);
  FRIEND_TEST(EncryptedFsTest, OldStateful);
  FRIEND_TEST(EncryptedFsTest, LoopdevTeardown);
  FRIEND_TEST(EncryptedFsTest, DevmapperTeardown);

  // TeardownByStage allows higher granularity over teardown
  // processes.
  bool TeardownByStage(TeardownStage stage, bool ignore_errors);

  // Root directory to use for the encrypted stateful filesystem.
  const base::FilePath rootdir_;
  // Size of the filesystem.
  const uint64_t fs_size_;

  // Dm-crypt device name: used for key finalization.
  const std::string dmcrypt_name_;

  // File paths used by encrypted stateful.
  const base::FilePath stateful_mount_;
  const base::FilePath dmcrypt_dev_;
  const base::FilePath encrypted_mount_;

  // Use a raw Platform pointer to avoid convoluted EXPECT_CALL semantics
  // for mock Platform objects.
  libstorage::Platform* platform_;

  // Encrypted container that will be mounted as the encrypted filesystem.
  std::unique_ptr<libstorage::StorageContainer> container_;

  std::vector<BindMount> bind_mounts_;
};

}  // namespace mount_encrypted

#endif  // INIT_MOUNT_ENCRYPTED_ENCRYPTED_FS_H_
