// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_CRYPTOHOME_KEYS_MANAGER_H_
#define CRYPTOHOME_MOCK_CRYPTOHOME_KEYS_MANAGER_H_

#include "cryptohome/cryptohome_keys_manager.h"

#include <gmock/gmock.h>

#include "cryptohome/mock_cryptohome_key_loader.h"

namespace cryptohome {

class MockCryptohomeKeysManager : public CryptohomeKeysManager {
 public:
  MockCryptohomeKeysManager();

  ~MockCryptohomeKeysManager() = default;

  MOCK_METHOD(void, Init, (), (override));
  MOCK_METHOD(bool, ReloadAllCryptohomeKeys, (), (override));
  MOCK_METHOD(bool, HasAnyCryptohomeKey, (), (override));
  MOCK_METHOD(CryptohomeKeyLoader*,
              GetKeyLoader,
              (CryptohomeKeyType),
              (override));

  // A helper to get the testing purpose loader.
  MockCryptohomeKeyLoader* get_mock_cryptohome_key_loader() {
    return &mock_cryptohome_key_loader_;
  }

 private:
  ::testing::NiceMock<MockCryptohomeKeyLoader> mock_cryptohome_key_loader_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_CRYPTOHOME_KEYS_MANAGER_H_
