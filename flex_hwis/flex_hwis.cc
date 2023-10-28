// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_hwis.h"
#include "flex_hwis/hwis_data.pb.h"

#include <string>
#include <utility>

#include <base/logging.h>

namespace flex_hwis {
namespace mojom = ::ash::cros_healthd::mojom;
namespace {
// Track the result of management policies.
void SendPermissionMetric(PermissionInfo info,
                          MetricsLibraryInterface& metrics) {
  PermissionResult result;
  if (!info.loaded) {
    result = PermissionResult::kError;
  } else if (info.managed) {
    if (info.permission) {
      result = PermissionResult::kPolicySuccess;
    } else {
      result = PermissionResult::kPolicyDenial;
    }
  } else {
    if (info.permission) {
      result = PermissionResult::kOptInSuccess;
    } else {
      result = PermissionResult::kOptInDenial;
    }
  }

  if (!metrics.SendEnumToUMA("Platform.FlexHwis.PermissionCheckResult",
                             static_cast<int>(result),
                             static_cast<int>(PermissionResult::kMax))) {
    LOG(INFO) << "Failed to send hwis permission metric";
  }
}

// Track the result of client-server interactions.
void SendServerMetric(std::string metric_name,
                      bool success,
                      MetricsLibraryInterface& metrics) {
  if (!metrics.SendBoolToUMA(metric_name, success)) {
    LOG(INFO) << "Failed to send hwis server metric";
  }
}
}  // namespace
constexpr char kPutMetricName[] = "Platform.FlexHwis.ServerPutSuccess";
constexpr char kPostMetricName[] = "Platform.FlexHwis.ServerPostSuccess";
constexpr char kDeleteMetricName[] = "Platform.FlexHwis.ServerDeleteSuccess";

FlexHwisSender::FlexHwisSender(const base::FilePath& base_path,
                               policy::PolicyProvider& provider,
                               HttpSender& sender)
    : base_path_(base_path), check_(base_path, provider), sender_(sender) {}

void FlexHwisSender::SetTelemetryInfoForTesting(mojom::TelemetryInfoPtr info) {
  mojo_.SetTelemetryInfoForTesting(std::move(info));
}

Result FlexHwisSender::CollectAndSend(MetricsLibraryInterface& metrics,
                                      Debug debug) {
  // Exit if HWIS runs successfully within 24 hours.
  if (check_.HasRunRecently()) {
    return Result::HasRunRecently;
  }

  PermissionInfo permission_info = check_.CheckPermission();
  SendPermissionMetric(permission_info, metrics);
  hwis_proto::Device hardware_info;
  const std::optional<std::string> device_name = check_.GetDeviceName();

  // Exit if the device does not have permission to send data to the server.
  if (!permission_info.permission) {
    if (device_name) {
      // If the user does not consent to share hardware data, the HWIS service
      // must delete the device name file after confirming that the request to
      // delete the hardware data to the server is successfully.
      hwis_proto::DeleteDevice delete_device;
      delete_device.set_name(device_name.value());
      bool api_delete_success = false;
      if (sender_.DeleteDevice(delete_device)) {
        check_.DeleteDeviceName();
        api_delete_success = true;
      }
      SendServerMetric(kDeleteMetricName, api_delete_success, metrics);
    }
    return Result::NotAuthorized;
  }

  mojo_.SetHwisInfo(&hardware_info);

  bool api_call_success = false;
  std::string metric_name;

  // If device name is not in client side, client should register a new device.
  // If device name already exists, then the client should update the device.
  if (device_name) {
    hardware_info.set_name(device_name.value());
    api_call_success = sender_.UpdateDevice(hardware_info);
    metric_name = kPutMetricName;
  } else {
    DeviceRegisterResult register_result =
        sender_.RegisterNewDevice(hardware_info);
    api_call_success = register_result.success;
    metric_name = kPostMetricName;

    // If the device is successfully registered, the server will return a
    // device name. The client must save this device name in the local file.
    if (api_call_success) {
      check_.SetDeviceName(register_result.device_name);
    }
  }

  SendServerMetric(metric_name, api_call_success, metrics);
  if (!api_call_success) {
    return Result::Error;
  }

  check_.RecordSendTime();

  if (debug == Debug::Print) {
    LOG(INFO) << hardware_info.DebugString();
  }
  return Result::Sent;
}

}  // namespace flex_hwis
