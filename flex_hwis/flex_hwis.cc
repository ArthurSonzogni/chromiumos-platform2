// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_hwis.h"

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
  const std::optional<std::string> uuid = check_.GetUuid();

  // Exit if the device does not have permission to send data to the server.
  if (!permission_info.permission) {
    if (uuid) {
      hardware_info.set_uuid(uuid.value());
      // If the user does not consent to share hardware data, the HWIS service
      // must delete the client's UUID after confirming that the request to
      // delete the hardware data to the server is successfully.
      if (sender_.DeleteDevice(hardware_info)) {
        check_.DeleteUuid();
      }
    }
    return Result::NotAuthorized;
  }

  mojo_.SetHwisInfo(&hardware_info);

  bool api_call_success = false;
  std::string metric_name;

  // If the device ID is not in the client side, client should do post request.
  // If the device ID already exists, then it should be a put request.
  if (uuid) {
    hardware_info.set_uuid(uuid.value());
    api_call_success = sender_.UpdateDevice(hardware_info);
    metric_name = kPutMetricName;
  } else {
    PostActionResponse response = sender_.RegisterNewDevice(hardware_info);
    api_call_success = response.success;
    metric_name = kPostMetricName;

    // If the POST API stores data successfully, the server will return a
    // device ID. The client must save this device ID in the local file.
    if (api_call_success) {
      hwis_proto::Device response_proto;
      response_proto.ParsePartialFromString(response.serialized_uuid);
      check_.SetUuid(response_proto.uuid());
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
