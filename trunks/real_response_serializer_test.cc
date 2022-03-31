// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/real_response_serializer.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "trunks/command_parser.h"
#include "trunks/tpm_generated.h"

namespace trunks {

// A placeholder test fixture.
class RealResponseSerializerTest : public testing::Test {
 protected:
  RealResponseSerializer serializer_;
};

namespace {

TEST_F(RealResponseSerializerTest, SerializeHeaderOnlyResponse) {
  std::string response;
  const TPM_RC rc = TPM_RC_LOCKOUT;
  serializer_.SerializeHeaderOnlyResponse(rc, &response);

  TPMI_ST_COMMAND_TAG tag = TPM_ST_NULL;
  EXPECT_EQ(Parse_TPMI_ST_COMMAND_TAG(&response, &tag, nullptr),
            TPM_RC_SUCCESS);
  EXPECT_EQ(tag, TPM_ST_NO_SESSIONS);

  UINT32 size = 0;
  EXPECT_EQ(Parse_UINT32(&response, &size, nullptr), TPM_RC_SUCCESS);
  EXPECT_EQ(size, kHeaderSize);

  TPM_RC rc_out = TPM_RC_SUCCESS;
  EXPECT_EQ(Parse_TPM_RC(&response, &rc_out, nullptr), TPM_RC_SUCCESS);
  EXPECT_EQ(rc_out, rc);
}

TEST_F(RealResponseSerializerTest, SerializeHeaderOnlyResponseBadTag) {
  std::string response;
  const TPM_RC rc = TPM_RC_BAD_TAG;
  serializer_.SerializeHeaderOnlyResponse(rc, &response);

  TPMI_ST_COMMAND_TAG tag = TPM_ST_NULL;
  EXPECT_EQ(Parse_TPMI_ST_COMMAND_TAG(&response, &tag, nullptr),
            TPM_RC_SUCCESS);
  EXPECT_EQ(tag, TPM_ST_RSP_COMMAND);

  UINT32 size = 0;
  EXPECT_EQ(Parse_UINT32(&response, &size, nullptr), TPM_RC_SUCCESS);
  EXPECT_EQ(size, kHeaderSize);

  TPM_RC rc_out = TPM_RC_SUCCESS;
  EXPECT_EQ(Parse_TPM_RC(&response, &rc_out, nullptr), TPM_RC_SUCCESS);
  EXPECT_EQ(rc_out, rc);
}

}  // namespace

}  // namespace trunks
