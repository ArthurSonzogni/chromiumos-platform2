/*
 * Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_GCAM_AE_GCAM_AE_DEVICE_ADAPTER_IPU6_H_
#define CAMERA_FEATURES_GCAM_AE_GCAM_AE_DEVICE_ADAPTER_IPU6_H_

#include "features/gcam_ae/gcam_ae_device_adapter.h"

#include <memory>
#include <optional>

#include <base/containers/flat_map.h>
#include <cros-camera/gcam_ae.h>

#include "features/gcam_ae/ae_info.h"

namespace cros {

// GcamAeDeviceAdapterIpu6 is the AE pipeline specilization for Intel IPU6/EP
// platforms.
class GcamAeDeviceAdapterIpu6 : public GcamAeDeviceAdapter {
 public:
  GcamAeDeviceAdapterIpu6();

  // GcamAeDeviceAdapter implementations.
  ~GcamAeDeviceAdapterIpu6() override = default;
  bool WriteRequestParameters(Camera3CaptureDescriptor* request,
                              const AeFrameInfo& frame_info) override;
  bool SetExposureTargetVendorTag(Camera3CaptureDescriptor* request,
                                  float exposure_target) override;
  bool ExtractAeStats(Camera3CaptureDescriptor* result,
                      MetadataLogger* metadata_logger = nullptr) override;
  std::optional<Gain> GetGain(const Camera3CaptureDescriptor& result) override;
  std::optional<GainRange> GetGainRange(
      const Camera3CaptureDescriptor& result) override;
  std::optional<Range<int>> GetSensitivityRange(
      const Camera3CaptureDescriptor& result) override;
  bool HasAeStats(int frame_number) override;
  AeParameters ComputeAeParameters(int frame_number,
                                   const AeFrameInfo& frame_info,
                                   const Range<float>& device_tet_range,
                                   float max_hdr_ratio) override;
  std::optional<base::Value::Dict> MaybeOverrideOptions(
      const base::Value::Dict& json_values,
      const Camera3CaptureDescriptor& result) override;
  base::Value::Dict GetOverriddenOptions(
      const base::Value::Dict& json_values) override;

 private:
  static constexpr int32_t kInvalidSensorMode = -1;
  static constexpr size_t kAeStatsRingBufferSize = 6;
  struct AeStatsEntry {
    int frame_number = -1;
    AeStatsIntelIpu6 ae_stats;
    std::optional<Range<float>> tet_range;
  };

  std::optional<AeStatsEntry*> GetAeStatsEntry(int frame_number,
                                               bool create_entry = false);

  std::array<AeStatsEntry, kAeStatsRingBufferSize> ae_stats_;

  std::unique_ptr<GcamAe> gcam_ae_;

  // Initially, set an invalid sensor mode.
  int32_t sensor_mode_ = kInvalidSensorMode;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_GCAM_AE_GCAM_AE_DEVICE_ADAPTER_IPU6_H_
