// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "faced/camera/test_utils.h"

#include <vector>

namespace faced::testing {

CameraSet YuvCameraSet() {
  CameraSet yuv_camera_set;
  yuv_camera_set.camera_name = "TestYuvCamera";
  yuv_camera_set.camera_id = 0;
  yuv_camera_set.format_infos = {kYuvHighDefCamera, kYuvStdDefCamera};
  yuv_camera_set.camera_info = {
      .id = yuv_camera_set.camera_id,
      .facing = 0,
      .name = yuv_camera_set.camera_name.c_str(),
      .format_count = static_cast<int>(yuv_camera_set.format_infos.size()),
      .format_info = yuv_camera_set.format_infos.data()};

  // Create fake results
  int width = yuv_camera_set.format_infos[0].width;
  int height = yuv_camera_set.format_infos[0].height;

  yuv_camera_set.data = {std::vector<uint8_t>(width * height, 1),
                         std::vector<uint8_t>(width * (height + 1) / 2, 1)};
  yuv_camera_set.frame = {
      .format = yuv_camera_set.format_infos[0],
      .planes =
          {
              {.stride = width,
               .size = static_cast<int>(yuv_camera_set.data[0].size()),
               .data = yuv_camera_set.data[0].data()},
              {.stride = width,
               .size = static_cast<int>(yuv_camera_set.data[1].size()),
               .data = yuv_camera_set.data[1].data()},
          },
  };
  yuv_camera_set.result = {.status = 0, .frame = &yuv_camera_set.frame};
  return yuv_camera_set;
}

CameraSet MjpgCameraSet() {
  CameraSet mjpg_camera_set;
  mjpg_camera_set.camera_name = "TestMjpgCamera";
  mjpg_camera_set.camera_id = 1;
  mjpg_camera_set.format_infos = {kMjpgCamera};
  mjpg_camera_set.camera_info = {
      .id = mjpg_camera_set.camera_id,
      .facing = 0,
      .name = mjpg_camera_set.camera_name.c_str(),
      .format_count = static_cast<int>(mjpg_camera_set.format_infos.size()),
      .format_info = mjpg_camera_set.format_infos.data()};

  // Create fake results
  int width = mjpg_camera_set.format_infos[0].width;
  int height = mjpg_camera_set.format_infos[0].height;

  mjpg_camera_set.data = {std::vector<uint8_t>(width * height, 1)};
  mjpg_camera_set.frame = {
      .format = mjpg_camera_set.format_infos[0],
      .planes = {{.stride = width,
                  .size = width * height,
                  .data = mjpg_camera_set.data[0].data()}}};
  mjpg_camera_set.result = {.status = 0, .frame = &mjpg_camera_set.frame};
  return mjpg_camera_set;
}

void FakeCameraServiceConnector::AddCameraInfo(cros_cam_info_t cam_info,
                                               bool is_removed) {
  camera_infos_.push_back(cam_info);
  camera_is_removed_.push_back(is_removed);
}

void FakeCameraServiceConnector::AddResult(cros_cam_capture_result_t result) {
  results_.push_back(result);
}

int FakeCameraServiceConnector::Init() {
  ops_runner_ = base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()});
  return 0;
}

int FakeCameraServiceConnector::Exit() {
  return 0;
}

int FakeCameraServiceConnector::GetCameraInfo(
    cros_cam_get_cam_info_cb_t callback, void* context) {
  for (int i = 0; i < camera_infos_.size(); i++) {
    if ((*callback)(context, &camera_infos_[i], camera_is_removed_[i]) != 0) {
      return 1;
    }
  }

  return 0;
}

int FakeCameraServiceConnector::StartCapture(
    const cros_cam_capture_request_t* request,
    cros_cam_capture_cb_t callback,
    void* context) {
  camera_id_ = request->id;
  ops_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&FakeCameraServiceConnector::StartCaptureCallback,
                     base::Unretained(this), request, callback, context));
  return 0;
}

int FakeCameraServiceConnector::StopCapture(int id) {
  ops_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&FakeCameraServiceConnector::StopCaptureCallback,
                     base::Unretained(this)));
  return 0;
}

void FakeCameraServiceConnector::StartCaptureCallback(
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
      base::BindOnce(&FakeCameraServiceConnector::StartCaptureCallback,
                     base::Unretained(this), request, callback, context),
      base::Milliseconds(33));
}

void FakeCameraServiceConnector::StopCaptureCallback() {
  DCHECK(ops_runner_->RunsTasksInCurrentSequence());
  results_.clear();
}

}  // namespace faced::testing
