// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_get_nonce_command.h"

#include <algorithm>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace ec {
namespace {

using ::testing::Return;

TEST(FpGetNonceCommand, FpGetNonceCommand) {
  FpGetNonceCommand cmd;
  EXPECT_EQ(cmd.Version(), 0);
  EXPECT_EQ(cmd.Command(), EC_CMD_FP_GENERATE_NONCE);
}

// Mock the underlying EcCommand to test.
class FpGetNonceCommandTest : public testing::Test {
 public:
  class MockFpGetNonceCommand : public FpGetNonceCommand {
   public:
    using FpGetNonceCommand::FpGetNonceCommand;
    MOCK_METHOD(struct ec_response_fp_generate_nonce*,
                Resp,
                (),
                (const, override));
  };
};

TEST_F(FpGetNonceCommandTest, Success) {
  const brillo::Blob nonce(32, 1);
  static_assert(sizeof(std::declval<ec_response_fp_generate_nonce>().nonce) ==
                32);

  MockFpGetNonceCommand mock_command;
  struct ec_response_fp_generate_nonce response {};
  std::copy(nonce.begin(), nonce.end(), response.nonce);

  EXPECT_CALL(mock_command, Resp).WillRepeatedly(Return(&response));

  EXPECT_EQ(mock_command.Nonce(), nonce);
}

}  // namespace
}  // namespace ec
