/*
 * Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_GCAM_AE_GCAM_AE_DEVICE_ADAPTER_H_
#define CAMERA_FEATURES_GCAM_AE_GCAM_AE_DEVICE_ADAPTER_H_

#include <memory>

#include <camera/camera_metadata.h>

#include "common/camera_hal3_helpers.h"
#include "common/metadata_logger.h"
#include "features/gcam_ae/ae_info.h"

namespace cros {

// AeDeviceAdapter handles the device or platform specific AE stats extraction
// and translation, and the AE algorithm implementation (e.g. calls down to the
// device-specific Gcam AE implementation).
class GcamAeDeviceAdapter {
 public:
  static std::unique_ptr<GcamAeDeviceAdapter> CreateInstance();

  virtual ~GcamAeDeviceAdapter() = default;

  // Called by GcamAeController to allow the adapter to set device specific
  // control metadata (e.g. vendor tags) for each capture request.
  virtual bool WriteRequestParameters(Camera3CaptureDescriptor* request,
                                      const AeFrameInfo& frame_info);

  // Called by GcamAeController to set the exposure target through vendor tag.
  // Returns true if the camera HAL accepts the exposure target vendor tag and
  // |tet| is successfully configured. Returns false if the camera HAL does not
  // support setting exposure target through vendor tag, or the tag is not
  // successfully configured.
  //
  // |exposure_target| has the same format as the TET computed by Gcam AE:
  //   exposure_time (ms) * analog_gain * digital_gain
  virtual bool SetExposureTargetVendorTag(Camera3CaptureDescriptor* request,
                                          float exposure_target);

  // Called by GcamAeController to extract the device specific AE stats from
  // |result|.
  virtual bool ExtractAeStats(Camera3CaptureDescriptor* result,
                              MetadataLogger* metadata_logger = nullptr);

  // Called by GcamAeController to get the analog and digital gains if available
  // from |result|'s metadata.
  virtual std::optional<Gain> GetGain(const Camera3CaptureDescriptor& result);

  // Called by GcamAeController to get the analog and digital gain ranges if
  // available from |result|'s metadata.
  virtual std::optional<GainRange> GetGainRange(
      const Camera3CaptureDescriptor& result);

  // Called by GcamAeController to get the sensitivity range if available from
  // |result|'s metadata.
  virtual std::optional<Range<int>> GetSensitivityRange(
      const Camera3CaptureDescriptor& result);

  // Whether there's AE stats available for frame |frame_number|.
  virtual bool HasAeStats(int frame_number);

  // Compute the AE parameters from |frame_info| and the AE stats previously
  // extracted for frame |frame_number|.  |device_tet_range| and |max_hdr_ratio|
  // are passed as input parameter to Gcam AE.
  virtual AeParameters ComputeAeParameters(int frame_number,
                                           const AeFrameInfo& frame_info,
                                           const Range<float>& device_tet_range,
                                           float max_hdr_ratio);

  // Returns the overridden Gcam AE options if the options need update based on
  // |result|. Otherwise, returns std::nullopt. This also updates the internal
  // state that specifies which override key to use in GetOverriddenOptions().
  virtual std::optional<base::Value::Dict> MaybeOverrideOptions(
      const base::Value::Dict& json_values,
      const Camera3CaptureDescriptor& result);

  // Returns default or overridden Gcam AE options based on the internal state
  // set by MaybeOverrideOptions(). "override" key may be left over in the
  // returned options. If so, its value should be ignored.
  virtual base::Value::Dict GetOverriddenOptions(
      const base::Value::Dict& json_values);
};

}  // namespace cros

#endif  // CAMERA_FEATURES_GCAM_AE_GCAM_AE_DEVICE_ADAPTER_H_
