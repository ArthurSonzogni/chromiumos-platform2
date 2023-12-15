// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_SIGNALLING_H_
#define CRYPTOHOME_MOCK_SIGNALLING_H_

#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <gmock/gmock.h>

#include "cryptohome/signalling.h"

namespace cryptohome {

class MockSignalling : public SignallingInterface {
 public:
  MockSignalling() = default;

  MockSignalling(const MockSignalling&) = delete;
  MockSignalling& operator=(const MockSignalling&) = delete;

  MOCK_METHOD(void,
              SendAuthenticateAuthFactorCompleted,
              (const user_data_auth::AuthenticateAuthFactorCompleted&),
              (override));
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_SIGNALLING_H_
