/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_KIOSK_VISION_KIOSK_VISION_STREAM_MANIPULATOR_H_
#define CAMERA_FEATURES_KIOSK_VISION_KIOSK_VISION_STREAM_MANIPULATOR_H_

#include <memory>
#include <vector>

#include <cros-camera/libkioskvision/kiosk_audience_measurement_types.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "common/stream_manipulator.h"

namespace cros {
class KioskVisionWrapper;

class KioskVisionStreamManipulator : public StreamManipulator {
 public:
  struct Options {
    // Adds current face/body detections to result metadata.
    bool enable_debug_visualization = false;
  };

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
  Camera3StreamBuffer* SelectInputBuffer(Camera3CaptureDescriptor& result);
  void SetDebugMetadata(Camera3CaptureDescriptor* result);
  int32_t DebugScaleWidth(int32_t original_width);
  int32_t DebugScaleHeight(int32_t original_height);

  void OnFrameProcessed(cros::kiosk_vision::Timestamp timestamp,
                        const cros::kiosk_vision::Appearance* audience_data,
                        uint32_t audience_size);

  void OnTrackCompleted(cros::kiosk_vision::TrackID id,
                        const cros::kiosk_vision::Appearance* appearances_data,
                        uint32_t appearances_size,
                        cros::kiosk_vision::Timestamp start_time,
                        cros::kiosk_vision::Timestamp end_time);
  void OnError();

  Options options_;
  StreamManipulator::Callbacks callbacks_;

  base::FilePath dlc_path_;

  // Sends vision results to the client (e.g. logic in ash-chrome).
  // Should be used in the IPC thread.
  raw_ref<mojo::Remote<mojom::KioskVisionObserver>> observer_;

  std::unique_ptr<KioskVisionWrapper> kiosk_vision_wrapper_;
  Size active_array_dimension_;
  Size detector_input_size_;

  // Protects members that can be accessed on different threads.
  base::Lock lock_;

  // Used for debug visualization via frame metadata.
  std::vector<cros::kiosk_vision::Appearance> latest_audience_result_
      GUARDED_BY(lock_);

  // Timestamp of the previous processed frame.
  int64_t processed_frame_timestamp_us_ GUARDED_BY(lock_) = 0;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_KIOSK_VISION_KIOSK_VISION_STREAM_MANIPULATOR_H_
