/*
 * Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_HDRNET_HDRNET_PROCESSOR_DEVICE_ADAPTER_H_
#define CAMERA_FEATURES_HDRNET_HDRNET_PROCESSOR_DEVICE_ADAPTER_H_

#include <memory>
#include <vector>

#include <system/camera_metadata.h>

#include <base/task/single_thread_task_runner.h>

#include "common/camera_hal3_helpers.h"
#include "common/metadata_logger.h"
#include "features/hdrnet/hdrnet_config.h"
#include "features/hdrnet/hdrnet_metrics.h"
#include "gpu/gpu_resources.h"
#include "gpu/shared_image.h"

namespace cros {

// Device specialization for the pre-processing and post-processing of the
// HDRnet pipeline.
//
// The default HdrNetProcessorDeviceAdapter implementation does nothing.
class HdrNetProcessorDeviceAdapter {
 public:
  struct OptionsOverrideData {
#if USE_IPU6 || USE_IPU6EP || USE_IPU6EPMTL
    // Initially, set an invalid sensor mode.
    int32_t sensor_mode = -1;
#endif
  };

  static std::unique_ptr<HdrNetProcessorDeviceAdapter> CreateInstance(
      const camera_metadata_t* static_info,
      scoped_refptr<base::SingleThreadTaskRunner> task_runner);

  // Returns the overridden HDRnet options if the options need update based on
  // |result|. Otherwise, returns std::nullopt. This also updates |data| that
  // can be used to specify which override key to use in GetOverriddenOptions().
  static std::optional<base::Value::Dict> MaybeOverrideOptions(
      const base::Value::Dict& json_values,
      const Camera3CaptureDescriptor& result,
      OptionsOverrideData& data);

  // Returns default or overridden HDRnet options based on the internal state
  // set by MaybeOverrideOptions(). "override" key may be left over in the
  // returned options. If so, its value should be ignored.
  static base::Value::Dict GetOverriddenOptions(
      const base::Value::Dict& json_values, const OptionsOverrideData& data);

  virtual ~HdrNetProcessorDeviceAdapter() = default;
  virtual bool Initialize(GpuResources* gpu_resources,
                          Size input_size,
                          const std::vector<Size>& output_sizes);
  virtual void TearDown();

  // Called on every frame to allow the adapter to set device specific
  // control metadata (e.g. vendor tags) for each capture request.
  virtual bool WriteRequestParameters(Camera3CaptureDescriptor* request,
                                      MetadataLogger* metadata_logger);

  // Called on every frame with the per-frame capture result metadata.
  virtual void ProcessResultMetadata(Camera3CaptureDescriptor* result,
                                     MetadataLogger* metadata_logger);

  // Runs the device-specific HDRnet processing pipeline.
  virtual bool Run(int frame_number,
                   const HdrNetConfig::Options& options,
                   const SharedImage& input,
                   const SharedImage& output,
                   HdrnetMetrics* hdrnet_metrics);
};

}  // namespace cros

#endif  // CAMERA_FEATURES_HDRNET_HDRNET_PROCESSOR_DEVICE_ADAPTER_H_
