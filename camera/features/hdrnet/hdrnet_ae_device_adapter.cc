/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/hdrnet/hdrnet_ae_device_adapter.h"

#if USE_IPU6 || USE_IPU6EP
#include "features/hdrnet/hdrnet_ae_device_adapter_ipu6.h"
#endif

namespace cros {

// static
std::unique_ptr<HdrNetAeDeviceAdapter> HdrNetAeDeviceAdapter::CreateInstance() {
#if USE_IPU6 || USE_IPU6EP
  return std::make_unique<HdrNetAeDeviceAdapterIpu6>();
#else
  return std::make_unique<HdrNetAeDeviceAdapter>();
#endif
}

bool HdrNetAeDeviceAdapter::ExtractAeStats(Camera3CaptureDescriptor* result,
                                           MetadataLogger* metadata_logger) {
  return true;
}

bool HdrNetAeDeviceAdapter::HasAeStats(int frame_number) {
  return true;
}

AeParameters HdrNetAeDeviceAdapter::ComputeAeParameters(
    int frame_number, const AeFrameInfo& frame_info, float max_hdr_ratio) {
  return AeParameters();
}

}  // namespace cros
