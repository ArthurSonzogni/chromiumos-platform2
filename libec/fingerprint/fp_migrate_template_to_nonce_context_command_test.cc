// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_migrate_template_to_nonce_context_command.h"

#include <string>
#include <tuple>

#include <base/containers/span.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace ec {
namespace {

using ::testing::ElementsAreArray;
using ::testing::Return;

TEST(FpMigrateTemplateToNonceContextCommand, IncorrectUserIdFormat) {
  const std::string user_id = "hello";

  auto cmd = FpMigrateTemplateToNonceContextCommand::Create(user_id);
  EXPECT_EQ(cmd, nullptr);
}

TEST(FpMigrateTemplateToNonceContextCommand,
     FpMigrateTemplateToNonceContextCommand) {
  const std::string user_id(64, 'a');
  const brillo::Blob decoded(32, 0xaa);

  auto cmd = FpMigrateTemplateToNonceContextCommand::Create(user_id);
  ASSERT_NE(cmd, nullptr);
  EXPECT_EQ(cmd->Version(), 0);
  EXPECT_EQ(cmd->Command(), EC_CMD_FP_MIGRATE_TEMPLATE_TO_NONCE_CONTEXT);
  auto req_user_id = base::as_byte_span(cmd->Req()->userid);
  EXPECT_THAT(std::make_tuple(req_user_id.data(), req_user_id.size()),
              ElementsAreArray(decoded));
}

TEST(FpMigrateTemplateToNonceContextCommand, HexStringToBytesSuccess) {
  const std::string hex = "deadbeef";
  const brillo::Blob expected_decoded{0xde, 0xad, 0xbe, 0xef};

  brillo::Blob decoded;
  ASSERT_TRUE(FpMigrateTemplateToNonceContextCommand::HexStringToBytes(
      hex, 4, decoded));
  EXPECT_EQ(decoded, expected_decoded);
}

TEST(FpMigrateTemplateToNonceContextCommand, HexStringToBytesTruncated) {
  const std::string hex = "deadbeef";
  const brillo::Blob expected_decoded{0xde, 0xad};

  brillo::Blob decoded;
  ASSERT_TRUE(FpMigrateTemplateToNonceContextCommand::HexStringToBytes(
      hex, 2, decoded));
  EXPECT_EQ(decoded, expected_decoded);
}

TEST(FpMigrateTemplateToNonceContextCommand, HexStringToBytesInvalidInput) {
  const std::string hex = "hello!";

  brillo::Blob decoded;
  EXPECT_FALSE(FpMigrateTemplateToNonceContextCommand::HexStringToBytes(
      hex, 6, decoded));
}

}  // namespace
}  // namespace ec
