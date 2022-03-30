// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>
#include <set>
#include <utility>

#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <dbus/cryptohome/dbus-constants.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/status/status_chain.h>

#include "cryptohome/error/cryptohome_mount_error.h"

namespace cryptohome {

namespace error {

class CryptohomeMountErrorTest : public ::testing::Test {};

namespace {

using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::StatusChain;

constexpr int kTestLocation1 = 1001;

TEST_F(CryptohomeMountErrorTest, BasicConstruction) {
  auto err1 = MakeStatus<CryptohomeMountError>(
      kTestLocation1, ErrorActionSet({ErrorAction::kResumePreviousMigration}),
      MountError::MOUNT_ERROR_KEY_FAILURE,
      user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);

  ASSERT_FALSE(err1.ok());
  EXPECT_EQ(err1->local_location(), kTestLocation1);
  EXPECT_EQ(err1->local_actions(),
            ErrorActionSet({ErrorAction::kResumePreviousMigration}));
  EXPECT_EQ(
      err1->local_legacy_error().value(),
      user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
  EXPECT_EQ(err1->mount_error(), MountError::MOUNT_ERROR_KEY_FAILURE);
}

TEST_F(CryptohomeMountErrorTest, Success) {
  StatusChain<CryptohomeMountError> err;
  EXPECT_TRUE(err.ok());
}

}  // namespace

}  // namespace error

}  // namespace cryptohome
