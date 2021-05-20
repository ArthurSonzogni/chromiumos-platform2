/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_HDRNET_HDRNET_AE_DEVICE_ADAPTER_H_
#define CAMERA_FEATURES_HDRNET_HDRNET_AE_DEVICE_ADAPTER_H_

#include <memory>

#include <camera/camera_metadata.h>

#include "common/metadata_logger.h"
#include "features/hdrnet/ae_info.h"

namespace cros {

// AeDeviceAdapter handles the device or platform specific AE stats extraction
// and translation, and the AE algorithm implementation (e.g. calls down to the
// device-specific Gcam AE implementation).
class HdrNetAeDeviceAdapter {
 public:
  static std::unique_ptr<HdrNetAeDeviceAdapter> CreateInstance();

  virtual ~HdrNetAeDeviceAdapter() = default;

  // Called by AeController to extract the device specific AE stats from the
  // given |result_metadata|.
  virtual bool ExtractAeStats(int frame_number,
                              const camera_metadata_t* result_metadata,
                              MetadataLogger* metadata_logger = nullptr);

  // Whether there's AE stats available for frame |frame_number|.
  virtual bool HasAeStats(int frame_number);

  // Compute the AE parameters from |frame_info| and the AE stats previously
  // extracted for frame |frame_number|.  |max_hdr_ratio| is passed a input
  // parameter to Gcam AE.
  virtual AeParameters ComputeAeParameters(int frame_number,
                                           const AeFrameInfo& frame_info,
                                           float max_hdr_ratio);
};

}  // namespace cros

#endif  // CAMERA_FEATURES_HDRNET_HDRNET_AE_DEVICE_ADAPTER_H_
