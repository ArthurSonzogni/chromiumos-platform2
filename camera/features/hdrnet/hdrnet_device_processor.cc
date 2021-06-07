/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/hdrnet/hdrnet_device_processor.h"

namespace cros {

// static
std::unique_ptr<HdrNetDeviceProcessor> HdrNetDeviceProcessor::GetInstance(
    const camera_metadata_t* static_info,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner) {
  return std::make_unique<HdrNetDeviceProcessor>();
}

bool HdrNetDeviceProcessor::Initialize() {
  return true;
}

void HdrNetDeviceProcessor::TearDown() {}

void HdrNetDeviceProcessor::ProcessResultMetadata(
    int frame_number, const camera_metadata_t* metadata) {}

bool HdrNetDeviceProcessor::Preprocess(const HdrNetConfig::Options& options,
                                       const SharedImage& input_external_yuv,
                                       const SharedImage& output_rgba) {
  return true;
}

bool HdrNetDeviceProcessor::Postprocess(const HdrNetConfig::Options& options,
                                        const SharedImage& input_rgba,
                                        const SharedImage& output_nv12) {
  return true;
}

}  // namespace cros
