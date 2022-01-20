// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef U2FD_SIGN_MANAGER_MOCK_SIGN_MANAGER_H_
#define U2FD_SIGN_MANAGER_MOCK_SIGN_MANAGER_H_

#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "u2fd/sign_manager/sign_manager.h"

namespace u2f {

class MockSignManager : public SignManager {
 public:
  MockSignManager() = default;
  ~MockSignManager() override = default;

  MOCK_METHOD(bool, IsReady, (), (override));
  MOCK_METHOD(bool,
              CreateKey,
              (KeyType key_type,
               const brillo::SecureBlob& auth_data,
               std::string* key_blob,
               std::vector<uint8_t>* public_key),
              (override));
  MOCK_METHOD(bool,
              Sign,
              (const std::string& key_blob,
               const std::string& data_to_sign,
               const brillo::SecureBlob& auth_data,
               std::string* signature),
              (override));
};

}  // namespace u2f

#endif  // U2FD_SIGN_MANAGER_MOCK_SIGN_MANAGER_H_
