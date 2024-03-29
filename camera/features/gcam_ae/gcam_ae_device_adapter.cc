/*
 * Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/gcam_ae/gcam_ae_device_adapter.h"

#include "features/gcam_ae/ae_info.h"
#if USE_IPU6 || USE_IPU6EP || USE_IPU6EPMTL || USE_IPU6EPADLN
#include "features/gcam_ae/gcam_ae_device_adapter_ipu6.h"
#endif

namespace cros {

// static
std::unique_ptr<GcamAeDeviceAdapter> GcamAeDeviceAdapter::CreateInstance() {
#if USE_IPU6 || USE_IPU6EP || USE_IPU6EPMTL || USE_IPU6EPADLN
  return std::make_unique<GcamAeDeviceAdapterIpu6>();
#else
  return std::make_unique<GcamAeDeviceAdapter>();
#endif
}

bool GcamAeDeviceAdapter::WriteRequestParameters(
    Camera3CaptureDescriptor* request, const AeFrameInfo& frame_info) {
  return true;
}

bool GcamAeDeviceAdapter::SetExposureTargetVendorTag(
    Camera3CaptureDescriptor* request, float exposure_target) {
  // Returns false by default indicating the exposure target vendor tag is not
  // supported.
  return false;
}

bool GcamAeDeviceAdapter::ExtractAeStats(Camera3CaptureDescriptor* result,
                                         MetadataLogger* metadata_logger) {
  return true;
}

std::optional<Gain> GcamAeDeviceAdapter::GetGain(
    const Camera3CaptureDescriptor& result) {
  return std::nullopt;
}

std::optional<GainRange> GcamAeDeviceAdapter::GetGainRange(
    const Camera3CaptureDescriptor& result) {
  return std::nullopt;
}

std::optional<Range<int>> GcamAeDeviceAdapter::GetSensitivityRange(
    const Camera3CaptureDescriptor& result) {
  return std::nullopt;
}

bool GcamAeDeviceAdapter::HasAeStats(int frame_number) {
  return true;
}

AeParameters GcamAeDeviceAdapter::ComputeAeParameters(
    int frame_number,
    const AeFrameInfo& frame_info,
    const Range<float>& device_tet_range,
    float max_hdr_ratio) {
  return AeParameters();
}

std::optional<base::Value::Dict> GcamAeDeviceAdapter::MaybeOverrideOptions(
    const base::Value::Dict& json_values,
    const Camera3CaptureDescriptor& result) {
  return std::nullopt;
}

base::Value::Dict GcamAeDeviceAdapter::GetOverriddenOptions(
    const base::Value::Dict& json_values) {
  return json_values.Clone();
}

}  // namespace cros
