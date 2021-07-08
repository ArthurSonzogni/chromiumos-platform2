// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/mock_cryptohome_key_loader.h"

using testing::_;
using testing::Return;

namespace cryptohome {

namespace {

const TpmKeyHandle kTestKeyHandle = 17;  // any non-zero value

}  // namespace

MockCryptohomeKeyLoader::MockCryptohomeKeyLoader()
    : CryptohomeKeyLoader(nullptr, nullptr, base::FilePath("")) {
  ON_CALL(*this, HasCryptohomeKey()).WillByDefault(Return(true));
  ON_CALL(*this, GetCryptohomeKey()).WillByDefault(Return(kTestKeyHandle));
  ON_CALL(*this, ReloadCryptohomeKey()).WillByDefault(Return(true));
  ON_CALL(*this, Init()).WillByDefault(Return());
}

MockCryptohomeKeyLoader::~MockCryptohomeKeyLoader() {}

}  // namespace cryptohome
