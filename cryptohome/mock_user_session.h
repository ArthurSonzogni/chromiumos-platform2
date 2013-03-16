// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_USER_SESSION_H_
#define CRYPTOHOME_MOCK_USER_SESSION_H_

#include "user_session.h"

#include <base/logging.h>
#include <chromeos/secure_blob.h>
#include <chromeos/utility.h>

#include <gmock/gmock.h>

namespace cryptohome {
using ::testing::_;
using ::testing::Invoke;

class MockUserSession : public UserSession {
 public:
  MockUserSession();
  ~MockUserSession();
  MOCK_METHOD1(Init, void(const chromeos::SecureBlob&));
  MOCK_METHOD1(SetUser, bool(const Credentials&));
  MOCK_METHOD0(Reset, void(void));
  MOCK_CONST_METHOD1(CheckUser, bool(const Credentials&));
  MOCK_CONST_METHOD1(Verify, bool(const Credentials&));
 private:
  UserSession user_session_;
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_USER_SESSION_H_
