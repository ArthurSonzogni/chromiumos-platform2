// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "camera3_test/camera3_preview_fixture.h"

#include <unistd.h>

#include <base/bind.h>
#include <base/command_line.h>
#include <base/strings/string_number_conversions.h>

namespace camera3_test {

void Camera3PreviewFixture::SetUp() {
  ASSERT_EQ(0, cam_service_.Initialize(
                   Camera3Service::ProcessStillCaptureResultCallback(),
                   Camera3Service::ProcessRecordingResultCallback()))
      << "Failed to initialize camera service";
}

void Camera3PreviewFixture::TearDown() {
  cam_service_.Destroy();
}

// Test parameters:
// - Camera ID
class Camera3SinglePreviewTest : public Camera3PreviewFixture,
                                 public ::testing::WithParamInterface<int32_t> {
 public:
  const uint32_t kNumPreviewFrames = 10;
  const uint32_t kTimeoutMsPerFrame = 1000;

  Camera3SinglePreviewTest()
      : Camera3PreviewFixture(std::vector<int>(1, GetParam())),
        cam_id_(GetParam()) {}

 protected:
  int cam_id_;
};

TEST_P(Camera3SinglePreviewTest, Camera3BasicPreviewTest) {
  auto resolutions =
      cam_service_.GetStaticInfo(cam_id_)->GetSortedOutputResolutions(
          HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED);
  for (const auto& resolution : resolutions) {
    ResolutionInfo jpeg_resolution(0, 0), recording_resolution(0, 0);
    ASSERT_EQ(0, cam_service_.StartPreview(cam_id_, resolution, jpeg_resolution,
                                           recording_resolution))
        << "Starting preview fails";
    ASSERT_EQ(0, cam_service_.WaitForPreviewFrames(cam_id_, kNumPreviewFrames,
                                                   kTimeoutMsPerFrame));
    cam_service_.StopPreview(cam_id_);
  }
}

// Test parameters:
// - Camera ID
class Camera3FaceDetectionTest : public Camera3PreviewFixture,
                                 public ::testing::WithParamInterface<int32_t> {
 public:
  const uint32_t kNumPreviewFrames = 20;
  const uint32_t kTimeoutMsPerFrame = 1000;
  Camera3FaceDetectionTest()
      : Camera3PreviewFixture(std::vector<int>(1, GetParam())),
        cam_id_(GetParam()),
        expected_num_faces_(GetCommandLineFaceDetectNumber()) {}

 protected:
  void SetUp() override;

  void ProcessPreviewResult(int cam_id,
                            uint32_t frame_number,
                            ScopedCameraMetadata metadata);

  int GetCommandLineFaceDetectNumber() {
    std::string switch_value =
        base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
            "expected_num_faces");
    if (switch_value == "") {
      return -1;
    }
    int value;
    if (!base::StringToInt(switch_value, &value)) {
      LOG(ERROR) << "Failed to convert " << switch_value << " to int";
      return -1;
    }
    if (value < 0) {
      return -1;
    }
    return value;
  }

  void CheckNumOfFaces(int num_faces);

  int cam_id_;
  int expected_num_faces_;

 private:
  ScopedCameraMetadata result_metadata_;
};

void Camera3FaceDetectionTest::SetUp() {
  ASSERT_EQ(0, cam_service_.Initialize(
                   Camera3Service::ProcessStillCaptureResultCallback(),
                   Camera3Service::ProcessRecordingResultCallback(),
                   base::Bind(&Camera3FaceDetectionTest::ProcessPreviewResult,
                              base::Unretained(this))))
      << "Failed to initialize camera service";
}

void Camera3FaceDetectionTest::ProcessPreviewResult(
    int cam_id, uint32_t /*frame_number*/, ScopedCameraMetadata metadata) {
  VLOGF_ENTER();
  result_metadata_ = std::move(metadata);
}

void Camera3FaceDetectionTest::CheckNumOfFaces(int num_faces) {
  ASSERT_NE(nullptr, result_metadata_.get())
      << "Result metadata is unavailable";
  camera_metadata_ro_entry_t entry;
  int result = find_camera_metadata_ro_entry(
      result_metadata_.get(), ANDROID_STATISTICS_FACE_RECTANGLES, &entry);
  // Accept no rectangles.
  if (num_faces == 0 && result != 0) {
    return;
  }
  ASSERT_EQ(0, result)
      << "Metadata key ANDROID_STATISTICS_FACE_RECTANGLES not found";
  EXPECT_EQ(num_faces * 4, entry.count)
      << "Expect face rectangles size " << num_faces * 4 << " but detected "
      << entry.count;
  ASSERT_EQ(
      0, find_camera_metadata_ro_entry(result_metadata_.get(),
                                       ANDROID_STATISTICS_FACE_SCORES, &entry))
      << "Metadata key ANDROID_STATISTICS_FACE_SCORES not found";
  EXPECT_EQ(num_faces, entry.count)
      << "Expect " << num_faces << " faces, but detected " << entry.count
      << " faces";
  result_metadata_.reset();
}

