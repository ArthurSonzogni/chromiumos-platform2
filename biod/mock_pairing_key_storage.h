// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_MOCK_PAIRING_KEY_STORAGE_H_
#define BIOD_MOCK_PAIRING_KEY_STORAGE_H_

#include "biod/pairing_key_storage.h"

#include <optional>
#include <string>

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

namespace biod {

class MockPairingKeyStorage : public PairingKeyStorage {
 public:
  MockPairingKeyStorage() = default;
  ~MockPairingKeyStorage() override = default;

  MOCK_METHOD(bool, PairingKeyExists, (), (override));
  MOCK_METHOD(std::optional<brillo::Blob>,
              ReadWrappedPairingKey,
              (),
              (override));
  MOCK_METHOD(bool, WriteWrappedPairingKey, (const brillo::Blob&), (override));
};

}  // namespace biod

#endif  // BIOD_MOCK_PAIRING_KEY_STORAGE_H_
