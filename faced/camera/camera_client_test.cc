// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "faced/camera/camera_client.h"

#include <memory>
#include <string>

#include <base/task/thread_pool.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <linux/videodev2.h>

#include "faced/camera/fake_camera_service.h"
#include "faced/camera/test_utils.h"
#include "faced/testing/status.h"
#include "faced/util/blocking_future.h"
#include "faced/util/task.h"

namespace faced {
namespace {

// All camera formats are supported
bool AllSupported(int width, int height) {
  return true;
}

// No camera formats are supported
//
// Returns false for function of type
// std::function<bool(int width, int height)> is_supported
bool NoneSupported(int width, int height) {
  return false;
}

// Tests free functions not using CameraClient

// Tests conversions of common fourcc codes
TEST(FourccToStringTest, FourccToString) {
  EXPECT_EQ(FourccToString(V4L2_PIX_FMT_NV12), "NV12");
  EXPECT_EQ(FourccToString(V4L2_PIX_FMT_MJPEG), "MJPG");

  // Codes with unprintable characters are just printed as hex.
  EXPECT_EQ(FourccToString(0x00112233), "0x00112233");
}

// Tests IsFormatEqual for identical and different formats
TEST(IsFormatEqualTest, IsFormatEqual) {
  EXPECT_TRUE(
      IsFormatEqual(testing::kYuvHighDefCamera, testing::kYuvHighDefCamera));
  EXPECT_FALSE(
      IsFormatEqual(testing::kYuvHighDefCamera, testing::kYuvStdDefCamera));
}

// Tests CameraClient::Create()
// Tests that the camera client is able to probe info for a single fake camera
// info.
// Case where is_removed = false
TEST(CameraClientTest, Create1) {
  // Required for threading in tests
  base::test::TaskEnvironment task_environment;

  testing::FakeCameraService fake_camera_service_connector;
  testing::CameraSet yuv_camera_set = testing::YuvCameraSet();
  fake_camera_service_connector.AddCameraInfo(yuv_camera_set.camera_info,
                                              /*is_removed=*/false);

  // Create a camera client.
  FACE_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<CameraClient> camera_client,
      CameraClient::Create(std::make_unique<testing::FakeCameraService>(
          fake_camera_service_connector)));

  // Check that formats are available
  for (const cros_cam_format_info_t& format_info :
       yuv_camera_set.format_infos) {
    EXPECT_TRUE(camera_client->FormatIsAvailable(yuv_camera_set.camera_id,
                                                 format_info));
  }

  // Expect GetMaxSupportedResolutionFormat to return a valid format equal to
  // the yuv format with the largest resolution
  std::optional<cros_cam_format_info_t> max_resolution_format =
      camera_client->GetMaxSupportedResolutionFormat(
          yuv_camera_set.camera_id, yuv_camera_set.format_infos[0].fourcc,
          &AllSupported);

  EXPECT_NE(max_resolution_format, std::nullopt);
  EXPECT_TRUE(IsFormatEqual(max_resolution_format.value(),
                            yuv_camera_set.format_infos[0]));

  // Expect GetMaxSupportedResolutionFormat to return no valid format when
  // passed in NoneSupported
  std::optional<cros_cam_format_info_t> max_resolution_format2 =
      camera_client->GetMaxSupportedResolutionFormat(
          yuv_camera_set.camera_id, yuv_camera_set.format_infos[0].fourcc,
          &NoneSupported);

  EXPECT_EQ(max_resolution_format2, std::nullopt);
}

// Tests CameraClient::Create()
// Tests that the camera client is able to probe info for a single fake camera
// info.
// Case where is_removed = true
TEST(CameraClientTest, Create2) {
  // Required for threading in tests
  base::test::TaskEnvironment task_environment;

  testing::FakeCameraService fake_camera_service_connector;
  testing::CameraSet yuv_camera_set = testing::YuvCameraSet();
  fake_camera_service_connector.AddCameraInfo(yuv_camera_set.camera_info,
                                              /*is_removed=*/true);

  // Create a camera client.
  FACE_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<CameraClient> camera_client,
      CameraClient::Create(std::make_unique<testing::FakeCameraService>(
          fake_camera_service_connector)));

  // Check that formats are unavailable
  for (const cros_cam_format_info_t& format_info :
       yuv_camera_set.format_infos) {
    EXPECT_FALSE(camera_client->FormatIsAvailable(yuv_camera_set.camera_id,
                                                  format_info));
  }

  // Expect GetMaxSupportedResolutionFormat to return no valid format
  std::optional<cros_cam_format_info_t> max_resolution_format =
      camera_client->GetMaxSupportedResolutionFormat(
          yuv_camera_set.camera_id, yuv_camera_set.format_infos[0].fourcc,
          &AllSupported);

  EXPECT_EQ(max_resolution_format, std::nullopt);
}

