// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libec/fingerprint/fp_preload_template_command.h>

namespace ec {
namespace {

using ::testing::Return;

constexpr int kFinger = 1;
constexpr int kValidMaxWriteSize = 128;

static const std::vector<uint8_t> kTemplateData(100);

TEST(FpPreloadTemplateCommand, FpPreloadTemplateCommand) {
  auto cmd = FpPreloadTemplateCommand::Create(kFinger, kTemplateData,
                                              kValidMaxWriteSize);
  EXPECT_TRUE(cmd);
  EXPECT_EQ(cmd->Version(), 0);
  EXPECT_EQ(cmd->Command(), EC_CMD_FP_PRELOAD_TEMPLATE);
}

TEST(FpPreloadTemplateCommand, Params) {
  EXPECT_EQ(sizeof(fp_preload_template::Header),
            sizeof(ec_params_fp_preload_template));
  EXPECT_EQ(sizeof(fp_preload_template::Params), kMaxPacketSize);
}

TEST(FpPreloadTemplateCommand, InvalidWriteSize) {
  constexpr int kInvalidMaxWriteSize = kMaxPacketSize + 1;
  EXPECT_FALSE(FpPreloadTemplateCommand::Create(kFinger, kTemplateData,
                                                kInvalidMaxWriteSize));
}

TEST(FpPreloadTemplateCommand, InvalidWriteSizeZero) {
  constexpr int kInvalidMaxWriteSize = 0;
  EXPECT_FALSE(FpPreloadTemplateCommand::Create(kFinger, kTemplateData,
                                                kInvalidMaxWriteSize));
}

TEST(FpPreloadTemplateCommand, MaxWriteSizeEqualsMaxPacketSize) {
  constexpr int kValidWriteSize = kMaxPacketSize;
  EXPECT_TRUE(FpPreloadTemplateCommand::Create(kFinger, kTemplateData,
                                               kValidWriteSize));
}

// Mock the underlying EcCommand to test.
class FpPreloadTemplateCommandTest : public testing::Test {
 public:
  class MockFpPreloadTemplateCommand : public FpPreloadTemplateCommand {
   public:
    MockFpPreloadTemplateCommand(uint16_t fgr,
                                 std::vector<uint8_t> tmpl,
                                 uint16_t max_write_size)
        : FpPreloadTemplateCommand(fgr, std::move(tmpl), max_write_size) {}
    MOCK_METHOD(bool, EcCommandRun, (int fd), (override));
    MOCK_METHOD(uint32_t, Result, (), (const, override));
  };
};

TEST_F(FpPreloadTemplateCommandTest, ZeroFrameSize) {
  constexpr int kMaxWriteSize = 544;  // SPI max packet size is 544.
  const std::vector<uint8_t> kEmptyTemplate;

  auto mock_fp_preload_template_command =
      FpPreloadTemplateCommand::Create<MockFpPreloadTemplateCommand>(
          kFinger, kEmptyTemplate, kMaxWriteSize);

  EXPECT_CALL(*mock_fp_preload_template_command, EcCommandRun)
      .WillOnce([&mock_fp_preload_template_command](int fd) {
        EXPECT_EQ(mock_fp_preload_template_command->Req()->req.offset, 0);
        // 0 | FP_TEMPLATE_COMMIT = 0x80000000
        EXPECT_EQ(mock_fp_preload_template_command->Req()->req.size,
                  0x80000000);
        return true;
      });
  EXPECT_TRUE(mock_fp_preload_template_command->Run(-1));
}

TEST_F(FpPreloadTemplateCommandTest, SmallTemplateSuccess) {
  constexpr int kMaxWriteSize = 544;  // SPI max packet size is 544.
  // Subtract sizeof(ec_params_fp_preload_template) to get 532.
  constexpr int kDataSize = 532;

  // Create a template that has two full packets worth of data and one partial
  // packet.
  constexpr int kTemplateSize = kDataSize + kDataSize + 10;
  std::vector<uint8_t> kTemplate(kTemplateSize);

  auto begin = kTemplate.begin();
  auto end = kTemplate.begin() + kDataSize;
  std::fill(begin, end, 'a');
  begin += kDataSize;
  end += kDataSize;
  std::fill(begin, end, 'b');
  begin += kDataSize;
  end += 10;
  std::fill(begin, end, 'c');

  auto mock_fp_preload_template_command =
      FpPreloadTemplateCommand::Create<MockFpPreloadTemplateCommand>(
          kFinger, kTemplate, kMaxWriteSize);

  testing::InSequence s;
  EXPECT_CALL(*mock_fp_preload_template_command, EcCommandRun)
      .WillOnce([&mock_fp_preload_template_command](int fd) {
        EXPECT_EQ(mock_fp_preload_template_command->Req()->req.fgr, kFinger);
        EXPECT_EQ(mock_fp_preload_template_command->Req()->req.offset, 0);
        EXPECT_EQ(mock_fp_preload_template_command->Req()->req.size, 532);
        auto expected = std::array<uint8_t, 532>();
        expected.fill('a');
        EXPECT_EQ(mock_fp_preload_template_command->Req()->data, expected);
        return true;
      });
  EXPECT_CALL(*mock_fp_preload_template_command, EcCommandRun)
      .WillOnce([&mock_fp_preload_template_command](int fd) {
        EXPECT_EQ(mock_fp_preload_template_command->Req()->req.fgr, kFinger);
        EXPECT_EQ(mock_fp_preload_template_command->Req()->req.offset, 532);
        EXPECT_EQ(mock_fp_preload_template_command->Req()->req.size, 532);
        auto expected = std::array<uint8_t, 532>();
        expected.fill('b');
        EXPECT_EQ(mock_fp_preload_template_command->Req()->data, expected);
        return true;
      });
  EXPECT_CALL(*mock_fp_preload_template_command, EcCommandRun)
      .WillOnce([&mock_fp_preload_template_command](int fd) {
        EXPECT_EQ(mock_fp_preload_template_command->Req()->req.fgr, kFinger);
        EXPECT_EQ(mock_fp_preload_template_command->Req()->req.offset, 1064);
        // 10 | FP_TEMPLATE_COMMIT = 0x8000000A
        EXPECT_EQ(mock_fp_preload_template_command->Req()->req.size,
                  0x8000000A);
        auto expected = std::array<uint8_t, 532>();
        expected.fill('c');
        // Only the first 10 values are valid.
        EXPECT_TRUE(
            std::equal(expected.cbegin(), expected.cbegin() + 10,
                       mock_fp_preload_template_command->Req()->data.begin()));
        return true;
      });
  EXPECT_TRUE(mock_fp_preload_template_command->Run(-1));
}

}  // namespace
}  // namespace ec
