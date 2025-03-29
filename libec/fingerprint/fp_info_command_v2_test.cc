/// Copyright 2025 The ChromiumOS Authors
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
using ::testing::SizeIs;

TEST(FpInfoCommand_v2, FpInfoCommand_v2) {
  auto cmd = std::make_unique<FpInfoCommand_v2>();
  EXPECT_TRUE(cmd);
  EXPECT_EQ(cmd->Version(), 2);
  EXPECT_EQ(cmd->Command(), EC_CMD_FP_INFO);
}

/**
 * Tests FpInfoCommand_v2's "errors()" method.
 */
class FpInfoCommand_v2_ErrorsTest : public testing::Test {
 public:
  class MockFpInfoCommand_v2 : public FpInfoCommand_v2 {
   public:
    MOCK_METHOD(fp_info::Params_v2*, Resp, (), (override));
  };
  MockFpInfoCommand_v2 mock_fp_info_command_;
};

TEST_F(FpInfoCommand_v2_ErrorsTest, Errors_None) {
  EXPECT_CALL(mock_fp_info_command_, Resp).WillOnce(Return(nullptr));

  EXPECT_EQ(mock_fp_info_command_.GetFpSensorErrors(), FpSensorErrors::kNone);
}

TEST_F(FpInfoCommand_v2_ErrorsTest, Errors_NoIrq) {
  struct fp_info::Params_v2 resp = {
      .info = {.sensor_info = {.errors = FP_ERROR_NO_IRQ |
                                         FP_ERROR_DEAD_PIXELS_UNKNOWN}}};

  EXPECT_CALL(mock_fp_info_command_, Resp).WillRepeatedly(Return(&resp));

  EXPECT_EQ(mock_fp_info_command_.GetFpSensorErrors(), FpSensorErrors::kNoIrq);
}

TEST_F(FpInfoCommand_v2_ErrorsTest, Errors_SpiCommunication) {
  struct fp_info::Params_v2 resp = {
      .info = {.sensor_info = {.errors = FP_ERROR_SPI_COMM |
                                         FP_ERROR_DEAD_PIXELS_UNKNOWN}}};

  EXPECT_CALL(mock_fp_info_command_, Resp).WillRepeatedly(Return(&resp));

  EXPECT_EQ(mock_fp_info_command_.GetFpSensorErrors(),
            FpSensorErrors::kSpiCommunication);
}

TEST_F(FpInfoCommand_v2_ErrorsTest, Errors_BadHardwareID) {
  struct fp_info::Params_v2 resp = {
      .info = {.sensor_info = {.errors = FP_ERROR_BAD_HWID |
                                         FP_ERROR_DEAD_PIXELS_UNKNOWN}}};

  EXPECT_CALL(mock_fp_info_command_, Resp).WillRepeatedly(Return(&resp));

  EXPECT_EQ(mock_fp_info_command_.GetFpSensorErrors(),
            FpSensorErrors::kBadHardwareID);
}

TEST_F(FpInfoCommand_v2_ErrorsTest, Errors_InitializationFailure) {
  struct fp_info::Params_v2 resp = {
      .info = {.sensor_info = {.errors = FP_ERROR_INIT_FAIL |
                                         FP_ERROR_DEAD_PIXELS_UNKNOWN}}};

  EXPECT_CALL(mock_fp_info_command_, Resp).WillRepeatedly(Return(&resp));

  EXPECT_EQ(mock_fp_info_command_.GetFpSensorErrors(),
            FpSensorErrors::kInitializationFailure);
}

TEST_F(FpInfoCommand_v2_ErrorsTest, Errors_DeadPixels_0) {
  struct fp_info::Params_v2 resp = {
      .info = {.sensor_info = {.errors = FP_ERROR_DEAD_PIXELS(0)}}};

  EXPECT_CALL(mock_fp_info_command_, Resp).WillRepeatedly(Return(&resp));

  EXPECT_EQ(mock_fp_info_command_.GetFpSensorErrors(), FpSensorErrors::kNone);
}

