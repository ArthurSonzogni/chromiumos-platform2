// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FACED_CAMERA_FACED_CAMERA_SERVICE_H_
#define FACED_CAMERA_FACED_CAMERA_SERVICE_H_

#include <memory>
#include <string>

#include <base/strings/string_piece.h>

#include "faced/camera/face_cli_camera_service_interface.h"

namespace faced {

// FaceCliCameraService is a simple wrapper around cros::CameraServiceConnector
class FaceCliCameraService final : public FaceCliCameraServiceInterface {
 public:
  // Creates an instance of FaceCliCameraServiceConnector using a given
  // permission token
  static std::unique_ptr<FaceCliCameraService> Create(
      base::StringPiece token_path_string);

  // Initializes the connection to camera HAL dispatcher and registers the
  // camera HAL client. Must be called before any other functions.
  int Init() override;

  // Terminates camera HAL client, all connections and threads.
  int Exit() override;

  // Sets the callback for camera info changes and fires |callback| with the
  // info of the cameras currently present.
  int GetCameraInfo(cros_cam_get_cam_info_cb_t callback,
                    void* context) override;

  // Starts capturing with the given parameters.
  int StartCapture(const cros_cam_capture_request_t* request,
                   cros_cam_capture_cb_t callback,
                   void* context) override;

  // Stops capturing. Waits for the ongoing capture callback if there is any
  // underway.
  int StopCapture(int id) override;

 private:
  explicit FaceCliCameraService(base::StringPiece token_path_string)
      : token_path_string_(token_path_string) {}

  const std::string token_path_string_;
};

}  // namespace faced

#endif  // FACED_CAMERA_FACED_CAMERA_SERVICE_H_
