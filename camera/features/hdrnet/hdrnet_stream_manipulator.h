/*
 * Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_HDRNET_HDRNET_STREAM_MANIPULATOR_H_
#define CAMERA_FEATURES_HDRNET_HDRNET_STREAM_MANIPULATOR_H_

#include "common/stream_manipulator.h"

#include <camera/camera_metadata.h>

#include <map>
#include <memory>
#include <string>

#include <base/files/scoped_file.h>

#include "common/camera_hal3_helpers.h"
#include "common/reloadable_config_file.h"
#include "common/still_capture_processor.h"
#include "common/stream_manipulator_helper.h"
#include "cros-camera/spatiotemporal_denoiser.h"
#include "features/hdrnet/hdrnet_config.h"
#include "features/hdrnet/hdrnet_metrics.h"
#include "features/hdrnet/hdrnet_processor.h"
#include "features/hdrnet/hdrnet_processor_device_adapter.h"
#include "gpu/shared_image.h"

namespace cros {

class HdrNetStreamManipulator : public StreamManipulator {
 public:
  HdrNetStreamManipulator(
      RuntimeOptions* runtime_options,
      GpuResources* gpu_resources,
      base::FilePath config_file_path,
      std::string camera_module_name,
      std::unique_ptr<StillCaptureProcessor> still_capture_processor,
      HdrNetProcessor::Factory hdrnet_processor_factory = base::NullCallback(),
      HdrNetConfig::Options* options = nullptr);

  ~HdrNetStreamManipulator() override;

  // Implementations of StreamManipulator.  These methods are trampolines and
  // all the actual tasks are carried out and sequenced on the sequenced task
  // runner of |hdrnet_gpu_resources_| with the internal implementations below.
  bool Initialize(const camera_metadata_t* static_info,
                  StreamManipulator::Callbacks callbacks) override;
  bool ConfigureStreams(Camera3StreamConfiguration* stream_config) override;
  bool OnConfiguredStreams(Camera3StreamConfiguration* stream_config) override;
  bool ConstructDefaultRequestSettings(
      android::CameraMetadata* default_request_settings, int type) override;
  bool ProcessCaptureRequest(Camera3CaptureDescriptor* request) override;
  bool ProcessCaptureResult(Camera3CaptureDescriptor result) override;
  void Notify(camera3_notify_msg_t msg) override;
  bool Flush() override;

 private:
  struct HdrNetStreamContext {
    // The HDRnet processor instance for this stream.
    HdrNetProcessor* processor = nullptr;

    // Spatiotemporal denoiser resources.
    SpatiotemporalDenoiser* denoiser = nullptr;
    SharedImage denoiser_intermediate;
    bool should_reset_temporal_buffer = true;
  };

  void InitializeGpuResourcesOnRootGpuThread();

  // Internal implementations of StreamManipulator.  All these methods are
  // sequenced on the sequenced task runner of |hdrnet_gpu_resources_|.
  bool InitializeOnGpuThread(const camera_metadata_t* static_info,
                             StreamManipulator::Callbacks callbacks);
  bool ConfigureStreamsOnGpuThread(Camera3StreamConfiguration* stream_config);
  bool OnConfiguredStreamsOnGpuThread(
      Camera3StreamConfiguration* stream_config);
  bool ProcessCaptureRequestOnGpuThread(Camera3CaptureDescriptor* request);
  bool ProcessCaptureResultOnGpuThread(Camera3CaptureDescriptor result);
  bool NotifyOnGpuThread(camera3_notify_msg_t* msg);
  bool FlushOnGpuThread();

  HdrNetConfig::Options PrepareProcessorConfig(
      uint32_t frame_number,
      const FeatureMetadata& feature_metadata,
      bool skip_hdrnet_processing) const;

  bool SetUpPipelineOnGpuThread();

  void ResetStateOnGpuThread();

  void OnOptionsUpdated(const base::Value::Dict& json_values);
  void SetOptions(const base::Value::Dict& json_values);
  void UploadMetrics();
  void OnProcessTask(ScopedProcessTask task);

  RuntimeOptions* runtime_options_ = nullptr;
  GpuResources* root_gpu_resources_ = nullptr;
  GpuResources* hdrnet_gpu_resources_ = nullptr;
  HdrNetProcessor::Factory hdrnet_processor_factory_;
  ReloadableConfigFile config_;
  HdrNetConfig::Options options_;
  android::CameraMetadata static_info_;

  std::string camera_module_name_;
  std::unique_ptr<StillCaptureProcessor> still_capture_processor_;
  std::unique_ptr<StreamManipulatorHelper> helper_;

  std::map<const camera3_stream_t*, std::unique_ptr<HdrNetStreamContext>>
      hdrnet_stream_context_;

  HdrnetMetrics hdrnet_metrics_;
  std::unique_ptr<CameraMetrics> camera_metrics_;

  // Metadata logger for tests and debugging.
  MetadataLogger metadata_logger_;

  // Stores the full content of the HDRnet config file including override values
  // if specified.
  base::Value::Dict json_values_;

  // Stores data to determine which override key to use.
  HdrNetProcessorDeviceAdapter::OptionsOverrideData override_data_;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_HDRNET_HDRNET_STREAM_MANIPULATOR_H_
