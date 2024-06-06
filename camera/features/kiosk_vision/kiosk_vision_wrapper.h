// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_FEATURES_KIOSK_VISION_KIOSK_VISION_WRAPPER_H_
#define CAMERA_FEATURES_KIOSK_VISION_KIOSK_VISION_WRAPPER_H_

#include <cstdint>
#include <vector>

#include <base/files/file_path.h>
#include <cros-camera/camera_buffer_manager.h>
#include <cros-camera/libkioskvision/kiosk_audience_measurement_types.h>

namespace cros {

// Encapsulates usage of a kiosk vision pipeline for audience measurement.
class KioskVisionWrapper {
 public:
  KioskVisionWrapper() = default;
  ~KioskVisionWrapper();
  KioskVisionWrapper(const KioskVisionWrapper&) = delete;
  KioskVisionWrapper& operator=(const KioskVisionWrapper&) = delete;

  // Loads dynamic library and initializes a vision pipeline.
  bool Initialize(const base::FilePath& dlc_root_path);

  // Inputs one frame into Kiosk Vision pipeline. Frame |buffer| should have
  // NV12 format, |timestamp| should increase from the previous call.
  bool ProcessFrame(int64_t timestamp, buffer_handle_t buffer);

  // Callbacks for Kiosk Vision pipeline.
  void OnFrameProcessed(cros::kiosk_vision::Timestamp timestamp,
                        const cros::kiosk_vision::Appearance* audience_data,
                        uint32_t audience_size);

  void OnTrackCompleted(cros::kiosk_vision::TrackID id,
                        const cros::kiosk_vision::Appearance* audience_data,
                        uint32_t audience_size,
                        cros::kiosk_vision::Timestamp start_time,
                        cros::kiosk_vision::Timestamp end_time);

  void OnError();

 private:
  bool InitializeLibrary(const base::FilePath& dlc_root_path);
  bool InitializePipeline();
  bool InitializeInputBuffer();

  // A handle for Kiosk Vision pipeline in the native library.
  void* pipeline_handle_ = nullptr;

  int32_t detector_input_width_ = 0;
  int32_t detector_input_height_ = 0;
  std::vector<uint8_t> detector_input_buffer_;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_KIOSK_VISION_KIOSK_VISION_WRAPPER_H_