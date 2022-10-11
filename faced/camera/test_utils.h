// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FACED_CAMERA_TEST_UTILS_H_
#define FACED_CAMERA_TEST_UTILS_H_

#include <deque>
#include <string>
#include <vector>

#include <base/task/thread_pool.h>
#include <linux/videodev2.h>

#include "faced/camera/face_cli_camera_service_interface.h"

namespace faced::testing {
constexpr cros_cam_format_info_t kYuvHighDefCamera = {
    .fourcc = V4L2_PIX_FMT_NV12, .width = 1920, .height = 1080, .fps = 30};
constexpr cros_cam_format_info_t kYuvStdDefCamera = {
    .fourcc = V4L2_PIX_FMT_NV12, .width = 1280, .height = 720, .fps = 30};
constexpr cros_cam_format_info_t kMjpgCamera = {
    .fourcc = V4L2_PIX_FMT_MJPEG, .width = 1280, .height = 720, .fps = 25};

struct CameraSet {
  std::string camera_name;
  int camera_id;
  std::vector<cros_cam_format_info_t> format_infos;
  cros_cam_info_t camera_info;

  // Fake results
  std::vector<std::vector<uint8_t>> data;
  cros_cam_frame_t frame;
  cros_cam_capture_result_t_ result;
};

CameraSet YuvCameraSet();
CameraSet MjpgCameraSet();

// FakeCameraServiceConnector provides fake data for tests

class FakeCameraServiceConnector : public FaceCliCameraServiceInterface {
 public:
  FakeCameraServiceConnector() = default;

  // Helper function to add test camera infos
  void AddCameraInfo(cros_cam_info_t cam_info, bool is_removed);

  // Helper function to add test results
  void AddResult(cros_cam_capture_result_t result);

  // Init set to always return success
  int Init() override;

  // Exit set to always return success
  int Exit() override;

  // Calls callback to all cameras that have been added via AddCameraInfo
  int GetCameraInfo(cros_cam_get_cam_info_cb_t callback,
                    void* context) override;

  // Starts capturing with the given parameters using a sequenced task runner
  int StartCapture(const cros_cam_capture_request_t* request,
                   cros_cam_capture_cb_t callback,
                   void* context) override;

  // Clears all results.
  int StopCapture(int id) override;

 private:
  // Callback for continually feeding test results on sequenced task runner
  void StartCaptureCallback(const cros_cam_capture_request_t* request,
                            cros_cam_capture_cb_t callback,
                            void* context);

  void StopCaptureCallback();

  // Data for tests
  std::vector<cros_cam_info_t> camera_infos_;
  std::vector<bool> camera_is_removed_;

  std::deque<cros_cam_capture_result_t> results_;
  int camera_id_;

  // Runner for getting camera images
  scoped_refptr<base::SequencedTaskRunner> ops_runner_;
};
}  // namespace faced::testing

#endif  // FACED_CAMERA_TEST_UTILS_H_
