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

// Verify that the user has granted permission or that all necessary policies
// are enabled to send hardware information.
bool CheckPermission(flex_hwis::FlexHwisCheck& check,
                     HttpSender& sender,
                     MetricsLibraryInterface& metrics) {
  PermissionInfo permission_info = check.CheckPermission();
  SendPermissionMetric(permission_info, metrics);
  if (permission_info.permission) {
    return true;
  }

  const std::optional<std::string> device_name = check.GetDeviceName();
  if (device_name) {
    // If the user does not consent to share hardware data, the HWIS service
    // must delete the device name file after confirming that the request to
    // delete the hardware data to the server is successfully.
    hwis_proto::DeleteDevice delete_device;
    delete_device.set_name(device_name.value());
    bool api_delete_success = false;
    if (sender.DeleteDevice(delete_device)) {
      LOG(INFO) << "Device has been deleted";
      check.DeleteDeviceName();
      api_delete_success = true;
    }
    const std::string kDeleteMetricName =
        "Platform.FlexHwis.ServerDeleteSuccess";
    SendServerMetric(kDeleteMetricName, api_delete_success, metrics);
  }
  return false;
}

bool RegisterNewDevice(flex_hwis::FlexHwisCheck& check,
                       HttpSender& sender,
                       hwis_proto::Device& hardware_info) {
  DeviceRegisterResult register_result =
      sender.RegisterNewDevice(hardware_info);
  if (!register_result.success) {
    return false;
  }
  // If the device is successfully registered, the server will return a
  // device name. The client must save this device name in the local file.
  LOG(INFO) << "Device has been registered";
  check.SetDeviceName(register_result.device_name);
  return true;
}

// Use the One Platform APIs to send hardware information to the server.
// Metrics will be used to track the status of the interaction.
bool SendHardwareInfo(flex_hwis::FlexHwisCheck& check,
                      HttpSender& sender,
                      hwis_proto::Device& hardware_info,
                      MetricsLibraryInterface& metrics) {
  bool api_call_success = false;
  const std::optional<std::string> device_name = check.GetDeviceName();

  // If device name already exists on the client side, then the client service
  // should update the device on the server.
  if (device_name) {
    hardware_info.set_name(device_name.value());
    DeviceUpdateResult update_result = sender.UpdateDevice(hardware_info);
    switch (update_result) {
      case DeviceUpdateResult::Success:
        LOG(INFO) << "Device has been updated";
        api_call_success = true;
        break;
      // TODO(b/309651923): Collect UMA metric for device not found error.
      case DeviceUpdateResult::DeviceNotFound:
        hardware_info.set_name("");
        // If the device name is on the client but not found on the server,
        // the client should register the device again.
        api_call_success = RegisterNewDevice(check, sender, hardware_info);
        break;
      case DeviceUpdateResult::Fail:
        break;
    }
  } else {
    api_call_success = RegisterNewDevice(check, sender, hardware_info);
  }

  const std::string metric_name =
      hardware_info.name().empty()
          ? /*register status metric=*/"Platform.FlexHwis.ServerPostSuccess"
          : /*update status metric=*/"Platform.FlexHwis.ServerPutSuccess";
  SendServerMetric(metric_name, api_call_success, metrics);
  return api_call_success;
}
}  // namespace

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
  // Exit if the device does not have permission to send data to the server.
  if (!CheckPermission(check_, sender_, metrics)) {
    return Result::NotAuthorized;
  }
  // Collect hardware information from cros_healthd via its mojo interface.
  hwis_proto::Device hardware_info;
  mojo_.SetHwisInfo(&hardware_info);
  // Exit if the hardware information is not successfully sent.
  if (!SendHardwareInfo(check_, sender_, hardware_info, metrics)) {
    return Result::Error;
  }

  check_.RecordSendTime();
  if (debug == Debug::Print) {
    LOG(INFO) << hardware_info.DebugString();
  }
  return Result::Sent;
}

}  // namespace flex_hwis
