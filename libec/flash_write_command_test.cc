// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libec/flash_write_command.h>

namespace ec {
namespace {

using ::testing::Return;

constexpr int kValidMaxWriteSize = 128;

TEST(FlashWriteCommand, FlashWriteCommand) {
  auto cmd = FlashWriteCommand::Create(std::vector<uint8_t>(100), 0,
                                       kValidMaxWriteSize);
  EXPECT_TRUE(cmd);
  EXPECT_EQ(cmd->Version(), 0);
  EXPECT_EQ(cmd->Command(), EC_CMD_FLASH_WRITE);
}

TEST(FlashWriteCommand, Params) {
  EXPECT_EQ(sizeof(flash_write::Header), sizeof(ec_params_flash_write));
  EXPECT_EQ(sizeof(flash_write::Params), kMaxPacketSize);
}

TEST(FlashWriteCommand, InvalidWriteSize) {
  constexpr int kInvalidMaxWriteSize = 545;
  EXPECT_FALSE(FlashWriteCommand::Create(std::vector<uint8_t>(100), 0,
                                         kInvalidMaxWriteSize));
}

TEST(FlashWriteCommand, InvalidWriteSizeZero) {
  constexpr int kInvalidMaxWriteSize = 0;
  EXPECT_FALSE(FlashWriteCommand::Create(std::vector<uint8_t>(100), 0,
                                         kInvalidMaxWriteSize));
}

TEST(FlashWriteCommand, MaxWriteSizeEqualsMaxPacketSize) {
  constexpr int kValidWriteSize = 544;
  EXPECT_TRUE(
      FlashWriteCommand::Create(std::vector<uint8_t>(100), 0, kValidWriteSize));
}

TEST(FlashWriteCommand, ZeroFrameSize) {
  const std::vector<uint8_t> kInvalidTemplateData(0);
  EXPECT_FALSE(
      FlashWriteCommand::Create(kInvalidTemplateData, 0, kValidMaxWriteSize));
}

TEST(FlashWriteCommand, OffsetBoundaryCondition) {
  constexpr uint32_t kOffset = 4294967295;  // 2^32 - 1
  EXPECT_TRUE(FlashWriteCommand::Create(std::vector<uint8_t>(1), kOffset,
                                        kValidMaxWriteSize));
  EXPECT_FALSE(FlashWriteCommand::Create(std::vector<uint8_t>(2), kOffset,
                                         kValidMaxWriteSize));
}

// Mock the underlying EcCommand to test.
class FlashWriteCommandTest : public testing::Test {
 public:
  class MockFlashWriteCommand : public FlashWriteCommand {
   public:
    MockFlashWriteCommand(std::vector<uint8_t> data,
                          uint32_t offset,
                          uint16_t max_write_size)
        : FlashWriteCommand(std::move(data), offset, max_write_size) {}
    MOCK_METHOD(bool, EcCommandRun, (int fd), (override));
    MOCK_METHOD(uint32_t, Result, (), (const, override));
  };
};

TEST_F(FlashWriteCommandTest, Success) {
  constexpr int kMaxWriteSize = 544;  // SPI max packet size is 544.
  constexpr int kDataSize = 536;      // Subtract
                                      // sizeof(struct ec_params_flash_write) to
                                      // get 536.

  // Perform a write that has one full packet of data and one partial
  // packet.
  constexpr int kFlashWriteSize = kDataSize + 10;
  std::vector<uint8_t> kFlashWriteData(kFlashWriteSize);

  auto begin = kFlashWriteData.begin();
  auto end = kFlashWriteData.begin() + kDataSize;
  std::fill(begin, end, 'a');
  begin += kDataSize;
  end += 10;
  std::fill(begin, end, 'b');

  auto mock_flash_write_command =
      FlashWriteCommand::Create<MockFlashWriteCommand>(kFlashWriteData, 5,
                                                       kMaxWriteSize);

  testing::InSequence s;
  EXPECT_CALL(*mock_flash_write_command, EcCommandRun)
      .WillOnce([&mock_flash_write_command](int fd) {
        EXPECT_EQ(mock_flash_write_command->Req()->req.offset, 5);
        EXPECT_EQ(mock_flash_write_command->Req()->req.size, 536);
        auto expected = std::array<uint8_t, 536>();
        expected.fill('a');
        EXPECT_EQ(mock_flash_write_command->Req()->data, expected);
        return true;
      });
  EXPECT_CALL(*mock_flash_write_command, EcCommandRun)
      .WillOnce([&mock_flash_write_command](int fd) {
        EXPECT_EQ(mock_flash_write_command->Req()->req.offset, 541);
        EXPECT_EQ(mock_flash_write_command->Req()->req.size, 10);
        auto expected = std::array<uint8_t, 10>();
        expected.fill('b');
        // Only the first 10 values are valid.
        EXPECT_TRUE(
            std::equal(mock_flash_write_command->Req()->data.cbegin(),
                       mock_flash_write_command->Req()->data.cbegin() + 10,
                       expected.cbegin()));
        return true;
      });
  EXPECT_TRUE(mock_flash_write_command->Run(-1));
}

}  // namespace
}  // namespace ec
