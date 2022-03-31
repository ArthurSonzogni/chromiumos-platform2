// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/real_command_parser.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "trunks/tpm_generated.h"

namespace trunks {

// A placeholder test fixture.
class RealCommandParserTest : public testing::Test {
 protected:
  RealCommandParser parser_;
};

namespace {

std::string ResizeSerializedBuffer(std::string command, int resize) {
  const int new_size = static_cast<int>(command.size()) + resize;
  if (new_size < kHeaderSize) {
    return "";
  }
  // Copy the "tag" part.
  std::string buffer = command.substr(0, sizeof(TPMI_ST_COMMAND_TAG));

  // Append `new_size` to `buffer`.
  if (Serialize_UINT32(new_size, &buffer) != TPM_RC_SUCCESS) {
    return "";
  }
  // Skip "tag" and "size" in the original command.
  buffer += command.substr(buffer.size(), std::string::npos);
  // Resize the whole buffer to `new_size`.
  buffer.resize(new_size, '\0');
  return buffer;
}

TEST_F(RealCommandParserTest, ParseHeaderSuccessHeaderOnlyCommand) {
  std::string command;
  TPMI_ST_COMMAND_TAG tag = TPM_ST_SESSIONS;
  UINT32 size = 0;
  TPM_CC cc = TPM_CC_FIRST;
  // TPM2_GetTestResult has no either handles or parameters.
  ASSERT_EQ(
      Tpm::SerializeCommand_GetTestResult(&command,
                                          /*authorization_delegate=*/nullptr),
      TPM_RC_SUCCESS);

  EXPECT_EQ(parser_.ParseHeader(&command, &tag, &size, &cc), TPM_RC_SUCCESS);
  EXPECT_EQ(tag, TPM_ST_NO_SESSIONS);
  EXPECT_EQ(size, kHeaderSize);
  EXPECT_EQ(cc, TPM_CC_GetTestResult);
}

TEST_F(RealCommandParserTest, ParseHeaderSuccessCommandWithPayload) {
  std::string command;
  TPMI_ST_COMMAND_TAG tag = TPM_ST_SESSIONS;
  UINT32 size = 0;
  TPM_CC cc = TPM_CC_FIRST;
  TPMI_YES_NO full_test = {};
  ASSERT_EQ(Tpm::SerializeCommand_SelfTest(full_test, &command,
                                           /*authorization_delegate=*/nullptr),
            TPM_RC_SUCCESS);

  EXPECT_EQ(parser_.ParseHeader(&command, &tag, &size, &cc), TPM_RC_SUCCESS);
  EXPECT_EQ(tag, TPM_ST_NO_SESSIONS);
  // TPM2_SelfTest has one parameter of TPMI_YES_NO type.
  EXPECT_EQ(size, kHeaderSize + sizeof(TPMI_YES_NO));
  EXPECT_EQ(cc, TPM_CC_SelfTest);
}

TEST_F(RealCommandParserTest, ParseHeaderFailureHeaderCommandTooShort) {
  std::string command;
  TPMI_ST_COMMAND_TAG tag = TPM_ST_SESSIONS;
  UINT32 size = 0;
  TPM_CC cc = TPM_CC_FIRST;
  // TPM2_GetTestResult has no either handles or parameters.
  ASSERT_EQ(
      Tpm::SerializeCommand_GetTestResult(&command,
                                          /*authorization_delegate=*/nullptr),
      TPM_RC_SUCCESS);

  // Make the command too short.
  command.pop_back();
  EXPECT_EQ(parser_.ParseHeader(&command, &tag, &size, &cc),
            TPM_RC_COMMAND_SIZE);
}

TEST_F(RealCommandParserTest, ParseHeaderFailureHeaderNoSize) {
  std::string command;
  TPMI_ST_COMMAND_TAG tag = TPM_ST_SESSIONS;
  UINT32 size = 0;
  TPM_CC cc = TPM_CC_FIRST;
  // TPM2_GetTestResult has no either handles or parameters.
  ASSERT_EQ(
      Tpm::SerializeCommand_GetTestResult(&command,
                                          /*authorization_delegate=*/nullptr),
      TPM_RC_SUCCESS);

  // Make the command too short.
  command.resize(sizeof(tag));
  EXPECT_EQ(parser_.ParseHeader(&command, &tag, &size, &cc),
            TPM_RC_INSUFFICIENT);
}

TEST_F(RealCommandParserTest, ParseHeaderFailureEmpty) {
  std::string command;
  TPMI_ST_COMMAND_TAG tag = TPM_ST_SESSIONS;
  UINT32 size = 0;
  TPM_CC cc = TPM_CC_FIRST;
  EXPECT_EQ(parser_.ParseHeader(&command, &tag, &size, &cc),
            TPM_RC_INSUFFICIENT);
}

TEST_F(RealCommandParserTest, ParseHeaderFailurePayloadTooLong) {
  std::string command;
  TPMI_ST_COMMAND_TAG tag = TPM_ST_SESSIONS;
  UINT32 size = 0;
  TPM_CC cc = TPM_CC_FIRST;
  TPMI_YES_NO full_test = {};
  ASSERT_EQ(Tpm::SerializeCommand_SelfTest(full_test, &command,
                                           /*authorization_delegate=*/nullptr),
            TPM_RC_SUCCESS);

  // Make the command too long.
  command += 'X';

  EXPECT_EQ(parser_.ParseHeader(&command, &tag, &size, &cc),
            TPM_RC_COMMAND_SIZE);
}

TEST_F(RealCommandParserTest, ParseHeaderFailurePayloadTooShort) {
  std::string command;
  TPMI_ST_COMMAND_TAG tag = TPM_ST_SESSIONS;
  UINT32 size = 0;
  TPM_CC cc = TPM_CC_FIRST;
  TPMI_YES_NO full_test = {};
  ASSERT_EQ(Tpm::SerializeCommand_SelfTest(full_test, &command,
                                           /*authorization_delegate=*/nullptr),
            TPM_RC_SUCCESS);

  // Make the command too short.
  command.pop_back();

  EXPECT_EQ(parser_.ParseHeader(&command, &tag, &size, &cc),
            TPM_RC_COMMAND_SIZE);
}

TEST_F(RealCommandParserTest, ParseHeaderFailureBadTag) {
  std::string command;
  TPMI_ST_COMMAND_TAG tag = TPM_ST_SESSIONS;
  UINT32 size = 0;
  TPM_CC cc = TPM_CC_FIRST;
  // TPM2_GetTestResult has no either handles or parameters.
  ASSERT_EQ(
      Tpm::SerializeCommand_GetTestResult(&command,
                                          /*authorization_delegate=*/nullptr),
      TPM_RC_SUCCESS);

  // Replace the data with a bad tag.
  std::string bad_tag;
  ASSERT_EQ(Serialize_TPMI_ST_COMMAND_TAG(TPM_ST_NULL, &bad_tag),
            TPM_RC_SUCCESS);
  for (int i = 0; i < bad_tag.size(); ++i) {
    command[i] = bad_tag[i];
  }

  EXPECT_EQ(parser_.ParseHeader(&command, &tag, &size, &cc), TPM_RC_BAD_TAG);
}

TEST_F(RealCommandParserTest, ParseCommandGetCapabilitySuccess) {
  std::string command;
  const TPM_CAP cap = TPM_CAP_HANDLES;
  const UINT32 fake_property = 123;
  const UINT32 property_count = 1;
  ASSERT_EQ(Tpm::SerializeCommand_GetCapability(
                cap, fake_property, property_count, &command,
                /*authorization_delegate=*/nullptr),
            TPM_RC_SUCCESS);
  TPM_CAP cap_out = TPM_CAP_FIRST;
  UINT32 property_out = 0;
  UINT32 property_count_out = 0;
  EXPECT_EQ(parser_.ParseCommandGetCapability(&command, &cap_out, &property_out,
                                              &property_count_out),
            TPM_RC_SUCCESS);
  EXPECT_EQ(cap_out, cap);
  EXPECT_EQ(property_out, fake_property);
  EXPECT_EQ(property_count_out, property_count);
}

TEST_F(RealCommandParserTest, ParseCommandGetCapabilityFailureWrongHeader) {
  std::string command;
  const TPM_CAP cap = TPM_CAP_HANDLES;
  const UINT32 fake_property = 123;
  const UINT32 property_count = 1;
  ASSERT_EQ(Tpm::SerializeCommand_GetCapability(
                cap, fake_property, property_count, &command,
                /*authorization_delegate=*/nullptr),
            TPM_RC_SUCCESS);

  // Breaks the tag.
  command[0] = ~command[0];

  TPM_CAP cap_out = TPM_CAP_FIRST;
  UINT32 property_out = 0;
  UINT32 property_count_out = 0;
  EXPECT_NE(parser_.ParseCommandGetCapability(&command, &cap_out, &property_out,
                                              &property_count_out),
            TPM_RC_SUCCESS);
}

TEST_F(RealCommandParserTest,
       ParseCommandGetCapabilityFailureParamNoPropertyCount) {
  std::string command;
  const TPM_CAP cap = TPM_CAP_HANDLES;
  const UINT32 fake_property = 123;
  const UINT32 property_count = 1;
  ASSERT_EQ(Tpm::SerializeCommand_GetCapability(
                cap, fake_property, property_count, &command,
                /*authorization_delegate=*/nullptr),
            TPM_RC_SUCCESS);

  command = ResizeSerializedBuffer(command, -4);

  TPM_CAP cap_out = TPM_CAP_FIRST;
  UINT32 property_out = 0;
  UINT32 property_count_out = 0;
  EXPECT_EQ(parser_.ParseCommandGetCapability(&command, &cap_out, &property_out,
                                              &property_count_out),
            TPM_RC_INSUFFICIENT);
}

TEST_F(RealCommandParserTest, ParseCommandGetCapabilityFailureParamNoProperty) {
  std::string command;
  const TPM_CAP cap = TPM_CAP_HANDLES;
  const UINT32 fake_property = 123;
  const UINT32 property_count = 1;
  ASSERT_EQ(Tpm::SerializeCommand_GetCapability(
                cap, fake_property, property_count, &command,
                /*authorization_delegate=*/nullptr),
            TPM_RC_SUCCESS);

  command = ResizeSerializedBuffer(command, -8);

  TPM_CAP cap_out = TPM_CAP_FIRST;
  UINT32 property_out = 0;
  UINT32 property_count_out = 0;
  EXPECT_EQ(parser_.ParseCommandGetCapability(&command, &cap_out, &property_out,
                                              &property_count_out),
            TPM_RC_INSUFFICIENT);
}

TEST_F(RealCommandParserTest, ParseCommandGetCapabilityFailureParamNoCap) {
  std::string command;
  const TPM_CAP cap = TPM_CAP_HANDLES;
  const UINT32 fake_property = 123;
  const UINT32 property_count = 1;
  ASSERT_EQ(Tpm::SerializeCommand_GetCapability(
                cap, fake_property, property_count, &command,
                /*authorization_delegate=*/nullptr),
            TPM_RC_SUCCESS);

  command = ResizeSerializedBuffer(command, -12);

  TPM_CAP cap_out = TPM_CAP_FIRST;
  UINT32 property_out = 0;
  UINT32 property_count_out = 0;
  EXPECT_EQ(parser_.ParseCommandGetCapability(&command, &cap_out, &property_out,
                                              &property_count_out),
            TPM_RC_INSUFFICIENT);
}

TEST_F(RealCommandParserTest, ParseCommandGetCapabilityFailureParamTooLong) {
  std::string command;
  const TPM_CAP cap = TPM_CAP_HANDLES;
  const UINT32 fake_property = 123;
  const UINT32 property_count = 1;
  ASSERT_EQ(Tpm::SerializeCommand_GetCapability(
                cap, fake_property, property_count, &command,
                /*authorization_delegate=*/nullptr),
            TPM_RC_SUCCESS);

  command = ResizeSerializedBuffer(command, 2);

  TPM_CAP cap_out = TPM_CAP_FIRST;
  UINT32 property_out = 0;
  UINT32 property_count_out = 0;
  EXPECT_EQ(parser_.ParseCommandGetCapability(&command, &cap_out, &property_out,
                                              &property_count_out),
            TPM_RC_SIZE);
}

}  // namespace

}  // namespace trunks
