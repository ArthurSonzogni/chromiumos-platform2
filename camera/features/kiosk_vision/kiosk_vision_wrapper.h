// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_FEATURES_KIOSK_VISION_KIOSK_VISION_WRAPPER_H_
#define CAMERA_FEATURES_KIOSK_VISION_KIOSK_VISION_WRAPPER_H_

#include <cstdint>
#include <vector>

#include <base/files/file_path.h>
#include <base/functional/callback.h>
#include <cros-camera/camera_buffer_manager.h>
#include <cros-camera/libkioskvision/kiosk_audience_measurement_types.h>

namespace cros {

// Encapsulates usage of a kiosk vision pipeline for audience measurement.
class KioskVisionWrapper {
 public:
  enum class InitializeStatus {
    kOk = 0,
    kDlcError = 1,
    kPipelineError = 2,
    kInputBufferError = 3,
    kMaxValue = kInputBufferError,
  };

  using FrameCallback =
      base::RepeatingCallback<void(cros::kiosk_vision::Timestamp,
                                   const cros::kiosk_vision::Appearance*,
                                   uint32_t)>;

  using TrackCallback = base::RepeatingCallback<void(
      cros::kiosk_vision::TrackID id,
      const cros::kiosk_vision::Appearance* audience_data,
      uint32_t audience_size,
      cros::kiosk_vision::Timestamp start_time,
      cros::kiosk_vision::Timestamp end_time)>;

  using ErrorCallback = base::RepeatingCallback<void()>;

  KioskVisionWrapper(FrameCallback frame_cb,
                     TrackCallback track_cb,
                     ErrorCallback error_cb);
  virtual ~KioskVisionWrapper();
  KioskVisionWrapper(const KioskVisionWrapper&) = delete;
  KioskVisionWrapper& operator=(const KioskVisionWrapper&) = delete;

  // Loads dynamic library and initializes a vision pipeline.
  virtual InitializeStatus Initialize(const base::FilePath& dlc_root_path);

  // Returns detector input size in pixels.
  cros::kiosk_vision::ImageSize GetDetectorInputSize() const;

  // Inputs one frame into Kiosk Vision pipeline. Frame |buffer| should have
  // NV12 format, |timestamp| should increase from the previous call.
  virtual bool ProcessFrame(int64_t timestamp, buffer_handle_t buffer);

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

  // client callback for processed frame
  FrameCallback frame_processed_callback_;

  // client callback for completed track
  TrackCallback track_complete_callback_;

  // client callback for pipeline error
  ErrorCallback pipeline_error_callback_;

  // A handle for Kiosk Vision pipeline in the native library.
  void* pipeline_handle_ = nullptr;

  cros::kiosk_vision::ImageSize detector_input_size_;
  std::vector<uint8_t> detector_input_buffer_;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_KIOSK_VISION_KIOSK_VISION_WRAPPER_H_
