// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libec/ec_command.h"
#include "libec/fingerprint/fp_encryption_status_command.h"

using testing::Return;

namespace ec {
namespace {

TEST(FpEncryptionStatusCommand, FpEncryptionStatusCommand) {
  FpEncryptionStatusCommand cmd;
  EXPECT_EQ(cmd.Version(), 0);
  EXPECT_EQ(cmd.Command(), EC_CMD_FP_ENC_STATUS);
}

// Mock the underlying EcCommand to test
class FpEncryptionStatusCommandTest : public testing::Test {
 public:
  class MockFpEncryptionStatusCommand : public FpEncryptionStatusCommand {
   public:
    using FpEncryptionStatusCommand::FpEncryptionStatusCommand;
    MOCK_METHOD(const ec_response_fp_encryption_status*,
                Resp,
                (),
                (const, override));
  };
};

TEST_F(FpEncryptionStatusCommandTest, FpEncStatusSet) {
  MockFpEncryptionStatusCommand mock_command;
  struct ec_response_fp_encryption_status response = {
      .valid_flags = FP_ENC_STATUS_SEED_SET, .status = FP_ENC_STATUS_SEED_SET};
  EXPECT_CALL(mock_command, Resp).WillRepeatedly(Return(&response));

  EXPECT_EQ(mock_command.GetValidFlags(), FP_ENC_STATUS_SEED_SET);
  EXPECT_EQ(mock_command.GetStatus(), FP_ENC_STATUS_SEED_SET);
}

TEST_F(FpEncryptionStatusCommandTest, FpEncStatusUnSet) {
  MockFpEncryptionStatusCommand mock_command;
  struct ec_response_fp_encryption_status response = {
      .valid_flags = FP_ENC_STATUS_SEED_SET, .status = 0};
  EXPECT_CALL(mock_command, Resp).WillRepeatedly(Return(&response));

  EXPECT_EQ(mock_command.GetValidFlags(), FP_ENC_STATUS_SEED_SET);
  EXPECT_EQ(mock_command.GetStatus(), 0);
}

}  // namespace
}  // namespace ec
