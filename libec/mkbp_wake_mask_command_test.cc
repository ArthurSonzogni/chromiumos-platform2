// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libec/mkbp_wake_mask_command.h"

namespace ec {
namespace {

using ::testing::Return;

TEST(MkbpWakeMaskCommand, MkbpWakeMaskCommandGet) {
  // Constructor for getting values.
  MkbpWakeMaskCommand cmd(EC_MKBP_HOST_EVENT_WAKE_MASK);
  EXPECT_EQ(cmd.Version(), 0);
  EXPECT_EQ(cmd.Command(), EC_CMD_MKBP_WAKE_MASK);
  EXPECT_EQ(cmd.Req()->action, GET_WAKE_MASK);
  EXPECT_EQ(cmd.Req()->mask_type, EC_MKBP_HOST_EVENT_WAKE_MASK);
}

TEST(MkbpWakeMaskCommand, MkbpWakeMaskCommandSet) {
  MkbpWakeMaskCommand cmd(EC_MKBP_HOST_EVENT_WAKE_MASK,
                          EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_CLOSED));
  EXPECT_EQ(cmd.Version(), 0);
  EXPECT_EQ(cmd.Command(), EC_CMD_MKBP_WAKE_MASK);
  EXPECT_EQ(cmd.Req()->action, SET_WAKE_MASK);
  EXPECT_EQ(cmd.Req()->mask_type, EC_MKBP_HOST_EVENT_WAKE_MASK);
  EXPECT_EQ(cmd.Req()->new_wake_mask, 1);
}

TEST(MkbpWakeMaskHostEventCommand, MkbpWakeMaskHostEventCommandGet) {
  MkbpWakeMaskHostEventCommand cmd;
  EXPECT_EQ(cmd.Version(), 0);
  EXPECT_EQ(cmd.Command(), EC_CMD_MKBP_WAKE_MASK);
  EXPECT_EQ(cmd.Req()->action, GET_WAKE_MASK);
  EXPECT_EQ(cmd.Req()->mask_type, EC_MKBP_HOST_EVENT_WAKE_MASK);
}

TEST(MkbpWakeMaskHostEventCommand, MkbpWakeMaskHostEventCommandSet) {
  MkbpWakeMaskHostEventCommand cmd(
      EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_CLOSED));
  EXPECT_EQ(cmd.Version(), 0);
  EXPECT_EQ(cmd.Command(), EC_CMD_MKBP_WAKE_MASK);
  EXPECT_EQ(cmd.Req()->action, SET_WAKE_MASK);
  EXPECT_EQ(cmd.Req()->mask_type, EC_MKBP_HOST_EVENT_WAKE_MASK);
  EXPECT_EQ(cmd.Req()->new_wake_mask, 1);
}

TEST(MkbpWakeMaskEventCommand, MkbpWakeMaskEventCommandGet) {
  MkbpWakeMaskEventCommand cmd;
  EXPECT_EQ(cmd.Version(), 0);
  EXPECT_EQ(cmd.Command(), EC_CMD_MKBP_WAKE_MASK);
  EXPECT_EQ(cmd.Req()->action, GET_WAKE_MASK);
  EXPECT_EQ(cmd.Req()->mask_type, EC_MKBP_EVENT_WAKE_MASK);
}

TEST(MkbpWakeMaskEventCommand, MkbpWakeMaskEventCommandSet) {
  MkbpWakeMaskEventCommand cmd(EC_HOST_EVENT_MASK(EC_MKBP_EVENT_BUTTON));
  EXPECT_EQ(cmd.Version(), 0);
  EXPECT_EQ(cmd.Command(), EC_CMD_MKBP_WAKE_MASK);
  EXPECT_EQ(cmd.Req()->action, SET_WAKE_MASK);
  EXPECT_EQ(cmd.Req()->mask_type, EC_MKBP_EVENT_WAKE_MASK);
  EXPECT_EQ(cmd.Req()->new_wake_mask, 4);
}

// Mock the underlying EcCommand to test.
class MkbpWakeMaskCommandTest : public testing::Test {
 public:
  class MockMkbpWakeMaskCommand : public MkbpWakeMaskCommand {
   public:
    using MkbpWakeMaskCommand::MkbpWakeMaskCommand;
    MOCK_METHOD(const struct ec_response_mkbp_event_wake_mask*,
                Resp,
                (),
                (const, override));
  };
};

TEST_F(MkbpWakeMaskCommandTest, Success) {
  MockMkbpWakeMaskCommand mock_command(EC_MKBP_HOST_EVENT_WAKE_MASK);
  struct ec_response_mkbp_event_wake_mask response = {
      .wake_mask = EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN)};
  EXPECT_CALL(mock_command, Resp).WillRepeatedly(Return(&response));

  EXPECT_EQ(mock_command.GetWakeMask(), 2);
}

// Mock the underlying EcCommand to test.
class MkbpWakeMaskHostEventCommandTest : public testing::Test {
 public:
  class MockMkbpWakeMaskHostEventCommand : public MkbpWakeMaskHostEventCommand {
   public:
    using MkbpWakeMaskHostEventCommand::MkbpWakeMaskHostEventCommand;
    MOCK_METHOD(const struct ec_response_mkbp_event_wake_mask*,
                Resp,
                (),
                (const, override));
  };
};

TEST_F(MkbpWakeMaskHostEventCommandTest, Success) {
  MockMkbpWakeMaskHostEventCommand mock_command;
  struct ec_response_mkbp_event_wake_mask response = {
      .wake_mask = EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN)};
  EXPECT_CALL(mock_command, Resp).WillRepeatedly(Return(&response));

  EXPECT_TRUE(mock_command.IsEnabled(EC_HOST_EVENT_LID_OPEN));
  EXPECT_FALSE(mock_command.IsEnabled(EC_HOST_EVENT_LID_CLOSED));

  EXPECT_EQ(mock_command.GetWakeMask(), 2);
}

// Mock the underlying EcCommand to test.
class MkbpWakeMaskEventCommandTest : public testing::Test {
 public:
  class MockMkbpWakeMaskEventCommand : public MkbpWakeMaskEventCommand {
   public:
    using MkbpWakeMaskEventCommand::MkbpWakeMaskEventCommand;
    MOCK_METHOD(const struct ec_response_mkbp_event_wake_mask*,
                Resp,
                (),
                (const, override));
  };
};

TEST_F(MkbpWakeMaskEventCommandTest, Success) {
  MockMkbpWakeMaskEventCommand mock_command;
  struct ec_response_mkbp_event_wake_mask response = {
      .wake_mask = EC_HOST_EVENT_MASK(EC_MKBP_EVENT_SWITCH)};
  EXPECT_CALL(mock_command, Resp).WillRepeatedly(Return(&response));

  EXPECT_TRUE(mock_command.IsEnabled(EC_MKBP_EVENT_SWITCH));
  EXPECT_FALSE(mock_command.IsEnabled(EC_MKBP_EVENT_FINGERPRINT));

  EXPECT_EQ(mock_command.GetWakeMask(), 8);
}

}  // namespace
}  // namespace ec
