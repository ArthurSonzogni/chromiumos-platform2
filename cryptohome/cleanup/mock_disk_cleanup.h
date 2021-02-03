// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CLEANUP_MOCK_DISK_CLEANUP_H_
#define CRYPTOHOME_CLEANUP_MOCK_DISK_CLEANUP_H_

#include "cryptohome/cleanup/disk_cleanup.h"

#include <cstdint>
#include <string>

#include <gmock/gmock.h>

namespace cryptohome {

class MockDiskCleanup : public DiskCleanup {
 public:
  MockDiskCleanup();
  virtual ~MockDiskCleanup();

  MOCK_METHOD(base::Optional<int64_t>,
              AmountOfFreeDiskSpace,
              (),
              (override, const));
  MOCK_METHOD(DiskCleanup::FreeSpaceState,
              GetFreeDiskSpaceState,
              (base::Optional<int64_t>),
              (override, const));
  MOCK_METHOD(DiskCleanup::FreeSpaceState,
              GetFreeDiskSpaceState,
              (),
              (override, const));
  MOCK_METHOD(bool, HasTargetFreeSpace, (), (override, const));
  MOCK_METHOD(bool, IsFreeableDiskSpaceAvailable, (), (override));
  MOCK_METHOD(bool, FreeDiskSpace, (), (override));
  MOCK_METHOD(void, set_cleanup_threshold, (uint64_t), (override));
  MOCK_METHOD(void, set_aggressive_cleanup_threshold, (uint64_t), (override));
  MOCK_METHOD(void, set_target_free_space, (uint64_t), (override));
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CLEANUP_MOCK_DISK_CLEANUP_H_
