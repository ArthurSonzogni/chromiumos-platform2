// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_info_params.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libec/ec_command.h"

namespace ec {
namespace {

using ::testing::SizeIs;

TEST(FpInfoParams, ValidateSize) {
  // Process for calculating the size of image_frame_params in Params_v2:
  // - The maximum packet size allowed: kMaxPacketSize = 544 bytes.
  // - A fixed-size header (Header_v2) needs to be accounted for.
  // - Header_v2 is composed of:
  //   - fp_template_info, which is 16 bytes.
  //   - fp_sensor_info, which is 20 bytes.
  //   - Therefore, sizeof(Header_v2) = 16 + 20 = 36 bytes.
  // - The remaining space in the packet is then available for multiple
  // instances of fp_image_frame_params.
  // - Each fp_image_frame_params structure has a size of 16 bytes.
  // - To find the maximum number of fp_image_frame_params that can fit, we
  // calculate:
  //   (kMaxPacketSize - sizeof(Header_v2)) / sizeof(fp_image_frame_params)
  //   = (544 - 36) / 16 = 31.75
  // - Since we can only fit whole structures, the resulting size of
  //   fp_info::Params_v2().image_frame_params is 31.
  EXPECT_THAT(fp_info::Params_v2().image_frame_params, SizeIs(31));
}

TEST(FpInfoParams, ImageFrameParamsEqual) {
  struct fp_image_frame_params expected_image_frame_params_0 = {
      .frame_size = 5120,
      .pixel_format = 0x59455247,
      .width = 64,
      .height = 80,
      .bpp = 8,
      .fp_capture_type = 2,
      .reserved = 0};
  struct fp_image_frame_params expected_image_frame_params_1 = {
      .frame_size = 5120,
      .pixel_format = 0x59455247,
      .width = 64,
      .height = 80,
      .bpp = 8,
      .fp_capture_type = 2,
      .reserved = 0};
  EXPECT_EQ(expected_image_frame_params_0, expected_image_frame_params_1);
}

TEST(FpInfoParams, ImageFrameParamsNotEqual) {
  struct fp_image_frame_params expected_image_frame_params_0 = {
      .frame_size = 5120,
      .pixel_format = 0x59455247,
      .width = 64,
      .height = 80,
      .bpp = 8,
      .fp_capture_type = 2,
      .reserved = 0};
  struct fp_image_frame_params expected_image_frame_params_1 = {
      .frame_size = 5120,
      .pixel_format = 0x59455247,
      .width = 64,
      .height = 80,
      .bpp = 16,
      .fp_capture_type = 2,
      .reserved = 0};
  EXPECT_FALSE(expected_image_frame_params_0 == expected_image_frame_params_1);
}

TEST(FpInfoParams, HeaderSize) {
  EXPECT_EQ(sizeof(fp_info::Header_v2::sensor_info),
            sizeof(ec_response_fp_info_v2::sensor_info));
  EXPECT_EQ(sizeof(fp_info::Header_v2::template_info),
            sizeof(ec_response_fp_info_v2::template_info));
}

TEST(FpInfoParams, ParamsSize) {
  // Unused space calculation within the kMaxPacketSize:
  // - The maximum packet size: kMaxPacketSize = 544 bytes.
  // - The header (Header_v2) occupies 36 bytes.
  // - We can fit 31 instances of fp_image_frame_params, each 16 bytes in size:
  //   The total space needed is 31 * 16 = 496 bytes.
  // - The remaining space in the packet is 544 - 36 - 496 = 12 bytes.
  EXPECT_EQ(sizeof(fp_info::Params_v2), kMaxPacketSize - 12);
}

}  // namespace
}  // namespace ec
