// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/real_response_serializer.h"

#include <algorithm>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "trunks/command_parser.h"
#include "trunks/password_authorization_delegate.h"
#include "trunks/tpm_generated.h"

namespace trunks {

namespace {

void InitializeFake(TPMS_CAPABILITY_DATA* data) {
  memset(data, 0, sizeof(*data));
  data->capability = TPM_CAP_HANDLES;
  for (int i = 0; i < 3; ++i) {
    data->data.handles.handle[data->data.handles.count] = i;
    ++data->data.handles.count;
  }
}

}  // namespace

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

TEST_F(RealResponseSerializerTest, SerializeResponseGetCapability) {
  const TPMI_YES_NO more = YES;
  TPMS_CAPABILITY_DATA data;
  InitializeFake(&data);
  std::string response;
  serializer_.SerializeResponseGetCapability(more, data, &response);

  TPMI_YES_NO more_out = NO;
  TPMS_CAPABILITY_DATA data_out = {};

  ASSERT_EQ(
      Tpm::ParseResponse_GetCapability(response, &more_out, &data_out,
                                       /*authorization_delegate=*/nullptr),
      TPM_RC_SUCCESS);
  EXPECT_EQ(more_out, more);
  EXPECT_EQ(memcmp(&data, &data_out, sizeof(data_out)), 0);
}

TEST_F(RealResponseSerializerTest, SerializeResponseNvRead) {
  const std::string fake_data = "fake data";
  const TPM2B_MAX_NV_BUFFER data = Make_TPM2B_MAX_NV_BUFFER(fake_data);

  std::string response;
  serializer_.SerializeResponseNvRead(data, &response);

  TPM2B_MAX_NV_BUFFER data_out = {};

  PasswordAuthorizationDelegate fake_password_authorization(
      "password placeholder");

  ASSERT_EQ(Tpm::ParseResponse_NV_Read(response, &data_out,
                                       &fake_password_authorization),
            TPM_RC_SUCCESS);
  EXPECT_EQ(std::string(data_out.buffer, data_out.buffer + data_out.size),
            fake_data);
}

}  // namespace

}  // namespace trunks
