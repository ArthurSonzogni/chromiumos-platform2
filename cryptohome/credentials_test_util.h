// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CREDENTIALS_TEST_UTIL_H_
#define CRYPTOHOME_CREDENTIALS_TEST_UTIL_H_

#include <gmock/gmock.h>

namespace cryptohome {

MATCHER_P(CredentialsMatcher, creds, "") {
  if (creds.username() != arg.username()) {
    return false;
  }
  if (creds.passkey() != arg.passkey()) {
    return false;
  }
  return true;
}

}  // namespace cryptohome

#endif  // CRYPTOHOME_CREDENTIALS_TEST_UTIL_H_