// Tests CameraClient::Create()
// Tests that the camera client is able to process multiple fake camera infos
TEST(CameraClientTest, Create3) {
  // Required for threading in tests
  base::test::TaskEnvironment task_environment;

  testing::FakeCameraService fake_camera_service_connector;

  testing::CameraSet yuv_camera_set = testing::YuvCameraSet();
  fake_camera_service_connector.AddCameraInfo(yuv_camera_set.camera_info,
                                              /*is_removed=*/false);

  testing::CameraSet mjpg_camera_set = testing::MjpgCameraSet();
  fake_camera_service_connector.AddCameraInfo(mjpg_camera_set.camera_info,
                                              /*is_removed=*/true);

  // Create a camera client.
  FACE_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<CameraClient> camera_client,
      CameraClient::Create(std::make_unique<testing::FakeCameraService>(
          fake_camera_service_connector)));

  // Check that formats from the first camera info (is_removed = false) are
  // available
  for (const cros_cam_format_info_t& format_info :
       yuv_camera_set.format_infos) {
    EXPECT_TRUE(camera_client->FormatIsAvailable(yuv_camera_set.camera_id,
                                                 format_info));
  }

  // Expect GetMaxSupportedResolutionFormat to return a valid format equal to
  // the yuv format with the largest resolution
  std::optional<cros_cam_format_info_t> max_resolution_format_yuv =
      camera_client->GetMaxSupportedResolutionFormat(
          yuv_camera_set.camera_id, yuv_camera_set.format_infos[0].fourcc,
          &AllSupported);

  EXPECT_NE(max_resolution_format_yuv, std::nullopt);
  EXPECT_TRUE(IsFormatEqual(max_resolution_format_yuv.value(),
                            yuv_camera_set.format_infos[0]));

  // Check that formats from the second camera info (is_removed = true) are
  // unavailable
  for (const cros_cam_format_info_t& format_info :
       mjpg_camera_set.format_infos) {
    EXPECT_FALSE(camera_client->FormatIsAvailable(mjpg_camera_set.camera_id,
                                                  format_info));
  }

  // Expect GetMaxSupportedResolutionFormat to return no valid format for mjpg
  std::optional<cros_cam_format_info_t> max_resolution_format_mjpg =
      camera_client->GetMaxSupportedResolutionFormat(
          mjpg_camera_set.camera_id, mjpg_camera_set.format_infos[0].fourcc,
          &AllSupported);

  EXPECT_EQ(max_resolution_format_mjpg, std::nullopt);
}

// Tests CameraClient::Create()
// Contrived setup where a camera is added and then removed
TEST(CameraClientTest, Create4) {
  // Required for threading in tests
  base::test::TaskEnvironment task_environment;

  testing::FakeCameraService fake_camera_service_connector;

  testing::CameraSet yuv_camera_set = testing::YuvCameraSet();
  fake_camera_service_connector.AddCameraInfo(yuv_camera_set.camera_info,
                                              /*is_removed=*/false);
  fake_camera_service_connector.AddCameraInfo(yuv_camera_set.camera_info,
                                              /*is_removed=*/true);

  // Create a camera client.
  FACE_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<CameraClient> camera_client,
      CameraClient::Create(std::make_unique<testing::FakeCameraService>(
          fake_camera_service_connector)));

  // Check that formats are unavailable
  for (const cros_cam_format_info_t& format_info :
       yuv_camera_set.format_infos) {
    EXPECT_FALSE(camera_client->FormatIsAvailable(yuv_camera_set.camera_id,
                                                  format_info));
  }

  // Expect GetMaxSupportedResolutionFormat to return no valid format
  std::optional<cros_cam_format_info_t> max_resolution_format =
      camera_client->GetMaxSupportedResolutionFormat(
          yuv_camera_set.camera_id, yuv_camera_set.format_infos[0].fourcc,
          &AllSupported);

  EXPECT_EQ(max_resolution_format, std::nullopt);
}

// Simple subclass of FrameProcessor that processes a certain number of
// frames then stops.
class SimpleFrameProcessor : public FrameProcessor {
 public:
  explicit SimpleFrameProcessor(int num_frames_to_process)
      : num_frames_to_process_(num_frames_to_process) {}

  // Increments the frame counter and calls processing_complete_ when the
  // requested number of frames have been processed.
  void ProcessFrame(std::unique_ptr<Frame> frame,
                    ProcessFrameDoneCallback done) override {
    num_frames_processed_++;

    if (num_frames_processed_ == num_frames_to_process_) {
      PostToCurrentSequence(base::BindOnce(std::move(done), absl::OkStatus()));
      return;
    }

    PostToCurrentSequence(base::BindOnce(std::move(done), std::nullopt));
  }

  // Returns how many frames have been processed.
  int FramesProcessed() { return num_frames_processed_; }

 private:
  int num_frames_processed_ = 0;
  int num_frames_to_process_ = 0;
};

// Tests CameraClient::CaptureFrames() with a custom FrameProcessor
TEST(CameraClientTest, CaptureFrames) {
  // Required for threading in tests
  base::test::TaskEnvironment task_environment{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  testing::FakeCameraService fake_camera_service_connector;

  testing::CameraSet yuv_camera_set = testing::YuvCameraSet();
  const int frames_available = 10;
  const int frames_to_process = 5;
  fake_camera_service_connector.AddCameraInfo(yuv_camera_set.camera_info,
                                              /*is_removed=*/false);

  // Add frames available from the fake camera
  for (int i = 0; i < frames_available; i++) {
    fake_camera_service_connector.AddResult(yuv_camera_set.result);
  }

  // Create a camera client.
  FACE_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<CameraClient> camera_client,
      CameraClient::Create(std::make_unique<testing::FakeCameraService>(
          fake_camera_service_connector)));

  BlockingFuture<absl::Status> status;
  auto frame_processor =
      base::MakeRefCounted<SimpleFrameProcessor>(frames_to_process);
  camera_client->CaptureFrames(
      {.camera_id = 0, .format = yuv_camera_set.format_infos[0]},
      frame_processor, status.PromiseCallback());

  EXPECT_TRUE(status.Wait().ok());
  ASSERT_EQ(frame_processor->FramesProcessed(), frames_to_process);
}
}  // namespace
}  // namespace faced
