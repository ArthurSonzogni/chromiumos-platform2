// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "trunks/mock_authorization_delegate.h"
#include "trunks/multiple_authorization_delegate.h"

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace trunks {

class MultipleAuthorizationTest : public testing::Test {
 public:
  ~MultipleAuthorizationTest() override = default;
  void SetUp() override {
    authorizations_.AddAuthorizationDelegate(&mock_delegate1_);
    authorizations_.AddAuthorizationDelegate(&mock_delegate2_);
  }

 protected:
  MultipleAuthorizations authorizations_;
  StrictMock<MockAuthorizationDelegate> mock_delegate1_;
  StrictMock<MockAuthorizationDelegate> mock_delegate2_;
};

TEST_F(MultipleAuthorizationTest, GetCommandAuthorization) {
  const std::string command_hash = "command_hash";
  const bool is_command_parameter_encryption_possible = true;
  const bool is_response_parameter_encryption_possible = false;

  const std::string output1 = "output1";
  const std::string output2 = "output2";
  const std::string expected_output = output1 + output2;

  EXPECT_CALL(mock_delegate1_,
              GetCommandAuthorization(
                  command_hash, is_command_parameter_encryption_possible,
                  is_response_parameter_encryption_possible, _))
      .WillOnce(DoAll(SetArgPointee<3>(output1), Return(true)));
  EXPECT_CALL(mock_delegate2_,
              GetCommandAuthorization(
                  command_hash, is_command_parameter_encryption_possible,
                  is_response_parameter_encryption_possible, _))
      .WillOnce(DoAll(SetArgPointee<3>(output2), Return(true)));

  std::string output;
  EXPECT_TRUE(authorizations_.GetCommandAuthorization(
      command_hash, is_command_parameter_encryption_possible,
      is_response_parameter_encryption_possible, &output));
  EXPECT_EQ(output, expected_output);
}

TEST_F(MultipleAuthorizationTest, GetCommandAuthorizationFailure) {
  const std::string command_hash = "command_hash";
  const bool is_command_parameter_encryption_possible = true;
  const bool is_response_parameter_encryption_possible = false;

  EXPECT_CALL(mock_delegate1_,
              GetCommandAuthorization(
                  command_hash, is_command_parameter_encryption_possible,
                  is_response_parameter_encryption_possible, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_delegate2_,
              GetCommandAuthorization(
                  command_hash, is_command_parameter_encryption_possible,
                  is_response_parameter_encryption_possible, _))
      .WillOnce(Return(false));

  std::string output;
  EXPECT_FALSE(authorizations_.GetCommandAuthorization(
      command_hash, is_command_parameter_encryption_possible,
      is_response_parameter_encryption_possible, &output));
}

TEST_F(MultipleAuthorizationTest, CheckResponseAuthorization) {
  std::string auth_response1(
      "\x00\x01\x00"  // nonceTpm = one byte buffer
      "\x01"          // session_attributes = continueSession
      "\x00\x00",     // hmac = zero length buffer
      6);
  std::string auth_response2(
      "\x00\x00"   // nonceTpm = zero length buffer
      "\x01"       // session_attributes = continueSession
      "\x00\x00",  // hmac = zero length buffer
      5);
  std::string auth_response = auth_response1 + auth_response2;

  EXPECT_CALL(mock_delegate1_, CheckResponseAuthorization(_, auth_response1))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_delegate2_, CheckResponseAuthorization(_, auth_response2))
      .WillOnce(Return(true));

  std::string response_hash;
  EXPECT_TRUE(
      authorizations_.CheckResponseAuthorization(response_hash, auth_response));
}

TEST_F(MultipleAuthorizationTest, CheckResponseAuthorizationFailure) {
  std::string auth_response(
      "\x00\x00"   // nonceTpm = zero length buffer
      "\x01"       // session_attributes = continueSession
      "\x00\x00",  // hmac = zero length buffer
      5);

  EXPECT_CALL(mock_delegate1_, CheckResponseAuthorization(_, auth_response))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_delegate2_, CheckResponseAuthorization(_, _))
      .WillOnce(Return(false));

  std::string response_hash;
  EXPECT_FALSE(
      authorizations_.CheckResponseAuthorization(response_hash, auth_response));
}

TEST_F(MultipleAuthorizationTest, EncryptCommandParameter) {
  EXPECT_CALL(mock_delegate1_, EncryptCommandParameter(_))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_delegate2_, EncryptCommandParameter(_))
      .WillOnce(Return(true));
  std::string fake_parameter = "fake_parameter";
  EXPECT_TRUE(authorizations_.EncryptCommandParameter(&fake_parameter));
}

TEST_F(MultipleAuthorizationTest, EncryptCommandParameterFailure) {
  EXPECT_CALL(mock_delegate1_, EncryptCommandParameter(_))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_delegate2_, EncryptCommandParameter(_))
      .WillOnce(Return(false));
  std::string fake_parameter = "fake_parameter";
  EXPECT_FALSE(authorizations_.EncryptCommandParameter(&fake_parameter));
}

TEST_F(MultipleAuthorizationTest, DecryptResponseParameter) {
  EXPECT_CALL(mock_delegate1_, DecryptResponseParameter(_))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_delegate2_, DecryptResponseParameter(_))
      .WillOnce(Return(true));
  std::string fake_parameter = "fake_parameter";
  EXPECT_TRUE(authorizations_.DecryptResponseParameter(&fake_parameter));
}

TEST_F(MultipleAuthorizationTest, DecryptResponseParameterFailure) {
  EXPECT_CALL(mock_delegate1_, DecryptResponseParameter(_))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_delegate2_, DecryptResponseParameter(_))
      .WillOnce(Return(false));
  std::string fake_parameter = "fake_parameter";
  EXPECT_FALSE(authorizations_.DecryptResponseParameter(&fake_parameter));
}

}  // namespace trunks
