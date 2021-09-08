// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libec/flash_read_command.h"

namespace ec {
namespace {

using ::testing::Return;

TEST(FlashReadCommand, FlashReadCommand) {
  auto cmd = FlashReadCommand::Create(3, 10, 128);
  EXPECT_EQ(cmd->Version(), 0);
  EXPECT_EQ(cmd->Command(), EC_CMD_FLASH_READ);
}

TEST(FlashReadCommand, MaxReadSizeTooLarge) {
  constexpr int kInvalidMaxReadSize = 545;
  EXPECT_FALSE(FlashReadCommand::Create(0, 10, kInvalidMaxReadSize));
}

TEST(FlashReadCommand, MaxReadSizeZero) {
  constexpr int kInvalidMaxReadSize = 0;
  EXPECT_FALSE(FlashReadCommand::Create(0, 10, kInvalidMaxReadSize));
}

TEST(FlashReadCommand, OffsetBoundaryCondition) {
  constexpr uint32_t kOffset = 4294967295;  // 2^32 - 1
  uint32_t read_size = 1;
  EXPECT_TRUE(FlashReadCommand::Create(kOffset, read_size, 128));
  read_size = 2;
  EXPECT_FALSE(FlashReadCommand::Create(kOffset, read_size, 128));
}

// Mock the underlying EcCommand to test.
class FlashReadCommandTest : public testing::Test {
 public:
  class MockFlashReadCommand : public FlashReadCommand {
   public:
    using FlashReadCommand::FlashReadCommand;
    MOCK_METHOD(FlashReadPacket*, Resp, (), (override));
    MOCK_METHOD(bool, EcCommandRun, (int fd), (override));
    MOCK_METHOD(uint32_t, Result, (), (const, override));
  };
};

TEST_F(FlashReadCommandTest, SinglePacketGetData) {
  auto mock_command =
      FlashReadCommand::Create<MockFlashReadCommand>(0, 5, kMaxPacketSize);
  FlashReadPacket response;
  for (int i = 0; i < response.size(); ++i) {
    response[i] = i;
  }

  EXPECT_CALL(*mock_command, EcCommandRun)
      .WillOnce([&mock_command, &response](int fd) {
        EXPECT_EQ(mock_command->Req()->offset, 0);
        EXPECT_EQ(mock_command->Req()->size, 5);
        EXPECT_CALL(*mock_command, Resp).WillRepeatedly(Return(&response));
        EXPECT_CALL(*mock_command, Result)
            .WillRepeatedly(Return(EC_RES_SUCCESS));
        return true;
      });

  EXPECT_TRUE(mock_command->Run(-1));

  EXPECT_EQ(mock_command->GetData(), (std::vector<uint8_t>{0, 1, 2, 3, 4}));
}

TEST_F(FlashReadCommandTest, MultiplePacketsGetData) {
  constexpr int kReadSize = kMaxPacketSize + 10;
  auto mock_command = FlashReadCommand::Create<MockFlashReadCommand>(
      3, kReadSize, kMaxPacketSize);
  EXPECT_TRUE(mock_command);

  FlashReadPacket response1;
  for (int i = 0; i < response1.size(); ++i) {
    response1[i] = i;
  }
  FlashReadPacket response2;
  for (int i = 0; i < response2.size(); ++i) {
    response2[i] = i + response1.size();
  }

  FlashReadPacket response{};
  EXPECT_CALL(*mock_command, Resp).WillRepeatedly(Return(&response));
  EXPECT_CALL(*mock_command, Result).WillRepeatedly(Return(EC_RES_SUCCESS));

  testing::InSequence s;
  EXPECT_CALL(*mock_command, EcCommandRun)
      .WillOnce([&mock_command, &response, &response1](int fd) {
        EXPECT_EQ(mock_command->Req()->offset, 3);
        EXPECT_EQ(mock_command->Req()->size, 544);
        response = response1;
        return true;
      });
  EXPECT_CALL(*mock_command, EcCommandRun)
      .WillOnce([&mock_command, &response, &response2](int fd) {
        EXPECT_EQ(mock_command->Req()->offset, 547);
        EXPECT_EQ(mock_command->Req()->size, 10);
        response = response2;
        return true;
      });

  EXPECT_TRUE(mock_command->Run(-1));

  std::vector<uint8_t> expected(554);
  for (int i = 0; i < expected.size(); ++i) {
    expected[i] = i;
  }
  EXPECT_EQ(mock_command->GetData(), expected);
}

}  // namespace
}  // namespace ec
