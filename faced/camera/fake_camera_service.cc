// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "faced/camera/fake_camera_service.h"

#include <vector>

#include <base/task/thread_pool.h>

namespace faced::testing {

void FakeCameraService::AddCameraInfo(cros_cam_info_t cam_info,
                                      bool is_removed) {
  camera_infos_.push_back(cam_info);
  camera_is_removed_.push_back(is_removed);
}

void FakeCameraService::AddResult(cros_cam_capture_result_t result) {
  results_.push_back(result);
}

int FakeCameraService::Init() {
  ops_runner_ = base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()});
  return 0;
}

int FakeCameraService::Exit() {
  return 0;
}

int FakeCameraService::GetCameraInfo(cros_cam_get_cam_info_cb_t callback,
                                     void* context) {
  for (int i = 0; i < camera_infos_.size(); i++) {
    if ((*callback)(context, &camera_infos_[i], camera_is_removed_[i]) != 0) {
      return 1;
    }
  }

  return 0;
}

int FakeCameraService::StartCapture(const cros_cam_capture_request_t* request,
                                    cros_cam_capture_cb_t callback,
                                    void* context) {
  camera_id_ = request->id;
  ops_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&FakeCameraService::StartCaptureCallback,
                     base::Unretained(this), request, callback, context));
  return 0;
}

int FakeCameraService::StopCapture(int id) {
  ops_runner_->PostTask(FROM_HERE,
                        base::BindOnce(&FakeCameraService::StopCaptureCallback,
                                       base::Unretained(this)));
  return 0;
}

void FakeCameraService::StartCaptureCallback(
    const cros_cam_capture_request_t* request,
    cros_cam_capture_cb_t callback,
    void* context) {
  DCHECK(ops_runner_->RunsTasksInCurrentSequence());

  if (results_.empty()) {
    return;
  }

  cros_cam_capture_result_t result = results_.front();
  results_.pop_front();

  bool should_continue = ((*callback)(context, &result) == 0);

  if (!should_continue) {
    StopCapture(camera_id_);
    return;
  }

  // Simulate 30fps
  ops_runner_->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&FakeCameraService::StartCaptureCallback,
                     base::Unretained(this), request, callback, context),
      base::Milliseconds(33));
}

void FakeCameraService::StopCaptureCallback() {
  DCHECK(ops_runner_->RunsTasksInCurrentSequence());
  results_.clear();
}

}  // namespace faced::testing
