/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_HDRNET_HDRNET_AE_DEVICE_ADAPTER_IPU6_H_
#define CAMERA_FEATURES_HDRNET_HDRNET_AE_DEVICE_ADAPTER_IPU6_H_

#include "features/hdrnet/hdrnet_ae_device_adapter.h"

#include <memory>

#include <base/containers/flat_map.h>
#include <cros-camera/gcam_ae.h>

namespace cros {

// HdrNetAeDeviceAdapterIpu6 is the AE pipeline specilization for Intel IPU6/EP
// platforms.
class HdrNetAeDeviceAdapterIpu6 : public HdrNetAeDeviceAdapter {
 public:
  HdrNetAeDeviceAdapterIpu6();

  // HdrNetAeDeviceAdapter implementations.
  ~HdrNetAeDeviceAdapterIpu6() override = default;
  bool ExtractAeStats(int frame_number,
                      const camera_metadata_t* result_metadata,
                      MetadataLogger* metadata_logger = nullptr) override;
  bool HasAeStats(int frame_number) override;
  AeParameters ComputeAeParameters(int frame_number,
                                   const AeFrameInfo& frame_info,
                                   float max_hdr_ratio) override;

 private:
  base::Optional<AeStatsIntelIpu6*> GetAeStatsEntry(int frame_number,
                                                    bool create_entry = false);

  static constexpr size_t kAeStatsRingBufferSize = 6;
  struct AeStatsEntry {
    int frame_number = -1;
    AeStatsIntelIpu6 ae_stats;
  };
  std::array<AeStatsEntry, kAeStatsRingBufferSize> ae_stats_;

  std::unique_ptr<GcamAe> gcam_ae_;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_HDRNET_HDRNET_AE_DEVICE_ADAPTER_IPU6_H_
