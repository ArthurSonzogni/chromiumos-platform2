// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLEX_HWIS_FLEX_HWIS_MOJO_H_
#define FLEX_HWIS_FLEX_HWIS_MOJO_H_

#include "flex_hwis/hwis_data.pb.h"

#include <optional>
#include <string>

#include <diagnostics/mojom/public/cros_healthd_probe.mojom.h>

namespace flex_hwis {
namespace mojom = ::ash::cros_healthd::mojom;

class FlexHwisMojo {
 public:
  // The HWIS data should be retrieved from the telemetry API though mojo
  // service and formatted into protobuf format.
  void SetHwisInfo(hwis_proto::Device* data);
  // The UUID read from a specific path shall be written into the HWIS data.
  void SetHwisUuid(hwis_proto::Device* data, std::optional<std::string> uuid);
  // Extract the system data needed by the HWIS system from the telemetry API
  // and convert it into protobuf format.
  void SetSystemInfo(hwis_proto::Device* data);
  // Extract the cpu data needed by the HWIS system from the telemetry API
  // and convert it into protobuf format.
  void SetCpuInfo(hwis_proto::Device* data);
  // Extract the memory data needed by the HWIS system from the telemetry API
  // and convert it into protobuf format.
  void SetMemoryInfo(hwis_proto::Device* data);
  // Extract the bus data needed by the HWIS system from the telemetry API
  // and convert it into protobuf format.
  void SetBusInfo(hwis_proto::Device* data);
  // Extract the graphic data needed by the HWIS system from the telemetry API
  // and convert it into protobuf format.
  void SetGraphicInfo(hwis_proto::Device* data);
  // Extract the input data needed by the HWIS system from the telemetry API
  // and convert it into protobuf format.
  void SetInputInfo(hwis_proto::Device* data);
  // Extract the tpm data needed by the HWIS system from the telemetry API
  // and convert it into protobuf format.
  void SetTpmInfo(hwis_proto::Device* data);
  // This function is used by tests only to set the telemetry info.
  void SetTelemetryInfoForTesting(mojom::TelemetryInfoPtr info);

 private:
  template <class T>
  // Extract the device data needed by the HWIS system from the telemetry API
  // and convert it into protobuf format.
  void SetDeviceInfo(const mojom::BusDevicePtr& device, const T& controller);
  mojom::TelemetryInfoPtr telemetry_info_;
};

}  // namespace flex_hwis

#endif  // FLEX_HWIS_FLEX_HWIS_MOJO_H_
