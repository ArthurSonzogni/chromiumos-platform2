/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/hdrnet/hdrnet_processor_device_adapter.h"

#if USE_IPU6 || USE_IPU6EP
#include "features/hdrnet/hdrnet_processor_device_adapter_ipu6.h"
#endif

namespace cros {

// static
std::unique_ptr<HdrNetProcessorDeviceAdapter>
HdrNetProcessorDeviceAdapter::CreateInstance(
    const camera_metadata_t* static_info,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner) {
#if USE_IPU6 || USE_IPU6EP
  return std::make_unique<HdrNetProcessorDeviceAdapterIpu6>(static_info,
                                                            task_runner);
#else
  return std::make_unique<HdrNetProcessorDeviceAdapter>();
#endif
}

bool HdrNetProcessorDeviceAdapter::Initialize() {
  return true;
}

void HdrNetProcessorDeviceAdapter::TearDown() {}

bool HdrNetProcessorDeviceAdapter::WriteRequestParameters(
    Camera3CaptureDescriptor* request) {
  return true;
}

void HdrNetProcessorDeviceAdapter::ProcessResultMetadata(
    Camera3CaptureDescriptor* result) {}

bool HdrNetProcessorDeviceAdapter::Preprocess(
    const HdrNetConfig::Options& options,
    const SharedImage& input_external_yuv,
    const SharedImage& output_rgba) {
  return true;
}

bool HdrNetProcessorDeviceAdapter::Postprocess(
    const HdrNetConfig::Options& options,
    const SharedImage& input_rgba,
    const SharedImage& output_nv12) {
  return true;
}

}  // namespace cros
