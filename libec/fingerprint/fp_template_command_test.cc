// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libec/fingerprint/fp_template_command.h>

namespace ec {
namespace {

using ::testing::Return;

constexpr int kValidMaxWriteSize = 128;

static const std::vector<uint8_t> kTemplateData(100);

TEST(FpTemplateCommand, FpTemplateCommand) {
  auto cmd = FpTemplateCommand::Create(kTemplateData, kValidMaxWriteSize, true);
  EXPECT_TRUE(cmd);
  EXPECT_EQ(cmd->Version(), 0);
  EXPECT_EQ(cmd->Command(), EC_CMD_FP_TEMPLATE);

  // No-commit mode.
  cmd = FpTemplateCommand::Create(kTemplateData, kValidMaxWriteSize, false);
  EXPECT_TRUE(cmd);
  EXPECT_EQ(cmd->Version(), 0);
  EXPECT_EQ(cmd->Command(), EC_CMD_FP_TEMPLATE);
}

TEST(FpTemplateCommand, Params) {
  EXPECT_EQ(sizeof(fp_template::Header), sizeof(ec_params_fp_template));
  EXPECT_EQ(sizeof(fp_template::Params), kMaxPacketSize);
}

TEST(FpTemplateCommand, InvalidWriteSize) {
  constexpr int kInvalidMaxWriteSize = kMaxPacketSize + 1;
  EXPECT_FALSE(
      FpTemplateCommand::Create(kTemplateData, kInvalidMaxWriteSize, true));
}

TEST(FpTemplateCommand, InvalidWriteSizeZero) {
  constexpr int kInvalidMaxWriteSize = 0;
  EXPECT_FALSE(
      FpTemplateCommand::Create(kTemplateData, kInvalidMaxWriteSize, true));
}

TEST(FpTemplateCommand, MaxWriteSizeEqualsMaxPacketSize) {
  constexpr int kValidWriteSize = kMaxPacketSize;
  EXPECT_TRUE(FpTemplateCommand::Create(kTemplateData, kValidWriteSize, true));
}

struct FpTemplateCommandTestParam {
  bool should_commit;
};

// Mock the underlying EcCommand to test.
class FpTemplateCommandTest
    : public testing::Test,
      public ::testing::WithParamInterface<FpTemplateCommandTestParam> {
 public:
  class MockFpTemplateCommand : public FpTemplateCommand {
   public:
    MockFpTemplateCommand(std::vector<uint8_t> tmpl,
                          uint16_t max_write_size,
                          bool commit)
        : FpTemplateCommand(std::move(tmpl), max_write_size, commit) {}
    MOCK_METHOD(bool, EcCommandRun, (int fd), (override));
    MOCK_METHOD(uint32_t, Result, (), (const, override));
  };
};

TEST_P(FpTemplateCommandTest, ZeroFrameSize) {
  constexpr int kMaxWriteSize = 544;  // SPI max packet size is 544.
  const std::vector<uint8_t> kEmptyTemplate;
  bool should_commit = GetParam().should_commit;

  auto mock_fp_template_command =
      FpTemplateCommand::Create<MockFpTemplateCommand>(
          kEmptyTemplate, kMaxWriteSize, should_commit);

  EXPECT_CALL(*mock_fp_template_command, EcCommandRun)
      .WillOnce([&mock_fp_template_command, should_commit](int fd) {
        EXPECT_EQ(mock_fp_template_command->Req()->req.offset, 0);
        // 0 | FP_TEMPLATE_COMMIT = 0x80000000
        EXPECT_EQ(mock_fp_template_command->Req()->req.size,
                  should_commit ? 0x80000000 : 0);
        return true;
      });
  EXPECT_TRUE(mock_fp_template_command->Run(-1));
}

TEST_P(FpTemplateCommandTest, SmallTemplateSuccess) {
  constexpr int kMaxWriteSize = 544;  // SPI max packet size is 544.
  constexpr int kDataSize = 536;      // Subtract
                                      // sizeof(struct ec_params_fp_template) to
                                      // get 536.
  bool should_commit = GetParam().should_commit;

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

  auto mock_fp_template_command =
      FpTemplateCommand::Create<MockFpTemplateCommand>(kTemplate, kMaxWriteSize,
                                                       should_commit);

  testing::InSequence s;
  EXPECT_CALL(*mock_fp_template_command, EcCommandRun)
      .WillOnce([&mock_fp_template_command](int fd) {
        EXPECT_EQ(mock_fp_template_command->Req()->req.offset, 0);
        EXPECT_EQ(mock_fp_template_command->Req()->req.size, 536);
        auto expected = std::array<uint8_t, 536>();
        expected.fill('a');
        EXPECT_EQ(mock_fp_template_command->Req()->data, expected);
        return true;
      });
  EXPECT_CALL(*mock_fp_template_command, EcCommandRun)
      .WillOnce([&mock_fp_template_command](int fd) {
        EXPECT_EQ(mock_fp_template_command->Req()->req.offset, 536);
        EXPECT_EQ(mock_fp_template_command->Req()->req.size, 536);
        auto expected = std::array<uint8_t, 536>();
        expected.fill('b');
        EXPECT_EQ(mock_fp_template_command->Req()->data, expected);
        return true;
      });
  EXPECT_CALL(*mock_fp_template_command, EcCommandRun)
      .WillOnce([&mock_fp_template_command, should_commit](int fd) {
        EXPECT_EQ(mock_fp_template_command->Req()->req.offset, 1072);
        // 10 | FP_TEMPLATE_COMMIT = 0x8000000A
        EXPECT_EQ(mock_fp_template_command->Req()->req.size,
                  10 | (should_commit ? 0x80000000 : 0));
        auto expected = std::array<uint8_t, 536>();
        expected.fill('c');
        // Only the first 10 values are valid.
        EXPECT_TRUE(std::equal(expected.cbegin(), expected.cbegin() + 10,
                               mock_fp_template_command->Req()->data.begin()));
        return true;
      });
  EXPECT_TRUE(mock_fp_template_command->Run(-1));
}

INSTANTIATE_TEST_SUITE_P(FpTemplate,
                         FpTemplateCommandTest,
                         ::testing::Values(
                             FpTemplateCommandTestParam{
                                 .should_commit = false,
                             },
                             FpTemplateCommandTestParam{
                                 .should_commit = true,
                             }));

}  // namespace
}  // namespace ec
