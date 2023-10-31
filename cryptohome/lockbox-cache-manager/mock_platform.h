// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_LOCKBOX_CACHE_MANAGER_MOCK_PLATFORM_H_
#define CRYPTOHOME_LOCKBOX_CACHE_MANAGER_MOCK_PLATFORM_H_

#include "cryptohome/lockbox-cache-manager/platform.h"

#include <string>
#include <vector>

#include <gmock/gmock.h>

namespace cryptohome {
class MockPlatform : public Platform {
 public:
  MockPlatform() = default;
  virtual ~MockPlatform() = default;
  MOCK_METHOD(bool, IsOwnedByRoot, (const std::string&), (override));
  MOCK_METHOD(bool,
              GetAppOutputAndError,
              (const std::vector<std::string>&, std::string*),
              (override));
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_LOCKBOX_CACHE_MANAGER_MOCK_PLATFORM_H_
