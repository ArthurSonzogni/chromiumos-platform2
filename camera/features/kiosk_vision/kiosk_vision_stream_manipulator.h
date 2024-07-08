/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_KIOSK_VISION_KIOSK_VISION_STREAM_MANIPULATOR_H_
#define CAMERA_FEATURES_KIOSK_VISION_KIOSK_VISION_STREAM_MANIPULATOR_H_

#include <memory>
#include <vector>

#include <base/memory/scoped_refptr.h>
#include <base/task/single_thread_task_runner.h>
#include <cros-camera/libkioskvision/kiosk_audience_measurement_types.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "common/reloadable_config_file.h"
#include "common/stream_manipulator.h"

namespace cros {
class KioskVisionWrapper;

class KioskVisionStreamManipulator : public StreamManipulator {
 public:
  struct Options {
    // Timeout since the previous frame before inputting the next frame into the
    // tracking pipeline. Measured in milliseconds.
    // Used to limit the processing frame rate (FPS):
    // E.g. FPS = 1000 / 'frame_timeout_ms', 166ms timeout corresponds to 6 FPS.
    int64_t frame_timeout_ms = 166;

    // Adds current face/body detections to the capture result metadata.
    bool enable_debug_visualization = false;
  };

  enum class Status {
    kNotInitialized = 0,
    kInitialized = 1,
    kUnknownError = 2,
    kDlcError = 3,
    kModelError = 4,
    kMaxValue = kModelError,
  };

  KioskVisionStreamManipulator(
      RuntimeOptions* runtime_options,
      const scoped_refptr<base::SingleThreadTaskRunner>&
          ipc_thread_task_runner);
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

  Status GetStatusForTesting() const;

 private:
  Camera3StreamBuffer* SelectInputBuffer(Camera3CaptureDescriptor& result);
  void SetDebugMetadata(Camera3CaptureDescriptor* result);
  int32_t DebugScaleWidth(int32_t original_width);
  int32_t DebugScaleHeight(int32_t original_height);

  void OnOptionsUpdated(const base::Value::Dict& json_values);

  void OnFrameProcessed(cros::kiosk_vision::Timestamp timestamp,
                        const cros::kiosk_vision::Appearance* audience_data,
                        uint32_t audience_size);

  void OnTrackCompleted(cros::kiosk_vision::TrackID id,
                        const cros::kiosk_vision::Appearance* appearances_data,
                        uint32_t appearances_size,
                        cros::kiosk_vision::Timestamp start_time,
                        cros::kiosk_vision::Timestamp end_time);
  void OnError();

  // Updates `status_` and calls `ReportError` depending on `status`.
  void UpdateStatus(Status status);

  // Reports `error_status` to `observer_`. Should only be called from
  // `UpdateStatus`.
  void ReportError(Status error_status);

  Options options_;
  ReloadableConfigFile config_;
  // Should only be updated via `UpdateStatus`.
  Status status_ = Status::kNotInitialized;
  StreamManipulator::Callbacks callbacks_;

  base::FilePath dlc_path_;

  // Sends vision results to the client (e.g. logic in ash-chrome).
  // Should be used in the IPC thread via `ipc_thread_task_runner_`.
  raw_ref<mojo::Remote<mojom::KioskVisionObserver>> observer_;
  // IPC thread runner which can be overridden in tests.
  scoped_refptr<base::SingleThreadTaskRunner> ipc_thread_task_runner_;

  // Protects members that can be accessed on different threads.
  base::Lock lock_;

  // Used for debug visualization via frame metadata.
  std::vector<cros::kiosk_vision::Appearance> latest_audience_result_
      GUARDED_BY(lock_);

  // Timestamp of the previous processed frame.
  int64_t processed_frame_timestamp_us_ GUARDED_BY(lock_) = 0;

  // Used to normalize (resize) detector results to debugging view.
  Size active_array_dimension_;
  Size detector_input_size_;

  // Kiosk Vision pipeline instance.
  // Declared last to ensure the correct destruction order, as it can trigger
  // callbacks during destruction.
  std::unique_ptr<KioskVisionWrapper> kiosk_vision_wrapper_;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_KIOSK_VISION_KIOSK_VISION_STREAM_MANIPULATOR_H_
