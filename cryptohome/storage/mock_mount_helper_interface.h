// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef CRYPTOHOME_STORAGE_MOCK_MOUNT_HELPER_INTERFACE_H_
#define CRYPTOHOME_STORAGE_MOCK_MOUNT_HELPER_INTERFACE_H_

#include "cryptohome/storage/mount_helper_interface.h"

#include <base/files/file_path.h>
#include <gmock/gmock.h>
#include <string>

namespace cryptohome {
class MockMounterHelperInterface : public MountHelperInterface {
 public:
  MockMounterHelperInterface() = default;
  ~MockMounterHelperInterface() = default;

  // Ephemeral mounts cannot be performed twice, so cryptohome needs to be able
  // to check whether an ephemeral mount can be performed.
  MOCK_METHOD(bool, CanPerformEphemeralMount, (), (const override));

  // Returns whether an ephemeral mount has been performed.
  MOCK_METHOD(bool, MountPerformed, (), (const override));

  // Returns whether |path| is currently mounted as part of the ephemeral mount.
  MOCK_METHOD(bool, IsPathMounted, (const base::FilePath&), (const override));

  // Carries out an ephemeral mount for user |username|.
  MOCK_METHOD(StorageStatus,
              PerformEphemeralMount,
              (const Username&, const base::FilePath&),
              (override));

  // Unmounts the mount point.
  // Returns whether an ephemeral mount has been performed.
  MOCK_METHOD(void, UnmountAll, (), (override));

  // Carries out mount operations for a regular cryptohome.
  // Carries out an ephemeral mount for user |username|.
  MOCK_METHOD(
      StorageStatus,
      PerformMount,
      (MountType, const Username&, const std::string&, const std::string&),
      (override));
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_STORAGE_MOCK_MOUNT_HELPER_INTERFACE_H_
