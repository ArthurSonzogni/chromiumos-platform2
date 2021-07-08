// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_CRYPTOHOME_KEY_LOADER_H_
#define CRYPTOHOME_MOCK_CRYPTOHOME_KEY_LOADER_H_

#include "cryptohome/cryptohome_key_loader.h"

#include <gmock/gmock.h>

namespace cryptohome {

class MockCryptohomeKeyLoader : public CryptohomeKeyLoader {
 public:
  MockCryptohomeKeyLoader();
  ~MockCryptohomeKeyLoader();

  MOCK_METHOD(bool, HasCryptohomeKey, (), (override));
  MOCK_METHOD(TpmKeyHandle, GetCryptohomeKey, (), (override));
  MOCK_METHOD(bool, ReloadCryptohomeKey, (), (override));
  MOCK_METHOD(void, Init, (), (override));
  MOCK_METHOD(bool, CreateCryptohomeKey, (brillo::SecureBlob*), (override));
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_CRYPTOHOME_KEY_LOADER_H_
