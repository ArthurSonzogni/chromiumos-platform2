/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_KIOSK_VISION_KIOSK_VISION_STREAM_MANIPULATOR_H_
#define CAMERA_FEATURES_KIOSK_VISION_KIOSK_VISION_STREAM_MANIPULATOR_H_

#include <string>
#include <mojo/public/cpp/bindings/remote.h>

#include "common/stream_manipulator.h"

namespace cros {

class KioskVisionStreamManipulator : public StreamManipulator {
 public:
  explicit KioskVisionStreamManipulator(RuntimeOptions* runtime_options);
  ~KioskVisionStreamManipulator() override;

  // StreamManipulator:
  bool Initialize(const camera_metadata_t* static_info,
                  Callbacks callbacks) override;
  bool ConfigureStreams(Camera3StreamConfiguration* stream_config) override;
  bool OnConfiguredStreams(Camera3StreamConfiguration* stream_config) override;
  bool ConstructDefaultRequestSettings(
      android::CameraMetadata* default_request_settings, int type) override;
  bool ProcessCaptureRequest(Camera3CaptureDescriptor* request) override;
  bool Flush() override;
  bool ProcessCaptureResult(Camera3CaptureDescriptor result) override;
  void Notify(camera3_notify_msg_t msg) override;

  const base::FilePath& GetDlcPathForTesting() const;

 private:
  base::FilePath dlc_path_;
  raw_ref<mojo::Remote<mojom::KioskVisionObserver>> observer_;
  StreamManipulator::Callbacks callbacks_;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_KIOSK_VISION_KIOSK_VISION_STREAM_MANIPULATOR_H_
