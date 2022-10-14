// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vtpm/commands/get_capability_command.h"

#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <trunks/mock_command_parser.h>
#include <trunks/mock_response_serializer.h>
#include <trunks/tpm_generated.h>

#include "vtpm/backends/mock_tpm_handle_manager.h"

namespace vtpm {

namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::Pointee;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;

constexpr char kFakeRequest[] = "fake request";
constexpr char kTestResponse[] = "test response";
constexpr trunks::TPM_HANDLE kFakeHandle = 123;
constexpr trunks::UINT32 kFakeRequestedPropertyCount = 3;

MATCHER_P(IsCapListOf, v, "") {
  if (arg.data.handles.count != v.size()) {
    return false;
  }
  for (int i = 0; i < v.size(); ++i) {
    if (arg.data.handles.handle[i] != v[i]) {
      return false;
    }
  }
  return true;
}

std::vector<trunks::TPM_HANDLE> MakeFakeFoundHandles(int size) {
  std::vector<trunks::TPM_HANDLE> handles;
  for (int i = 0; i < size; ++i) {
    handles.push_back(i);
  }
  return handles;
}

}  // namespace

// A placeholder test fixture.
class GetCapabilityCommandTest : public testing::Test {
 protected:
  StrictMock<trunks::MockCommandParser> mock_cmd_parser_;
  StrictMock<trunks::MockResponseSerializer> mock_resp_serializer_;
  StrictMock<MockTpmHandleManager> mock_tpm_handle_manager_;
  GetCapabilityCommand command_{&mock_cmd_parser_, &mock_resp_serializer_,
                                &mock_tpm_handle_manager_};
};