TEST_F(FpInfoCommand_v2_ErrorsTest, Errors_DeadPixels_1) {
  struct fp_info::Params_v2 resp = {
      .info = {.sensor_info = {.errors = FP_ERROR_DEAD_PIXELS(1)}}};

  EXPECT_CALL(mock_fp_info_command_, Resp).WillRepeatedly(Return(&resp));

  EXPECT_EQ(mock_fp_info_command_.GetFpSensorErrors(),
            FpSensorErrors::kDeadPixels);
}

TEST_F(FpInfoCommand_v2_ErrorsTest, Errors_Multiple) {
  struct fp_info::Params_v2 resp = {
      .info = {.sensor_info = {.errors = FP_ERROR_DEAD_PIXELS(1) |
                                         FP_ERROR_BAD_HWID}}};

  EXPECT_CALL(mock_fp_info_command_, Resp).WillRepeatedly(Return(&resp));

  EXPECT_EQ(mock_fp_info_command_.GetFpSensorErrors(),
            FpSensorErrors::kDeadPixels | FpSensorErrors::kBadHardwareID);
}

/** Tests FpInfoCommand_v2's "NumDeadPixels()" method. */
class FpInfoCommand_v2_NumDeadPixelsTest : public testing::Test {
 public:
  class MockFpInfoCommand_v2 : public FpInfoCommand_v2 {
   public:
    MOCK_METHOD(fp_info::Params_v2*, Resp, (), (override));
  };
  MockFpInfoCommand_v2 mock_fp_info_command_;
};

TEST_F(FpInfoCommand_v2_NumDeadPixelsTest, NoResponse) {
  EXPECT_CALL(mock_fp_info_command_, Resp).WillRepeatedly(Return(nullptr));

  const auto expected = FpInfoCommand::kDeadPixelsUnknown;
  EXPECT_EQ(mock_fp_info_command_.NumDeadPixels(), expected);
}

TEST_F(FpInfoCommand_v2_NumDeadPixelsTest, DeadPixelsUnknown) {
  struct fp_info::Params_v2 resp = {
      .info = {.sensor_info = {.errors = FP_ERROR_BAD_HWID |
                                         FP_ERROR_DEAD_PIXELS_UNKNOWN}}};

  EXPECT_CALL(mock_fp_info_command_, Resp).WillRepeatedly(Return(&resp));

  const auto expected = FpInfoCommand::kDeadPixelsUnknown;
  EXPECT_EQ(mock_fp_info_command_.NumDeadPixels(), expected);
}

TEST_F(FpInfoCommand_v2_NumDeadPixelsTest, ZeroDeadPixels) {
  struct fp_info::Params_v2 resp = {
      .info = {.sensor_info = {.errors = FP_ERROR_INIT_FAIL |
                                         FP_ERROR_DEAD_PIXELS(0)}}};

  EXPECT_CALL(mock_fp_info_command_, Resp).WillRepeatedly(Return(&resp));

  EXPECT_EQ(mock_fp_info_command_.NumDeadPixels(), 0);
}

TEST_F(FpInfoCommand_v2_NumDeadPixelsTest, OneDeadPixel) {
  struct fp_info::Params_v2 resp = {
      .info = {.sensor_info = {.errors = FP_ERROR_SPI_COMM |
                                         FP_ERROR_DEAD_PIXELS(1)}}};

  EXPECT_CALL(mock_fp_info_command_, Resp).WillRepeatedly(Return(&resp));

  EXPECT_EQ(mock_fp_info_command_.NumDeadPixels(), 1);
}

/**
 * Tests FpInfoCommand_v2's "sensor_id" method.
 */
class FpInfoCommand_v2_SensorIdTest : public testing::Test {
 public:
  class MockFpInfoCommand_v2 : public FpInfoCommand_v2 {
   public:
    MOCK_METHOD(fp_info::Params_v2*, Resp, (), (override));
  };
  MockFpInfoCommand_v2 mock_fp_info_command;
};

TEST_F(FpInfoCommand_v2_SensorIdTest, NullResponse) {
  EXPECT_CALL(mock_fp_info_command, Resp).WillRepeatedly(Return(nullptr));

  EXPECT_EQ(mock_fp_info_command.sensor_id(), std::nullopt);
}

