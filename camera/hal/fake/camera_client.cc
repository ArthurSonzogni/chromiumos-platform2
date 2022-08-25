/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal/fake/camera_client.h"

#include "cros-camera/common.h"

#include "hal/fake/camera_hal.h"
#include "hal/fake/camera_hal_device_ops.h"

namespace cros {

CameraClient::CameraClient(int id,
                           const android::CameraMetadata& static_metadata,
                           const android::CameraMetadata& request_template,
                           const hw_module_t* module,
                           hw_device_t** hw_device)
    : id_(id),
      // This clones the metadata.
      static_metadata_(static_metadata),
      request_template_(request_template) {
  camera3_device_ = {
      .common =
          {
              .tag = HARDWARE_DEVICE_TAG,
              .version = CAMERA_DEVICE_API_VERSION_3_5,
              .module = const_cast<hw_module_t*>(module),
              .close = cros::camera_device_close,
          },
      .ops = &g_camera_device_ops,
      .priv = this,
  };
  *hw_device = &camera3_device_.common;

  DETACH_FROM_SEQUENCE(ops_sequence_checker_);
}

CameraClient::~CameraClient() {
  VLOGFID(1, id_);
}

int CameraClient::OpenDevice() {
  VLOGFID(1, id_);
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  return 0;
}

int CameraClient::CloseDevice() {
  VLOGFID(1, id_);
  DCHECK_CALLED_ON_VALID_SEQUENCE(ops_sequence_checker_);

  return 0;
}

int CameraClient::Initialize(const camera3_callback_ops_t* callback_ops) {
  VLOGFID(1, id_);
  DCHECK_CALLED_ON_VALID_SEQUENCE(ops_sequence_checker_);

  return -ENODEV;
}

int CameraClient::ConfigureStreams(
    camera3_stream_configuration_t* stream_config) {
  VLOGFID(1, id_);
  DCHECK_CALLED_ON_VALID_SEQUENCE(ops_sequence_checker_);

  return -ENODEV;
}

const camera_metadata_t* CameraClient::ConstructDefaultRequestSettings(
    int type) {
  VLOGFID(1, id_) << "type = " << type;
  DCHECK_CALLED_ON_VALID_SEQUENCE(ops_sequence_checker_);

  return nullptr;
}

int CameraClient::ProcessCaptureRequest(camera3_capture_request_t* request) {
  VLOGFID(1, id_);
  DCHECK_CALLED_ON_VALID_SEQUENCE(ops_sequence_checker_);

  return -ENODEV;
}

void CameraClient::Dump(int fd) {
  VLOGFID(1, id_);
}

int CameraClient::Flush(const camera3_device_t* dev) {
  VLOGFID(1, id_);
  DCHECK_CALLED_ON_VALID_SEQUENCE(ops_sequence_checker_);

  return -ENODEV;
}

}  // namespace cros
