/*
 * Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/hdrnet/hdrnet_processor_device_adapter.h"

#if USE_IPU6 || USE_IPU6EP || USE_IPU6EPMTL
#include "features/hdrnet/hdrnet_processor_device_adapter_ipu6.h"
#endif

namespace cros {

// static
std::unique_ptr<HdrNetProcessorDeviceAdapter>
HdrNetProcessorDeviceAdapter::CreateInstance(
    const camera_metadata_t* static_info,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner) {
#if USE_IPU6 || USE_IPU6EP || USE_IPU6EPMTL
  return std::make_unique<HdrNetProcessorDeviceAdapterIpu6>(static_info,
                                                            task_runner);
#else
  return std::make_unique<HdrNetProcessorDeviceAdapter>();
#endif
}

#if !(USE_IPU6 || USE_IPU6EP || USE_IPU6EPMTL)

// static
std::optional<base::Value::Dict>
HdrNetProcessorDeviceAdapter::MaybeOverrideOptions(
    const base::Value::Dict& json_values,
    const Camera3CaptureDescriptor& result,
    HdrNetProcessorDeviceAdapter::OptionsOverrideData& data) {
  return std::nullopt;
}

// static
base::Value::Dict HdrNetProcessorDeviceAdapter::GetOverriddenOptions(
    const base::Value::Dict& json_values,
    const HdrNetProcessorDeviceAdapter::OptionsOverrideData& data) {
  return json_values.Clone();
}

#endif

bool HdrNetProcessorDeviceAdapter::Initialize(
    GpuResources* gpu_resources,
    Size input_size,
    const std::vector<Size>& output_sizes) {
  return true;
}

void HdrNetProcessorDeviceAdapter::TearDown() {}

bool HdrNetProcessorDeviceAdapter::WriteRequestParameters(
    Camera3CaptureDescriptor* request, MetadataLogger* metadata_logger) {
  return true;
}

void HdrNetProcessorDeviceAdapter::ProcessResultMetadata(
    Camera3CaptureDescriptor* result, MetadataLogger* metadata_logger) {}

bool HdrNetProcessorDeviceAdapter::Run(int frame_number,
                                       const HdrNetConfig::Options& options,
                                       const SharedImage& input,
                                       const SharedImage& output,
                                       HdrnetMetrics* hdrnet_metrics) {
  return true;
}

}  // namespace cros
