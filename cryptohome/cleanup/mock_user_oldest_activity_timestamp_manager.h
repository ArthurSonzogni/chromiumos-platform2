// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CLEANUP_MOCK_USER_OLDEST_ACTIVITY_TIMESTAMP_MANAGER_H_
#define CRYPTOHOME_CLEANUP_MOCK_USER_OLDEST_ACTIVITY_TIMESTAMP_MANAGER_H_

#include "cryptohome/cleanup/user_oldest_activity_timestamp_manager.h"

#include <string>

#include <base/time/time.h>

#include <gmock/gmock.h>

namespace cryptohome {

class MockUserOldestActivityTimestampManager
    : public UserOldestActivityTimestampManager {
 public:
  MockUserOldestActivityTimestampManager()
      : UserOldestActivityTimestampManager(nullptr) {}
  virtual ~MockUserOldestActivityTimestampManager() = default;

  MOCK_METHOD(void,
              LoadTimestampWithLegacy,
              (const std::string&, base::Time),
              (override));

  MOCK_METHOD(void, LoadTimestamp, (const std::string&), (override));
  MOCK_METHOD(bool,
              UpdateTimestamp,
              (const std::string&, base::TimeDelta time_shift),
              (override));

  MOCK_METHOD(void, RemoveUser, (const std::string&), (override));
  MOCK_METHOD(base::Time,
              GetLastUserActivityTimestamp,
              (const std::string&),
              (const, override));
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_CLEANUP_MOCK_USER_OLDEST_ACTIVITY_TIMESTAMP_MANAGER_H_
