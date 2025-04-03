// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_info_command.h"

#include <bitset>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libec/ec_command.h"

namespace ec {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Return;
using ::testing::SizeIs;

constexpr int kDummyFd = 0;

TEST(FpInfoCommand, FpInfoCommand) {
  auto cmd_v1 = FpInfoCommand(/*version=*/1);
  EXPECT_EQ(cmd_v1.Version(), 1);
  EXPECT_EQ(cmd_v1.GetVersion(), 1);
  EXPECT_EQ(cmd_v1.Command(), EC_CMD_FP_INFO);

  auto cmd_v2 = FpInfoCommand(/*version=*/2);
  EXPECT_EQ(cmd_v2.Version(), 2);
  EXPECT_EQ(cmd_v2.GetVersion(), 2);
  EXPECT_EQ(cmd_v2.Command(), EC_CMD_FP_INFO);
}

class FpInfoCommandTest : public testing::Test {
 public:
  FpInfoCommandTest() {
    mock_fp_info_command_v1_ = std::make_unique<MockFpInfoCommand_v1>();
    mock_fp_info_command_v2_ = std::make_unique<MockFpInfoCommand_v2>();
  }

 protected:
  class MockFpInfoCommand_v1 : public FpInfoCommand_v1 {
   public:
    MOCK_METHOD(ec_response_fp_info*, Resp, (), (override));
    MOCK_METHOD(bool, Run, (int fd), (override));
    MOCK_METHOD(uint32_t, Result, (), (const, override));
  };

  class MockFpInfoCommand_v2 : public FpInfoCommand_v2 {
   public:
    MOCK_METHOD(ec::fp_info::Params_v2*, Resp, (), (override));
    MOCK_METHOD(bool, Run, (int fd), (override));
    MOCK_METHOD(uint32_t, Result, (), (const, override));
  };

