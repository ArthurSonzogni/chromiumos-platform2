/*
 * Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/sw_privacy_switch_stream_manipulator.h"

#include <base/test/task_environment.h>
#include <hardware/camera3.h>
#include <libyuv.h>
#include <sync/sync.h>
#include <system/graphics.h>

// gtest's internal typedef of None and Bool conflicts with the None and Bool
// macros in X11/X.h (https://github.com/google/googletest/issues/371).
// X11/X.h is pulled in by the GL headers we include.
#pragma push_macro("None")
#pragma push_macro("Bool")
#undef None
#undef Bool
#include <gtest/gtest.h>
#pragma pop_macro("None")
#pragma pop_macro("Bool")

namespace cros {

namespace tests {

namespace {

constexpr size_t kWidth = 1280;
constexpr size_t kHeight = 720;
constexpr uint32_t kBufferUsage =
    GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN;

void WaitForReleaseFence(base::ScopedFD release_fence) {
  if (release_fence.is_valid()) {
    constexpr int kSyncWaitTimeoutMs = 300;
    ASSERT_EQ(sync_wait(release_fence.get(), kSyncWaitTimeoutMs), 0)
        << "sync_wait() timed out";
  }
}

void FillInFrameWithNonBlackColor(buffer_handle_t handle) {
  auto mapping = ScopedMapping(handle);
  ASSERT_TRUE(mapping.is_valid()) << "Failed to map buffer";
  for (uint32_t i = 0; i < mapping.num_planes(); ++i) {
    auto plane = mapping.plane(i);
    // 64 is a random number that results in a non-black NV12 frame when the
    // frame is filled in with that number.
    memset(plane.addr, 64, plane.size);
  }
}

bool IsBlackFrameNV12(const ScopedMapping& mapping) {
  auto y_plane = mapping.plane(0);
  for (size_t y = 0; y < kHeight; ++y) {
    for (size_t x = 0; x < kWidth; ++x) {
      if (*(y_plane.addr + y * y_plane.stride + x) != 0) {
        return false;
      }
    }
  }
  auto uv_plane = mapping.plane(1);
  for (size_t y = 0; y < kHeight / 2; ++y) {
    for (size_t x = 0; x < kWidth; ++x) {
      if (*(uv_plane.addr + y * uv_plane.stride + x) != 128) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace

// Environment to create CameraMojoChannelManagerToken and CameraBufferManager
// instances to be shared for all test cases.
class SWPrivacySwitchTestEnvironment;
SWPrivacySwitchTestEnvironment* g_env;

class SWPrivacySwitchTestEnvironment : public ::testing::Environment {
 public:
  SWPrivacySwitchTestEnvironment()
      : mojo_manager_token_(CameraMojoChannelManagerToken::CreateInstance()),
        camera_buffer_manager_(CameraBufferManager::GetInstance()) {}

  base::test::SingleThreadTaskEnvironment task_environment_;
  std::unique_ptr<CameraMojoChannelManagerToken> mojo_manager_token_;
  CameraBufferManager* camera_buffer_manager_;
};

class SWPrivacySwitchTest : public ::testing::Test {
 protected:
  SWPrivacySwitchTest()
      : runtime_options_(StreamManipulator::RuntimeOptions{
            .sw_privacy_switch_state = mojom::CameraPrivacySwitchState::OFF}),
        stream_manipulator_(&runtime_options_,
                            g_env->mojo_manager_token_.get()) {}

  Camera3CaptureDescriptor WrapWithCamera3CaptureDescriptorResult(
      buffer_handle_t* handle) const {
    auto stream = camera3_stream_t{
        .width = g_env->camera_buffer_manager_->GetWidth(*handle),
        .height = g_env->camera_buffer_manager_->GetHeight(*handle),
        .format = static_cast<int>(
            g_env->camera_buffer_manager_->GetHalPixelFormat(*handle))};
    auto stream_buffer =
        camera3_stream_buffer_t{.stream = &stream,
                                .buffer = handle,
                                .status = CAMERA3_BUFFER_STATUS_OK,
                                .acquire_fence = -1,
                                .release_fence = -1};
    return Camera3CaptureDescriptor(
        camera3_capture_result_t{.frame_number = 0,
                                 .num_output_buffers = 1,
                                 .output_buffers = &stream_buffer});
  }

  StreamManipulator::RuntimeOptions runtime_options_;
  SWPrivacySwitchStreamManipulator stream_manipulator_;
};

TEST_F(SWPrivacySwitchTest, NV12Output) {
  ScopedBufferHandle handle =
      g_env->camera_buffer_manager_->AllocateScopedBuffer(
          kWidth, kHeight, HAL_PIXEL_FORMAT_YCbCr_420_888, kBufferUsage);
  FillInFrameWithNonBlackColor(*handle);

  // When |sw_privacy_switch_state| is OFF.
  auto result = WrapWithCamera3CaptureDescriptorResult(handle.get());
  ASSERT_TRUE(stream_manipulator_.ProcessCaptureResult(&result))
      << "SWPrivacySwitchStreamManipulator::ProcessCaptureResult failed when"
         "|sw_privacy_switch_state| is OFF";
  WaitForReleaseFence(
      base::ScopedFD(result.GetOutputBuffers()[0].release_fence));
  {
    auto mapping = ScopedMapping(*handle);
    ASSERT_TRUE(mapping.is_valid()) << "Failed to map buffer";
    EXPECT_FALSE(IsBlackFrameNV12(mapping))
        << "The result frame shoud not be black, but is black";
  }

  // When |sw_privacy_switch_state| is ON.
  runtime_options_.sw_privacy_switch_state =
      mojom::CameraPrivacySwitchState::ON;
  result = WrapWithCamera3CaptureDescriptorResult(handle.get());
  ASSERT_TRUE(stream_manipulator_.ProcessCaptureResult(&result))
      << "SWPrivacySwitchStreamManipulator::ProcessCaptureResult failed when"
         "|sw_privacy_switch_state| was ON";
  WaitForReleaseFence(
      base::ScopedFD(result.GetOutputBuffers()[0].release_fence));
  {
    auto mapping = ScopedMapping(*handle);
    ASSERT_TRUE(mapping.is_valid()) << "Failed to map buffer";
    EXPECT_TRUE(IsBlackFrameNV12(mapping))
        << "The result frame shoud be black, but is not black";
  }
}

TEST_F(SWPrivacySwitchTest, JpegOutput) {
  // There is no need to initialize the JPEG frame for the testing purpose,
  // because SWPrivacySwitchStreamManipulator will ignore it. Without
  // initialization, the JPEG frame will be invalid.
  ScopedBufferHandle jpeg_handle =
      g_env->camera_buffer_manager_->AllocateScopedBuffer(
          kWidth, kHeight, HAL_PIXEL_FORMAT_BLOB, kBufferUsage);
  ScopedBufferHandle nv12_handle =
      g_env->camera_buffer_manager_->AllocateScopedBuffer(
          kWidth, kHeight, HAL_PIXEL_FORMAT_YCbCr_420_888, kBufferUsage);
  auto nv12_mapping = ScopedMapping(*nv12_handle);

  // When |sw_privacy_switch_state| is OFF, after
  // SWPrivacySwitchStreamManipulator::ProcessCaptureResult, the JPEG frame
  // should be still invalid and libyuv::MJPGToNV12 should fail, because
  // SWPrivacySwitchStreamManipulator should not change the frame.
  auto result = WrapWithCamera3CaptureDescriptorResult(jpeg_handle.get());
  ASSERT_TRUE(stream_manipulator_.ProcessCaptureResult(&result))
      << "SWPrivacySwitchStreamManipulator::ProcessCaptureResult failed"
         "when |sw_privacy_switch_state| was OFF";
  WaitForReleaseFence(
      base::ScopedFD(result.GetOutputBuffers()[0].release_fence));
  {
    auto jpeg_mapping = ScopedMapping(*jpeg_handle);
    ASSERT_TRUE(jpeg_mapping.is_valid()) << "Failed to map buffer";
    EXPECT_NE(libyuv::MJPGToNV12(
                  jpeg_mapping.plane(0).addr, jpeg_mapping.plane(0).size,
                  nv12_mapping.plane(0).addr, nv12_mapping.plane(0).stride,
                  nv12_mapping.plane(1).addr, nv12_mapping.plane(1).stride,
                  kWidth, kHeight, kWidth, kHeight),
              0)
        << "Decoding of the result JPEG frame should fail, but succeeded";
  }

  // When |sw_privacy_switch_state| is ON, after
  // SWPrivacySwitchStreamManipulator::ProcessCaptureResult, the JPEG frame
  // should be valid, libyuv::MJPGToNV12 should succeed, and the resulting NV12
  // frame should be black.
  runtime_options_.sw_privacy_switch_state =
      mojom::CameraPrivacySwitchState::ON;
  result = WrapWithCamera3CaptureDescriptorResult(jpeg_handle.get());
  ASSERT_TRUE(stream_manipulator_.ProcessCaptureResult(&result))
      << "SWPrivacySwitchStreamManipulator::ProcessCaptureResult failed"
         "when |sw_privacy_switch_state| was ON";
  WaitForReleaseFence(
      base::ScopedFD(result.GetOutputBuffers()[0].release_fence));
  {
    auto jpeg_mapping = ScopedMapping(*jpeg_handle);
    ASSERT_TRUE(jpeg_mapping.is_valid()) << "Failed to map buffer";
    ASSERT_EQ(libyuv::MJPGToNV12(
                  jpeg_mapping.plane(0).addr, jpeg_mapping.plane(0).size,
                  nv12_mapping.plane(0).addr, nv12_mapping.plane(0).stride,
                  nv12_mapping.plane(1).addr, nv12_mapping.plane(1).stride,
                  kWidth, kHeight, kWidth, kHeight),
              0)
        << "Decoding of the result JPEG frame should succeed, but failed";
    EXPECT_TRUE(IsBlackFrameNV12(nv12_mapping))
        << "The result frame is not black when the SW privacy switch is"
           "enabled";
  }
}

}  // namespace tests

}  // namespace cros

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  cros::tests::g_env =
      reinterpret_cast<cros::tests::SWPrivacySwitchTestEnvironment*>(
          testing::AddGlobalTestEnvironment(
              new cros::tests::SWPrivacySwitchTestEnvironment()));
  return RUN_ALL_TESTS();
}
