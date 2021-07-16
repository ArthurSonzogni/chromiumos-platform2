/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/camera_hal3_helpers.h"

#include <base/at_exit.h>
#include <camera/camera_metadata.h>
#include <gtest/gtest.h>

#include "cros-camera/common.h"

namespace cros {

TEST(Camera3CaptureDescriptor, BasicCaptureRequestCorrectnessTest) {
  android::CameraMetadata request_settings(10);
  std::array<uint8_t, 1> ae_mode{ANDROID_CONTROL_AE_MODE_ON};
  request_settings.update(ANDROID_CONTROL_AE_MODE, ae_mode.data(),
                          ae_mode.size());
  std::array<camera_metadata_rational_t, 1> ae_comp_step{
      camera_metadata_rational_t{1, 3}};
  request_settings.update(ANDROID_CONTROL_AE_COMPENSATION_STEP,
                          ae_comp_step.data(), ae_comp_step.size());
  std::vector<camera3_stream_buffer_t> output_buffers;
  output_buffers.push_back(camera3_stream_buffer_t());
  output_buffers.push_back(camera3_stream_buffer_t());

  constexpr uint32_t kTestFrameNumber = 15;
  camera3_capture_request_t request = {
      .frame_number = kTestFrameNumber,
      .settings = request_settings.getAndLock(),
      .input_buffer = nullptr,
      .num_output_buffers = static_cast<uint32_t>(output_buffers.size()),
      .output_buffers = output_buffers.data(),
      .num_physcam_settings = 0,
      .physcam_id = nullptr,
      .physcam_settings = nullptr,
  };

  Camera3CaptureDescriptor desc(request);

  // We should be able to get the AE_MODE and AE_COMPENSATION_STEP metadata and
  // their values should be ON and 1/3.
  {
    base::span<const uint8_t> ae_mode =
        desc.GetMetadata<uint8_t>(ANDROID_CONTROL_AE_MODE);
    EXPECT_EQ(ae_mode.size(), 1);
    EXPECT_EQ(ae_mode[0], ANDROID_CONTROL_AE_MODE_ON);
    base::span<const camera_metadata_rational_t> ae_comp_step =
        desc.GetMetadata<camera_metadata_rational_t>(
            ANDROID_CONTROL_AE_COMPENSATION_STEP);
    EXPECT_EQ(ae_comp_step.size(), 1);
    EXPECT_EQ(ae_comp_step[0].numerator, 1);
    EXPECT_EQ(ae_comp_step[0].denominator, 3);
  }

  // We should be able to modify the AE_MODE metadata to OFF, and add a new
  // AE_EXPOSURE_COMPENSATION metadata.
  {
    std::array<uint8_t, 1> ae_mode{ANDROID_CONTROL_AE_MODE_OFF};
    desc.UpdateMetadata(ANDROID_CONTROL_AE_MODE,
                        base::span<const uint8_t>(ae_mode));
    std::array<int32_t, 1> ae_compensation{1};
    desc.UpdateMetadata(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,
                        base::span<const int32_t>(ae_compensation));
    base::span<const uint8_t> ae_mode_entry =
        desc.GetMetadata<uint8_t>(ANDROID_CONTROL_AE_MODE);
    EXPECT_EQ(ae_mode_entry.size(), 1);
    EXPECT_EQ(ae_mode_entry[0], ANDROID_CONTROL_AE_MODE_OFF);
    base::span<const int32_t> ae_comp_entry =
        desc.GetMetadata<int32_t>(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION);
    EXPECT_EQ(ae_comp_entry.size(), 1);
    EXPECT_EQ(ae_comp_entry[0], 1);
  }

  // The input buffer should be nullptr initially, and we should be able to add
  // an input buffer.
  {
    EXPECT_EQ(desc.GetInputBuffer(), nullptr);
    // Use a fake stream pointer as cookie for test.
    camera3_stream_t* kFakeStreamPtr =
        reinterpret_cast<camera3_stream_t*>(0xbeef);
    desc.SetInputBuffer(camera3_stream_buffer_t{.stream = kFakeStreamPtr});
    EXPECT_EQ(desc.GetInputBuffer()->stream, kFakeStreamPtr);
  }

  // There should be two output buffers initially, and we should be able to add
  // a new output buffer.
  {
    base::span<const camera3_stream_buffer_t> output_buffers =
        desc.GetOutputBuffers();
    EXPECT_EQ(output_buffers.size(), 2);
    std::vector<camera3_stream_buffer_t> modified_output_buffers{
        output_buffers.begin(), output_buffers.end()};
    modified_output_buffers.push_back(camera3_stream_buffer_t());
    desc.SetOutputBuffers(modified_output_buffers);
    output_buffers = desc.GetOutputBuffers();
    EXPECT_EQ(output_buffers.size(), 3);
  }

  // Finally the locked camera3_capture_request_t should reflect all the changes
  // we made above.
  {
    const camera3_capture_request_t* locked_request = desc.LockForRequest();
    EXPECT_EQ(locked_request->frame_number, kTestFrameNumber);
    EXPECT_EQ(get_camera_metadata_entry_count(locked_request->settings), 3);
    EXPECT_NE(locked_request->input_buffer, nullptr);
    EXPECT_EQ(locked_request->num_output_buffers, 3);
  }

  // The metadata shouldn't be modified when the descriptor is locked.
  {
    std::array<uint8_t, 1> ae_mode{ANDROID_CONTROL_AE_MODE_ON};
    EXPECT_FALSE(desc.UpdateMetadata(ANDROID_CONTROL_AE_MODE,
                                     base::span<const uint8_t>(ae_mode)));
    base::span<const uint8_t> ae_mode_entry =
        desc.GetMetadata<uint8_t>(ANDROID_CONTROL_AE_MODE);
    EXPECT_EQ(ae_mode_entry.size(), 1);
    EXPECT_EQ(ae_mode_entry[0], ANDROID_CONTROL_AE_MODE_OFF);
  }
}

TEST(Camera3CaptureDescriptor, BasicCaptureResultCorrectnessTest) {
  android::CameraMetadata result_metadata(10);
  std::array<uint8_t, 1> ae_mode{ANDROID_CONTROL_AE_MODE_ON};
  result_metadata.update(ANDROID_CONTROL_AE_MODE, ae_mode.data(),
                         ae_mode.size());
  std::array<camera_metadata_rational_t, 1> ae_comp_step{
      camera_metadata_rational_t{1, 3}};
  result_metadata.update(ANDROID_CONTROL_AE_COMPENSATION_STEP,
                         ae_comp_step.data(), ae_comp_step.size());
  std::vector<camera3_stream_buffer_t> output_buffers;
  output_buffers.push_back(camera3_stream_buffer_t());
  output_buffers.push_back(camera3_stream_buffer_t());

  constexpr uint32_t kTestFrameNumber = 15;
  camera3_capture_result_t result = {
      .frame_number = kTestFrameNumber,
      .result = result_metadata.getAndLock(),
      .num_output_buffers = static_cast<uint32_t>(output_buffers.size()),
      .output_buffers = output_buffers.data(),
      .input_buffer = nullptr,
      .partial_result = 1,
      .num_physcam_metadata = 0,
      .physcam_ids = nullptr,
      .physcam_metadata = nullptr,
  };

  Camera3CaptureDescriptor desc(result);

  // We should be able to get the AE_MODE and AE_COMPENSATION_STEP metadata and
  // their values should be ON and 1/3.
  {
    base::span<const uint8_t> ae_mode =
        desc.GetMetadata<uint8_t>(ANDROID_CONTROL_AE_MODE);
    EXPECT_EQ(ae_mode.size(), 1);
    EXPECT_EQ(ae_mode[0], ANDROID_CONTROL_AE_MODE_ON);
    base::span<const camera_metadata_rational_t> ae_comp_step =
        desc.GetMetadata<camera_metadata_rational_t>(
            ANDROID_CONTROL_AE_COMPENSATION_STEP);
    EXPECT_EQ(ae_comp_step.size(), 1);
    EXPECT_EQ(ae_comp_step[0].numerator, 1);
    EXPECT_EQ(ae_comp_step[0].denominator, 3);
  }

  // We should be able to modify the AE_MODE metadata to OFF, and add a new
  // AE_EXPOSURE_COMPENSATION metadata.
  {
    std::array<uint8_t, 1> ae_mode{ANDROID_CONTROL_AE_MODE_OFF};
    desc.UpdateMetadata(ANDROID_CONTROL_AE_MODE,
                        base::span<const uint8_t>(ae_mode));
    std::array<int32_t, 1> ae_compensation{1};
    desc.UpdateMetadata(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,
                        base::span<const int32_t>(ae_compensation));
    base::span<const uint8_t> ae_mode_entry =
        desc.GetMetadata<uint8_t>(ANDROID_CONTROL_AE_MODE);
    EXPECT_EQ(ae_mode_entry.size(), 1);
    EXPECT_EQ(ae_mode_entry[0], ANDROID_CONTROL_AE_MODE_OFF);
    base::span<const int32_t> ae_comp_entry =
        desc.GetMetadata<int32_t>(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION);
    EXPECT_EQ(ae_comp_entry.size(), 1);
    EXPECT_EQ(ae_comp_entry[0], 1);
  }

  // The input buffer should be nullptr initially, and we should be able to add
  // an input buffer.
  {
    EXPECT_EQ(desc.GetInputBuffer(), nullptr);
    desc.SetInputBuffer(camera3_stream_buffer_t());
    EXPECT_NE(desc.GetInputBuffer(), nullptr);
  }

  // There should be two output buffers initially, and we should be able to add
  // a new output buffer.
  {
    base::span<const camera3_stream_buffer_t> output_buffers =
        desc.GetOutputBuffers();
    EXPECT_EQ(output_buffers.size(), 2);
    std::vector<camera3_stream_buffer_t> modified_output_buffers{
        output_buffers.begin(), output_buffers.end()};
    modified_output_buffers.push_back(camera3_stream_buffer_t());
    desc.SetOutputBuffers(modified_output_buffers);
    output_buffers = desc.GetOutputBuffers();
    EXPECT_EQ(output_buffers.size(), 3);
  }

  // Finally the locked camera3_capture_result_t should reflect all the changes
  // we made above.
  {
    const camera3_capture_result_t* locked_result = desc.LockForResult();
    EXPECT_EQ(locked_result->frame_number, kTestFrameNumber);
    EXPECT_EQ(get_camera_metadata_entry_count(locked_result->result), 3);
    EXPECT_NE(locked_result->input_buffer, nullptr);
    EXPECT_EQ(locked_result->num_output_buffers, 3);
    EXPECT_EQ(locked_result->partial_result, 1);
  }

  // The metadata shouldn't be modified when the descriptor is locked.
  {
    std::array<uint8_t, 1> ae_mode{ANDROID_CONTROL_AE_MODE_ON};
    EXPECT_FALSE(desc.UpdateMetadata(ANDROID_CONTROL_AE_MODE,
                                     base::span<const uint8_t>(ae_mode)));
    base::span<const uint8_t> ae_mode_entry =
        desc.GetMetadata<uint8_t>(ANDROID_CONTROL_AE_MODE);
    EXPECT_EQ(ae_mode_entry.size(), 1);
    EXPECT_EQ(ae_mode_entry[0], ANDROID_CONTROL_AE_MODE_OFF);
  }
}

}  // namespace cros

int main(int argc, char** argv) {
  base::AtExitManager exit_manager;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