  std::unique_ptr<MockFpInfoCommand_v1> mock_fp_info_command_v1_;
  std::unique_ptr<MockFpInfoCommand_v2> mock_fp_info_command_v2_;
};

TEST_F(FpInfoCommandTest, GetFpSensorErrors_v1) {
  struct ec_response_fp_info resp = {.errors = FP_ERROR_INIT_FAIL |
                                               FP_ERROR_DEAD_PIXELS_UNKNOWN};

  EXPECT_CALL(*mock_fp_info_command_v1_, Resp).WillRepeatedly(Return(&resp));
  auto fp_info_command = std::make_unique<ec::FpInfoCommand>(
      1, std::move(mock_fp_info_command_v1_), nullptr);

  EXPECT_EQ(fp_info_command->GetFpSensorErrors(),
            FpSensorErrors::kInitializationFailure);
}

TEST_F(FpInfoCommandTest, NumDeadPixels_v1) {
  struct ec_response_fp_info resp = {.errors = FP_ERROR_BAD_HWID |
                                               FP_ERROR_DEAD_PIXELS_UNKNOWN};

  EXPECT_CALL(*mock_fp_info_command_v1_, Resp).WillRepeatedly(Return(&resp));
  auto fp_info_command = std::make_unique<ec::FpInfoCommand>(
      1, std::move(mock_fp_info_command_v1_), nullptr);

  const auto expected = FpInfoCommand::kDeadPixelsUnknown;
  EXPECT_EQ(fp_info_command->NumDeadPixels(), expected);
}

TEST_F(FpInfoCommandTest, sensor_id_v1) {
  struct ec_response_fp_info resp = {
      .vendor_id = 1, .product_id = 2, .model_id = 3, .version = 4};

  EXPECT_CALL(*mock_fp_info_command_v1_, Resp).WillRepeatedly(Return(&resp));
  auto fp_info_command = std::make_unique<ec::FpInfoCommand>(
      1, std::move(mock_fp_info_command_v1_), nullptr);

  EXPECT_THAT(fp_info_command->sensor_id().value(), Eq(SensorId{
                                                        .vendor_id = 1,
                                                        .product_id = 2,
                                                        .model_id = 3,
                                                        .version = 4,
                                                    }));
}

TEST_F(FpInfoCommandTest, sensor_image_valid_v1) {
  struct ec_response_fp_info resp = {
      .frame_size = 1, .pixel_format = 2, .width = 3, .height = 4, .bpp = 5};

  EXPECT_CALL(*mock_fp_info_command_v1_, Resp).WillRepeatedly(Return(&resp));
  auto fp_info_command = std::make_unique<ec::FpInfoCommand>(
      1, std::move(mock_fp_info_command_v1_), nullptr);

  EXPECT_THAT(fp_info_command->sensor_image(), ElementsAre(SensorImage{
                                                   .width = 3,
                                                   .height = 4,
                                                   .frame_size = 1,
                                                   .pixel_format = 2,
                                                   .bpp = 5,
                                               }));
}

TEST_F(FpInfoCommandTest, sensor_image_empty_v1) {
  EXPECT_CALL(*mock_fp_info_command_v1_, Resp).WillRepeatedly(Return(nullptr));
  auto fp_info_command = std::make_unique<ec::FpInfoCommand>(
      1, std::move(mock_fp_info_command_v1_), nullptr);

  EXPECT_THAT(fp_info_command->sensor_image(), SizeIs(0));
}

TEST_F(FpInfoCommandTest, template_info_v1) {
  struct ec_response_fp_info resp = {.template_size = 1024,
                                     .template_max = 4,
                                     .template_valid = 3,
                                     .template_dirty = 1 << 3,
                                     .template_version = 1};

  EXPECT_CALL(*mock_fp_info_command_v1_, Resp).WillRepeatedly(Return(&resp));
  auto fp_info_command = std::make_unique<ec::FpInfoCommand>(
      1, std::move(mock_fp_info_command_v1_), nullptr);

  EXPECT_THAT(fp_info_command->template_info().value(),
              Eq(TemplateInfo{
                  .version = 1,
                  .size = 1024,
                  .max_templates = 4,
                  .num_valid = 3,
                  .dirty = std::bitset<32>(1 << 3),
              }));
}

TEST_F(FpInfoCommandTest, Run_v1) {
  EXPECT_CALL(*mock_fp_info_command_v1_, Run).WillRepeatedly(Return(true));
  auto fp_info_command = std::make_unique<ec::FpInfoCommand>(
      1, std::move(mock_fp_info_command_v1_), nullptr);

  EXPECT_TRUE(fp_info_command->Run(kDummyFd));
}

TEST_F(FpInfoCommandTest, Result_v1) {
  uint32_t result = EC_RES_ACCESS_DENIED;
  EXPECT_CALL(*mock_fp_info_command_v1_, Result).WillRepeatedly(Return(result));
  auto fp_info_command = std::make_unique<ec::FpInfoCommand>(
      1, std::move(mock_fp_info_command_v1_), nullptr);

  EXPECT_EQ(fp_info_command->Result(), result);
}

TEST_F(FpInfoCommandTest, GetFpSensorErrors_v2) {
  struct fp_info::Params_v2 resp = {
      .info = {.sensor_info = {.errors = FP_ERROR_INIT_FAIL |
                                         FP_ERROR_DEAD_PIXELS_UNKNOWN}}};

  EXPECT_CALL(*mock_fp_info_command_v2_, Resp).WillRepeatedly(Return(&resp));
  auto fp_info_command = std::make_unique<ec::FpInfoCommand>(
      2, nullptr, std::move(mock_fp_info_command_v2_));

  EXPECT_EQ(fp_info_command->GetFpSensorErrors(),
            FpSensorErrors::kInitializationFailure);
}

TEST_F(FpInfoCommandTest, NumDeadPixels_v2) {
  EXPECT_CALL(*mock_fp_info_command_v2_, Resp).WillRepeatedly(Return(nullptr));
  auto fp_info_command = std::make_unique<ec::FpInfoCommand>(
      2, nullptr, std::move(mock_fp_info_command_v2_));

  const auto expected = FpInfoCommand::kDeadPixelsUnknown;
  EXPECT_EQ(fp_info_command->NumDeadPixels(), expected);
}

TEST_F(FpInfoCommandTest, sensor_id_v2) {
  struct fp_info::Params_v2 resp = {.info = {.sensor_info = {.vendor_id = 1,
                                                             .product_id = 2,
                                                             .model_id = 3,
                                                             .version = 4}}};

  EXPECT_CALL(*mock_fp_info_command_v2_, Resp).WillRepeatedly(Return(&resp));
  auto fp_info_command = std::make_unique<ec::FpInfoCommand>(
      2, nullptr, std::move(mock_fp_info_command_v2_));

  EXPECT_THAT(fp_info_command->sensor_id().value(), Eq(SensorId{
                                                        .vendor_id = 1,
                                                        .product_id = 2,
                                                        .model_id = 3,
                                                        .version = 4,
                                                    }));
}

TEST_F(FpInfoCommandTest, sensor_image_valid_v2) {
  struct fp_info::Params_v2 resp;

  resp.info.sensor_info.num_capture_types = 2;
  resp.image_frame_params[0] = {
      .frame_size = 5120,
      .pixel_format = 0x59455247,
      .width = 64,
      .height = 80,
      .bpp = 8,
  };
  resp.image_frame_params[1] = {
      .frame_size = 36864,
      .pixel_format = 0x59455247,
      .width = 192,
      .height = 96,
      .bpp = 16,
  };

  EXPECT_CALL(*mock_fp_info_command_v2_, Resp).WillRepeatedly(Return(&resp));
  auto fp_info_command = std::make_unique<ec::FpInfoCommand>(
      2, nullptr, std::move(mock_fp_info_command_v2_));

  EXPECT_THAT(fp_info_command->sensor_image(),
              ElementsAre(
                  SensorImage{
                      .width = 64,
                      .height = 80,
                      .frame_size = 5120,
                      .pixel_format = 0x59455247,
                      .bpp = 8,
                  },
                  SensorImage{
                      .width = 192,
                      .height = 96,
                      .frame_size = 36864,
                      .pixel_format = 0x59455247,
                      .bpp = 16,
                  }));
}

TEST_F(FpInfoCommandTest, sensor_image_empty_v2) {
  EXPECT_CALL(*mock_fp_info_command_v2_, Resp).WillRepeatedly(Return(nullptr));
  auto fp_info_command = std::make_unique<ec::FpInfoCommand>(
      2, nullptr, std::move(mock_fp_info_command_v2_));

  EXPECT_THAT(fp_info_command->sensor_image(), SizeIs(0));
}

TEST_F(FpInfoCommandTest, template_info_v2) {
  struct fp_info::Params_v2 resp = {
      .info = {.template_info = {.template_size = 1024,
                                 .template_max = 4,
                                 .template_valid = 3,
                                 .template_dirty = 1 << 3,
                                 .template_version = 1}}};

  EXPECT_CALL(*mock_fp_info_command_v2_, Resp).WillRepeatedly(Return(&resp));
  auto fp_info_command = std::make_unique<ec::FpInfoCommand>(
      2, nullptr, std::move(mock_fp_info_command_v2_));

  EXPECT_THAT(fp_info_command->template_info().value(),
              Eq(TemplateInfo{
                  .version = 1,
                  .size = 1024,
                  .max_templates = 4,
                  .num_valid = 3,
                  .dirty = std::bitset<32>(1 << 3),
              }));
}

TEST_F(FpInfoCommandTest, Run_v2) {
  EXPECT_CALL(*mock_fp_info_command_v2_, Run).WillRepeatedly(Return(true));
  auto fp_info_command = std::make_unique<ec::FpInfoCommand>(
      2, nullptr, std::move(mock_fp_info_command_v2_));

  EXPECT_TRUE(fp_info_command->Run(kDummyFd));
}

TEST_F(FpInfoCommandTest, Result_v2) {
  uint32_t result = EC_RES_ACCESS_DENIED;
  EXPECT_CALL(*mock_fp_info_command_v2_, Result).WillRepeatedly(Return(result));
  auto fp_info_command = std::make_unique<ec::FpInfoCommand>(
      2, nullptr, std::move(mock_fp_info_command_v2_));

  EXPECT_EQ(fp_info_command->Result(), result);
}

}  // namespace
}  // namespace ec
