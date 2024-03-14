// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CLEANUP_MOCK_LOW_DISK_SPACE_HANDLER_H_
#define CRYPTOHOME_CLEANUP_MOCK_LOW_DISK_SPACE_HANDLER_H_

#include <cstdint>
#include <string>

#include <base/functional/bind.h>
#include <base/time/time.h>
#include <gmock/gmock.h>

#include "cryptohome/cleanup/low_disk_space_handler.h"
#include "cryptohome/signalling.h"
#include "cryptohome/util/async_init.h"

namespace cryptohome {

class MockLowDiskSpaceHandler : public LowDiskSpaceHandler {
 public:
  MockLowDiskSpaceHandler()
      : LowDiskSpaceHandler(nullptr,
                            nullptr,
                            AsyncInitPtr<SignallingInterface>(nullptr),
                            nullptr) {}

  MOCK_METHOD(
      bool,
      Init,
      (base::RepeatingCallback<bool(
           const base::Location&, base::OnceClosure, const base::TimeDelta&)>),
      (override));
  MOCK_METHOD(void,
              SetUpdateUserActivityTimestampCallback,
              (const base::RepeatingCallback<void()>&),
              (override));
  MOCK_METHOD(void, Stop, (), (override));
  MOCK_METHOD(DiskCleanup*, disk_cleanup, (), (override, const));
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CLEANUP_MOCK_LOW_DISK_SPACE_HANDLER_H_