TEST_P(Camera3FaceDetectionTest, Detection) {
  // Run only if --expected_num_faces argument presented.
  if (expected_num_faces_ < 0) {
    GTEST_SKIP();
  }

  auto IsAFSupported = [this]() {
    std::vector<uint8_t> available_af_modes;
    cam_service_.GetStaticInfo(cam_id_)->GetAvailableAFModes(
        &available_af_modes);
    uint8_t af_modes[] = {ANDROID_CONTROL_AF_MODE_AUTO,
                          ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE,
                          ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO,
                          ANDROID_CONTROL_AF_MODE_MACRO};
    for (const auto& it : af_modes) {
      if (std::find(available_af_modes.begin(), available_af_modes.end(), it) !=
          available_af_modes.end()) {
        return true;
      }
    }
    return false;
  };

  ASSERT_TRUE(cam_service_.GetStaticInfo(cam_id_)->IsKeyAvailable(
      ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES))
      << "NO ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES key in static "
         "info";
  std::set<uint8_t> face_detect_modes;
  ASSERT_EQ(0, cam_service_.GetStaticInfo(cam_id_)->GetAvailableFaceDetectModes(
                   &face_detect_modes) != 0)
      << "Failed to get face detect modes";
  ASSERT_NE(face_detect_modes.find(ANDROID_STATISTICS_FACE_DETECT_MODE_SIMPLE),
            face_detect_modes.end())
      << "Can't find ANDROID_STATISTICS_FACE_DETECT_MODE_SIMPLE";

  auto resolution =
      cam_service_.GetStaticInfo(cam_id_)
          ->GetSortedOutputResolutions(HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED)
          .back();
  ResolutionInfo jpeg_resolution(0, 0), recording_resolution(0, 0);
  ASSERT_EQ(0, cam_service_.StartPreview(cam_id_, resolution, jpeg_resolution,
                                         recording_resolution))
      << "Starting preview fails";

  // Trigger an auto focus run, and wait for AF locked.
  if (IsAFSupported()) {
    cam_service_.StartAutoFocus(cam_id_);
    ASSERT_EQ(0, cam_service_.WaitForAutoFocusDone(cam_id_))
        << "Wait for auto focus done timed out";
  }
  // Wait for AWB converged, then lock it.
  ASSERT_EQ(0, cam_service_.WaitForAWBConvergedAndLock(cam_id_))
      << "Wait for AWB converged timed out";

  // Trigger an AE precapture metering sequence and wait for AE converged.
  cam_service_.StartAEPrecapture(cam_id_);
  ASSERT_EQ(0, cam_service_.WaitForAEStable(cam_id_))
      << "Wait for AE stable timed out";

  // Check there is no face detected before enabling face detection
  ASSERT_EQ(0, cam_service_.WaitForPreviewFrames(cam_id_, kNumPreviewFrames,
                                                 kTimeoutMsPerFrame));
  CheckNumOfFaces(0);

  cam_service_.StartFaceDetection(cam_id_);
  ASSERT_EQ(0, cam_service_.WaitForPreviewFrames(cam_id_, kNumPreviewFrames,
                                                 kTimeoutMsPerFrame));
  CheckNumOfFaces(expected_num_faces_);

  // Check no face detected after stop face detection
  cam_service_.StopFaceDetection(cam_id_);
  ASSERT_EQ(0, cam_service_.WaitForPreviewFrames(cam_id_, kNumPreviewFrames,
                                                 kTimeoutMsPerFrame));
  CheckNumOfFaces(0);

  cam_service_.StopPreview(cam_id_);
}

INSTANTIATE_TEST_SUITE_P(
    Camera3PreviewTest,
    Camera3SinglePreviewTest,
    ::testing::ValuesIn(Camera3Module().GetTestCameraIds()));

INSTANTIATE_TEST_SUITE_P(
    Camera3PreviewTest,
    Camera3FaceDetectionTest,
    ::testing::ValuesIn(Camera3Module().GetTestCameraIds()));

}  // namespace camera3_test
