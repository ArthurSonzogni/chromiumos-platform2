// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <bitset>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libec/ec_command.h"
#include "libec/fingerprint/fp_info_command.h"

namespace ec {
namespace {

using ::testing::Return;

TEST(FpInfoCommand_v1, FpInfoCommand_v1) {
  auto cmd = std::make_unique<FpInfoCommand_v1>();
  EXPECT_TRUE(cmd);
  EXPECT_EQ(cmd->Version(), 1);
  EXPECT_EQ(cmd->Command(), EC_CMD_FP_INFO);
}

/**
 * Tests FpInfoCommand_v1's "errors()" method.
 */
class FpInfoCommand_v1_ErrorsTest : public testing::Test {
 public:
  class MockFpInfoCommand_v1 : public FpInfoCommand_v1 {
   public:
    MOCK_METHOD(ec_response_fp_info*, Resp, (), (override));
  };
  MockFpInfoCommand_v1 mock_fp_info_command_;
};

TEST_F(FpInfoCommand_v1_ErrorsTest, Errors_None) {
  EXPECT_CALL(mock_fp_info_command_, Resp).WillOnce(Return(nullptr));

  EXPECT_EQ(mock_fp_info_command_.GetFpSensorErrors(), FpSensorErrors::kNone);
}

TEST_F(FpInfoCommand_v1_ErrorsTest, Errors_NoIrq) {
  struct ec_response_fp_info resp = {.errors = FP_ERROR_NO_IRQ |
                                               FP_ERROR_DEAD_PIXELS_UNKNOWN};

  EXPECT_CALL(mock_fp_info_command_, Resp).WillRepeatedly(Return(&resp));

  EXPECT_EQ(mock_fp_info_command_.GetFpSensorErrors(), FpSensorErrors::kNoIrq);
}

TEST_F(FpInfoCommand_v1_ErrorsTest, Errors_SpiCommunication) {
  struct ec_response_fp_info resp = {.errors = FP_ERROR_SPI_COMM |
                                               FP_ERROR_DEAD_PIXELS_UNKNOWN};

  EXPECT_CALL(mock_fp_info_command_, Resp).WillRepeatedly(Return(&resp));

  EXPECT_EQ(mock_fp_info_command_.GetFpSensorErrors(),
            FpSensorErrors::kSpiCommunication);
}

TEST_F(FpInfoCommand_v1_ErrorsTest, Errors_BadHardwareID) {
  struct ec_response_fp_info resp = {.errors = FP_ERROR_BAD_HWID |
                                               FP_ERROR_DEAD_PIXELS_UNKNOWN};

  EXPECT_CALL(mock_fp_info_command_, Resp).WillRepeatedly(Return(&resp));

  EXPECT_EQ(mock_fp_info_command_.GetFpSensorErrors(),
            FpSensorErrors::kBadHardwareID);
}

TEST_F(FpInfoCommand_v1_ErrorsTest, Errors_InitializationFailure) {
  struct ec_response_fp_info resp = {.errors = FP_ERROR_INIT_FAIL |
                                               FP_ERROR_DEAD_PIXELS_UNKNOWN};

  EXPECT_CALL(mock_fp_info_command_, Resp).WillRepeatedly(Return(&resp));

  EXPECT_EQ(mock_fp_info_command_.GetFpSensorErrors(),
            FpSensorErrors::kInitializationFailure);
}

TEST_F(FpInfoCommand_v1_ErrorsTest, Errors_DeadPixels_0) {
  struct ec_response_fp_info resp = {.errors = FP_ERROR_DEAD_PIXELS(0)};

  EXPECT_CALL(mock_fp_info_command_, Resp).WillRepeatedly(Return(&resp));

  EXPECT_EQ(mock_fp_info_command_.GetFpSensorErrors(), FpSensorErrors::kNone);
}

TEST_F(FpInfoCommand_v1_ErrorsTest, Errors_DeadPixels_1) {
  struct ec_response_fp_info resp = {.errors = FP_ERROR_DEAD_PIXELS(1)};

  EXPECT_CALL(mock_fp_info_command_, Resp).WillRepeatedly(Return(&resp));

  EXPECT_EQ(mock_fp_info_command_.GetFpSensorErrors(),
            FpSensorErrors::kDeadPixels);
}

TEST_F(FpInfoCommand_v1_ErrorsTest, Errors_Multiple) {
  struct ec_response_fp_info resp = {.errors = FP_ERROR_DEAD_PIXELS(1) |
                                               FP_ERROR_BAD_HWID};

  EXPECT_CALL(mock_fp_info_command_, Resp).WillRepeatedly(Return(&resp));

  EXPECT_EQ(mock_fp_info_command_.GetFpSensorErrors(),
            FpSensorErrors::kDeadPixels | FpSensorErrors::kBadHardwareID);
}

/**
 * Tests FpInfoCommand_v1's "NumDeadPixels()" method.
 */
class FpInfoCommand_v1_NumDeadPixelsTest : public testing::Test {
 public:
  class MockFpInfoCommand_v1 : public FpInfoCommand_v1 {
   public:
    MOCK_METHOD(ec_response_fp_info*, Resp, (), (override));
  };
  MockFpInfoCommand_v1 mock_fp_info_command_;
};

TEST_F(FpInfoCommand_v1_NumDeadPixelsTest, NoResponse) {
  EXPECT_CALL(mock_fp_info_command_, Resp).WillRepeatedly(Return(nullptr));

  const auto expected = FpInfoCommand::kDeadPixelsUnknown;
  EXPECT_EQ(mock_fp_info_command_.NumDeadPixels(), expected);
}

TEST_F(FpInfoCommand_v1_NumDeadPixelsTest, DeadPixelsUnknown) {
  struct ec_response_fp_info resp = {.errors = FP_ERROR_BAD_HWID |
                                               FP_ERROR_DEAD_PIXELS_UNKNOWN};

  EXPECT_CALL(mock_fp_info_command_, Resp).WillRepeatedly(Return(&resp));

  const auto expected = FpInfoCommand::kDeadPixelsUnknown;
  EXPECT_EQ(mock_fp_info_command_.NumDeadPixels(), expected);
}

TEST_F(FpInfoCommand_v1_NumDeadPixelsTest, ZeroDeadPixels) {
  struct ec_response_fp_info resp = {.errors = FP_ERROR_INIT_FAIL |
                                               FP_ERROR_DEAD_PIXELS(0)};

  EXPECT_CALL(mock_fp_info_command_, Resp).WillRepeatedly(Return(&resp));

  EXPECT_EQ(mock_fp_info_command_.NumDeadPixels(), 0);
}

TEST_F(FpInfoCommand_v1_NumDeadPixelsTest, OneDeadPixel) {
  struct ec_response_fp_info resp = {.errors = FP_ERROR_SPI_COMM |
                                               FP_ERROR_DEAD_PIXELS(1)};

  EXPECT_CALL(mock_fp_info_command_, Resp).WillRepeatedly(Return(&resp));

  EXPECT_EQ(mock_fp_info_command_.NumDeadPixels(), 1);
}

/**
 * Tests FpInfoCommand_v1's "sensor_id" method.
 */
class FpInfoCommand_v1_SensorIdTest : public testing::Test {
 public:
  class MockFpInfoCommand_v1 : public FpInfoCommand_v1 {
   public:
    MOCK_METHOD(ec_response_fp_info*, Resp, (), (override));
  };
  MockFpInfoCommand_v1 mock_fp_info_command;
};

TEST_F(FpInfoCommand_v1_SensorIdTest, NullResponse) {
  EXPECT_CALL(mock_fp_info_command, Resp).WillRepeatedly(Return(nullptr));

  EXPECT_EQ(mock_fp_info_command.sensor_id(), std::nullopt);
}

TEST_F(FpInfoCommand_v1_SensorIdTest, ValidSensorId) {
  struct ec_response_fp_info resp = {
      .vendor_id = 1, .product_id = 2, .model_id = 3, .version = 4};
  EXPECT_CALL(mock_fp_info_command, Resp).WillRepeatedly(Return(&resp));

  EXPECT_TRUE(mock_fp_info_command.sensor_id().has_value());
  EXPECT_EQ(mock_fp_info_command.sensor_id()->vendor_id, 1);
  EXPECT_EQ(mock_fp_info_command.sensor_id()->product_id, 2);
  EXPECT_EQ(mock_fp_info_command.sensor_id()->model_id, 3);
  EXPECT_EQ(mock_fp_info_command.sensor_id()->version, 4);
}

/**
 * Tests FpInfoCommand_v1's "sensor_image" method.
 */
class FpInfoCommand_v1_SensorImageTest : public testing::Test {
 public:
  class MockFpInfoCommand_v1 : public FpInfoCommand_v1 {
   public:
    MOCK_METHOD(ec_response_fp_info*, Resp, (), (override));
  };
  MockFpInfoCommand_v1 mock_fp_info_command;
};

TEST_F(FpInfoCommand_v1_SensorImageTest, NullResponse) {
  EXPECT_CALL(mock_fp_info_command, Resp).WillRepeatedly(Return(nullptr));

  EXPECT_EQ(mock_fp_info_command.sensor_image(), std::nullopt);
}

TEST_F(FpInfoCommand_v1_SensorImageTest, ValidSensorImage) {
  struct ec_response_fp_info resp = {
      .frame_size = 1, .pixel_format = 2, .width = 3, .height = 4, .bpp = 5};
  EXPECT_CALL(mock_fp_info_command, Resp).WillRepeatedly(Return(&resp));

  EXPECT_TRUE(mock_fp_info_command.sensor_image().has_value());
  EXPECT_EQ(mock_fp_info_command.sensor_image()->frame_size, 1);
  EXPECT_EQ(mock_fp_info_command.sensor_image()->pixel_format, 2);
  EXPECT_EQ(mock_fp_info_command.sensor_image()->width, 3);
  EXPECT_EQ(mock_fp_info_command.sensor_image()->height, 4);
  EXPECT_EQ(mock_fp_info_command.sensor_image()->bpp, 5);
}

/**
 * Tests FpInfoCommand_v1's "template_info" method.
 */
class FpInfoCommand_v1_TemplateInfoTest : public testing::Test {
 public:
  class MockFpInfoCommand_v1 : public FpInfoCommand_v1 {
   public:
    MOCK_METHOD(ec_response_fp_info*, Resp, (), (override));
  };
  MockFpInfoCommand_v1 mock_fp_info_command;
};

TEST_F(FpInfoCommand_v1_TemplateInfoTest, NullResponse) {
  EXPECT_CALL(mock_fp_info_command, Resp).WillRepeatedly(Return(nullptr));

  EXPECT_EQ(mock_fp_info_command.template_info(), std::nullopt);
}

TEST_F(FpInfoCommand_v1_TemplateInfoTest, ValidTemplateInfo) {
  struct ec_response_fp_info resp = {.template_size = 1024,
                                     .template_max = 4,
                                     .template_valid = 3,
                                     .template_dirty = 1 << 3,
                                     .template_version = 1};

  EXPECT_CALL(mock_fp_info_command, Resp).WillRepeatedly(Return(&resp));

  EXPECT_TRUE(mock_fp_info_command.template_info().has_value());
  EXPECT_EQ(mock_fp_info_command.template_info()->size, 1024);
  EXPECT_EQ(mock_fp_info_command.template_info()->max_templates, 4);
  EXPECT_EQ(mock_fp_info_command.template_info()->num_valid, 3);
  EXPECT_EQ(mock_fp_info_command.template_info()->dirty,
            std::bitset<32>(1 << 3));
  EXPECT_EQ(mock_fp_info_command.template_info()->version, 1);
}

}  // namespace
}  // namespace ec
