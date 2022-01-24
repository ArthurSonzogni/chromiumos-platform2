// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for AuthFactor.

#include "cryptohome/auth_factor/auth_factor.h"

#include <memory>
#include <string>
#include <utility>

#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

namespace cryptohome {

class AuthFactorTest : public ::testing::Test {
 public:
  AuthFactorTest() = default;
  AuthFactorTest(const AuthFactorTest&) = delete;
  AuthFactorTest& operator=(const AuthFactorTest&) = delete;
  ~AuthFactorTest() override = default;
};

}  // namespace cryptohome