TEST_F(FpInfoCommand_v2_SensorIdTest, ValidSensorId) {
  struct fp_info::Params_v2 resp = {.info = {.sensor_info = {.vendor_id = 1,
                                                             .product_id = 2,
                                                             .model_id = 3,
                                                             .version = 4}}};
  EXPECT_CALL(mock_fp_info_command, Resp).WillRepeatedly(Return(&resp));

  EXPECT_TRUE(mock_fp_info_command.sensor_id().has_value());
  EXPECT_EQ(mock_fp_info_command.sensor_id()->vendor_id, 1);
  EXPECT_EQ(mock_fp_info_command.sensor_id()->product_id, 2);
  EXPECT_EQ(mock_fp_info_command.sensor_id()->model_id, 3);
  EXPECT_EQ(mock_fp_info_command.sensor_id()->version, 4);
}

/**
 * Tests FpInfoCommand_v2's "sensor_image" method.
 */
class FpInfoCommand_v2_SensorImageTest : public testing::Test {
 public:
  class MockFpInfoCommand_v2 : public FpInfoCommand_v2 {
   public:
    MOCK_METHOD(fp_info::Params_v2*, Resp, (), (override));
  };
  MockFpInfoCommand_v2 mock_fp_info_command;
};

TEST_F(FpInfoCommand_v2_SensorImageTest, NullResponse) {
  EXPECT_CALL(mock_fp_info_command, Resp).WillRepeatedly(Return(nullptr));

  EXPECT_TRUE(mock_fp_info_command.sensor_image().empty());
}

TEST_F(FpInfoCommand_v2_SensorImageTest, ZeroCaptureImages) {
  struct fp_info::Params_v2 resp = {.info = {.sensor_info = {
                                                 .num_capture_types = 0,
                                             }}};
  EXPECT_CALL(mock_fp_info_command, Resp).WillRepeatedly(Return(&resp));
  EXPECT_TRUE(mock_fp_info_command.sensor_image().empty());
}

TEST_F(FpInfoCommand_v2_SensorImageTest, ValidSensorImage) {
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

  EXPECT_CALL(mock_fp_info_command, Resp).WillRepeatedly(Return(&resp));

  auto result = mock_fp_info_command.sensor_image();

  EXPECT_THAT(result, SizeIs(2));
  EXPECT_EQ(result[0].frame_size, 5120);
  EXPECT_EQ(result[0].pixel_format, 0x59455247);
  EXPECT_EQ(result[0].width, 64);
  EXPECT_EQ(result[0].height, 80);
  EXPECT_EQ(result[0].bpp, 8);
  EXPECT_EQ(result[1].frame_size, 36864);
  EXPECT_EQ(result[1].pixel_format, 0x59455247);
  EXPECT_EQ(result[1].width, 192);
  EXPECT_EQ(result[1].height, 96);
  EXPECT_EQ(result[1].bpp, 16);
}

/**
 * Tests FpInfoCommand_v2's "template_info" method.
 */
class FpInfoCommand_v2_TemplateInfoTest : public testing::Test {
 public:
  class MockFpInfoCommand_v2 : public FpInfoCommand_v2 {
   public:
    MOCK_METHOD(fp_info::Params_v2*, Resp, (), (override));
  };
  MockFpInfoCommand_v2 mock_fp_info_command;
};

TEST_F(FpInfoCommand_v2_TemplateInfoTest, NullResponse) {
  EXPECT_CALL(mock_fp_info_command, Resp).WillRepeatedly(Return(nullptr));

  EXPECT_EQ(mock_fp_info_command.template_info(), std::nullopt);
}

TEST_F(FpInfoCommand_v2_TemplateInfoTest, ValidTemplateInfo) {
  struct fp_info::Params_v2 resp = {
      .info = {.template_info = {.template_size = 1024,
                                 .template_max = 4,
                                 .template_valid = 3,
                                 .template_dirty = 1 << 3,
                                 .template_version = 1}}};

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