namespace {

TEST_F(GetCapabilityCommandTest, SuccessHasHandles) {
  std::string response;
  CommandResponseCallback callback =
      base::BindOnce([](std::string* resp_out,
                        const std::string& resp_in) { *resp_out = resp_in; },
                     &response);
  EXPECT_CALL(
      mock_cmd_parser_,
      ParseCommandGetCapability(Pointee(std::string(kFakeRequest)), _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(trunks::TPM_CAP_HANDLES),
                      SetArgPointee<2>(kFakeHandle),
                      SetArgPointee<3>(kFakeRequestedPropertyCount),
                      Return(trunks::TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_tpm_handle_manager_, IsHandleTypeSuppoerted(kFakeHandle))
      .WillOnce(Return(true));

  EXPECT_CALL(mock_tpm_handle_manager_, GetHandleList(kFakeHandle, _))
      .WillOnce(DoAll(
          SetArgPointee<1>(MakeFakeFoundHandles(kFakeRequestedPropertyCount)),
          Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(
      mock_resp_serializer_,
      SerializeResponseGetCapability(
          NO, IsCapListOf(MakeFakeFoundHandles(kFakeRequestedPropertyCount)),
          _))
      .WillOnce(SetArgPointee<2>(kTestResponse));

  command_.Run(kFakeRequest, std::move(callback));
  EXPECT_EQ(response, kTestResponse);
}

TEST_F(GetCapabilityCommandTest, SuccessHasLessHandles) {
  std::string response;
  CommandResponseCallback callback =
      base::BindOnce([](std::string* resp_out,
                        const std::string& resp_in) { *resp_out = resp_in; },
                     &response);
  EXPECT_CALL(
      mock_cmd_parser_,
      ParseCommandGetCapability(Pointee(std::string(kFakeRequest)), _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(trunks::TPM_CAP_HANDLES),
                      SetArgPointee<2>(kFakeHandle),
                      SetArgPointee<3>(kFakeRequestedPropertyCount),
                      Return(trunks::TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_tpm_handle_manager_, IsHandleTypeSuppoerted(kFakeHandle))
      .WillOnce(Return(true));

  // Make the handles found by `mock_tpm_handle_manager_` short by 1.
  EXPECT_CALL(mock_tpm_handle_manager_, GetHandleList(kFakeHandle, _))
      .WillOnce(DoAll(SetArgPointee<1>(MakeFakeFoundHandles(
                          kFakeRequestedPropertyCount - 1)),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(
      mock_resp_serializer_,
      SerializeResponseGetCapability(
          NO,
          IsCapListOf(MakeFakeFoundHandles(kFakeRequestedPropertyCount - 1)),
          _))
      .WillOnce(SetArgPointee<2>(kTestResponse));

  command_.Run(kFakeRequest, std::move(callback));
  EXPECT_EQ(response, kTestResponse);
}

TEST_F(GetCapabilityCommandTest, SuccessHasMoreHandles) {
  std::string response;
  CommandResponseCallback callback =
      base::BindOnce([](std::string* resp_out,
                        const std::string& resp_in) { *resp_out = resp_in; },
                     &response);
  EXPECT_CALL(
      mock_cmd_parser_,
      ParseCommandGetCapability(Pointee(std::string(kFakeRequest)), _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(trunks::TPM_CAP_HANDLES),
                      SetArgPointee<2>(kFakeHandle),
                      SetArgPointee<3>(kFakeRequestedPropertyCount),
                      Return(trunks::TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_tpm_handle_manager_, IsHandleTypeSuppoerted(kFakeHandle))
      .WillOnce(Return(true));

  // Double the size of the handles found by `mock_tpm_handle_manager_`.
  EXPECT_CALL(mock_tpm_handle_manager_, GetHandleList(kFakeHandle, _))
      .WillOnce(DoAll(SetArgPointee<1>(MakeFakeFoundHandles(
                          kFakeRequestedPropertyCount * 2)),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(
      mock_resp_serializer_,
      SerializeResponseGetCapability(
          YES, IsCapListOf(MakeFakeFoundHandles(kFakeRequestedPropertyCount)),
          _))
      .WillOnce(SetArgPointee<2>(kTestResponse));

  command_.Run(kFakeRequest, std::move(callback));
  EXPECT_EQ(response, kTestResponse);
}

TEST_F(GetCapabilityCommandTest, SuccessRequestTooManyHandles) {
  std::string response;
  CommandResponseCallback callback =
      base::BindOnce([](std::string* resp_out,
                        const std::string& resp_in) { *resp_out = resp_in; },
                     &response);
  EXPECT_CALL(
      mock_cmd_parser_,
      ParseCommandGetCapability(Pointee(std::string(kFakeRequest)), _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(trunks::TPM_CAP_HANDLES),
                      SetArgPointee<2>(kFakeHandle),
                      SetArgPointee<3>(MAX_CAP_HANDLES * 2),
                      Return(trunks::TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_tpm_handle_manager_, IsHandleTypeSuppoerted(kFakeHandle))
      .WillOnce(Return(true));

  // Make the handles found by `mock_tpm_handle_manager_` short by 1.
  EXPECT_CALL(mock_tpm_handle_manager_, GetHandleList(kFakeHandle, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(MakeFakeFoundHandles(MAX_CAP_HANDLES + 1)),
                Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(mock_resp_serializer_,
              SerializeResponseGetCapability(
                  YES, IsCapListOf(MakeFakeFoundHandles(MAX_CAP_HANDLES)), _))
      .WillOnce(SetArgPointee<2>(kTestResponse));

  command_.Run(kFakeRequest, std::move(callback));
  EXPECT_EQ(response, kTestResponse);
}

TEST_F(GetCapabilityCommandTest, SuccessRequestZeroHandles) {
  std::string response;
  CommandResponseCallback callback =
      base::BindOnce([](std::string* resp_out,
                        const std::string& resp_in) { *resp_out = resp_in; },
                     &response);
  EXPECT_CALL(
      mock_cmd_parser_,
      ParseCommandGetCapability(Pointee(std::string(kFakeRequest)), _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(trunks::TPM_CAP_HANDLES),
                      SetArgPointee<2>(kFakeHandle), SetArgPointee<3>(0),
                      Return(trunks::TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_tpm_handle_manager_, IsHandleTypeSuppoerted(kFakeHandle))
      .WillOnce(Return(true));

  EXPECT_CALL(mock_tpm_handle_manager_, GetHandleList(kFakeHandle, _))
      .WillOnce(DoAll(SetArgPointee<1>(MakeFakeFoundHandles(1)),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(mock_resp_serializer_,
              SerializeResponseGetCapability(
                  YES, IsCapListOf(MakeFakeFoundHandles(0)), _))
      .WillOnce(SetArgPointee<2>(kTestResponse));

  command_.Run(kFakeRequest, std::move(callback));
  EXPECT_EQ(response, kTestResponse);
}

TEST_F(GetCapabilityCommandTest, SuccessNoHandle) {
  std::string response;
  CommandResponseCallback callback =
      base::BindOnce([](std::string* resp_out,
                        const std::string& resp_in) { *resp_out = resp_in; },
                     &response);
  EXPECT_CALL(
      mock_cmd_parser_,
      ParseCommandGetCapability(Pointee(std::string(kFakeRequest)), _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(trunks::TPM_CAP_HANDLES),
                      SetArgPointee<2>(kFakeHandle),
                      SetArgPointee<3>(kFakeRequestedPropertyCount),
                      Return(trunks::TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_tpm_handle_manager_, IsHandleTypeSuppoerted(kFakeHandle))
      .WillOnce(Return(true));

  EXPECT_CALL(mock_tpm_handle_manager_, GetHandleList(kFakeHandle, _))
      .WillOnce(DoAll(SetArgPointee<1>(MakeFakeFoundHandles(0)),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(mock_resp_serializer_,
              SerializeResponseGetCapability(
                  NO, IsCapListOf(MakeFakeFoundHandles(0)), _))
      .WillOnce(SetArgPointee<2>(kTestResponse));

  command_.Run(kFakeRequest, std::move(callback));
  EXPECT_EQ(response, kTestResponse);
}

TEST_F(GetCapabilityCommandTest, SuccessNoHandleRequestZeroHandles) {
  std::string response;
  CommandResponseCallback callback =
      base::BindOnce([](std::string* resp_out,
                        const std::string& resp_in) { *resp_out = resp_in; },
                     &response);
  EXPECT_CALL(
      mock_cmd_parser_,
      ParseCommandGetCapability(Pointee(std::string(kFakeRequest)), _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(trunks::TPM_CAP_HANDLES),
                      SetArgPointee<2>(kFakeHandle), SetArgPointee<3>(0),
                      Return(trunks::TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_tpm_handle_manager_, IsHandleTypeSuppoerted(kFakeHandle))
      .WillOnce(Return(true));

  EXPECT_CALL(mock_tpm_handle_manager_, GetHandleList(kFakeHandle, _))
      .WillOnce(DoAll(SetArgPointee<1>(MakeFakeFoundHandles(0)),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(mock_resp_serializer_,
              SerializeResponseGetCapability(
                  NO, IsCapListOf(MakeFakeFoundHandles(0)), _))
      .WillOnce(SetArgPointee<2>(kTestResponse));

  command_.Run(kFakeRequest, std::move(callback));
  EXPECT_EQ(response, kTestResponse);
}

TEST_F(GetCapabilityCommandTest, FailureUnsupportedHandleType) {
  std::string response;
  CommandResponseCallback callback =
      base::BindOnce([](std::string* resp_out,
                        const std::string& resp_in) { *resp_out = resp_in; },
                     &response);
  EXPECT_CALL(
      mock_cmd_parser_,
      ParseCommandGetCapability(Pointee(std::string(kFakeRequest)), _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(trunks::TPM_CAP_HANDLES),
                      SetArgPointee<2>(kFakeHandle),
                      SetArgPointee<3>(kFakeRequestedPropertyCount),
                      Return(trunks::TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_tpm_handle_manager_, IsHandleTypeSuppoerted(kFakeHandle))
      .WillOnce(Return(false));

  EXPECT_CALL(mock_resp_serializer_,
              SerializeHeaderOnlyResponse(trunks::TPM_RC_HANDLE, _))
      .WillOnce(SetArgPointee<1>(kTestResponse));

  command_.Run(kFakeRequest, std::move(callback));
  EXPECT_EQ(response, kTestResponse);
}

TEST_F(GetCapabilityCommandTest, FailureUnsupportedCap) {
  std::string response;
  CommandResponseCallback callback =
      base::BindOnce([](std::string* resp_out,
                        const std::string& resp_in) { *resp_out = resp_in; },
                     &response);
  EXPECT_CALL(
      mock_cmd_parser_,
      ParseCommandGetCapability(Pointee(std::string(kFakeRequest)), _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(trunks::TPM_CAP_ALGS),
                      SetArgPointee<2>(kFakeHandle),
                      SetArgPointee<3>(kFakeRequestedPropertyCount),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(mock_resp_serializer_,
              SerializeHeaderOnlyResponse(trunks::TPM_RC_VALUE, _))
      .WillOnce(SetArgPointee<1>(kTestResponse));

  command_.Run(kFakeRequest, std::move(callback));
  EXPECT_EQ(response, kTestResponse);
}

TEST_F(GetCapabilityCommandTest, FailureUnknownCap) {
  std::string response;
  CommandResponseCallback callback =
      base::BindOnce([](std::string* resp_out,
                        const std::string& resp_in) { *resp_out = resp_in; },
                     &response);
  EXPECT_CALL(
      mock_cmd_parser_,
      ParseCommandGetCapability(Pointee(std::string(kFakeRequest)), _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(trunks::TPM_CAP_LAST + 1),
                      SetArgPointee<2>(kFakeHandle),
                      SetArgPointee<3>(kFakeRequestedPropertyCount),
                      Return(trunks::TPM_RC_SUCCESS)));

  EXPECT_CALL(mock_resp_serializer_,
              SerializeHeaderOnlyResponse(trunks::TPM_RC_VALUE, _))
      .WillOnce(SetArgPointee<1>(kTestResponse));

  command_.Run(kFakeRequest, std::move(callback));
  EXPECT_EQ(response, kTestResponse);
}

TEST_F(GetCapabilityCommandTest, FailureParserError) {
  std::string response;
  CommandResponseCallback callback =
      base::BindOnce([](std::string* resp_out,
                        const std::string& resp_in) { *resp_out = resp_in; },
                     &response);
  EXPECT_CALL(
      mock_cmd_parser_,
      ParseCommandGetCapability(Pointee(std::string(kFakeRequest)), _, _, _))
      .WillOnce(Return(trunks::TPM_RC_INSUFFICIENT));

  EXPECT_CALL(mock_resp_serializer_,
              SerializeHeaderOnlyResponse(trunks::TPM_RC_INSUFFICIENT, _))
      .WillOnce(SetArgPointee<1>(kTestResponse));

  command_.Run(kFakeRequest, std::move(callback));
  EXPECT_EQ(response, kTestResponse);
}

}  // namespace

}  // namespace vtpm
