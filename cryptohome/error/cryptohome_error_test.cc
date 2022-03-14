// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>
#include <set>
#include <utility>

#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/status/status_chain.h>

#include "cryptohome/error/action.h"
#include "cryptohome/error/cryptohome_error.h"

namespace cryptohome {

namespace error {

class CryptohomeErrorTest : public ::testing::Test {};

namespace {

using hwsec_foundation::status::MakeStatus;

constexpr int kTestLocation1 = 10001;
constexpr int kTestLocation2 = 10002;

TEST_F(CryptohomeErrorTest, LegacyCryptohomeErrorCode) {
  auto err1 = MakeStatus<CryptohomeError>(kTestLocation1, NoErrorAction());
  EXPECT_EQ(err1->local_legacy_error(), std::nullopt);

  auto err2 = MakeStatus<CryptohomeError>(
      kTestLocation2, NoErrorAction(),
      user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
  EXPECT_EQ(
      err2->local_legacy_error().value(),
      user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND);
}

TEST_F(CryptohomeErrorTest, BasicFields) {
  // This test checks that the basic fields that the error holds is working.
  // Basic fields as in location and actions.

  auto err1 = MakeStatus<CryptohomeError>(kTestLocation1, NoErrorAction());
  EXPECT_EQ(err1->local_location(), kTestLocation1);
  EXPECT_EQ(err1->local_actions().size(), 0);

  auto err2 = MakeStatus<CryptohomeError>(
      kTestLocation2,
      ErrorActionSet({ErrorAction::kRetry, ErrorAction::kPowerwash}));
  EXPECT_EQ(err2->local_location(), kTestLocation2);
  EXPECT_EQ(err2->local_actions(),
            std::set<CryptohomeError::Action>(
                {ErrorAction::kRetry, ErrorAction::kPowerwash}));
}

TEST_F(CryptohomeErrorTest, ToString) {
  auto err2 = MakeStatus<CryptohomeError>(
      kTestLocation2,
      ErrorActionSet({ErrorAction::kRetry, ErrorAction::kPowerwash}));

  std::stringstream ss;
  ss << "Loc: " << kTestLocation2 << " Actions: (";
  ss << static_cast<int>(ErrorAction::kRetry) << ", "
     << static_cast<int>(ErrorAction::kPowerwash) << ")";

  EXPECT_EQ(err2->ToString(), ss.str());
}

}  // namespace

}  // namespace error

}  // namespace cryptohome
