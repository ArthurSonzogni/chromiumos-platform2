// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FACED_CAMERA_FAKE_CAMERA_SERVICE_H_
#define FACED_CAMERA_FAKE_CAMERA_SERVICE_H_

#include <deque>
#include <string>
#include <vector>

#include <base/task/thread_pool.h>

#include "faced/camera/camera_service.h"

namespace faced::testing {

// FakeCameraService provides fake data for tests
class FakeCameraService : public CameraService {
 public:
  FakeCameraService() = default;

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

#endif  // FACED_CAMERA_FAKE_CAMERA_SERVICE_H_
