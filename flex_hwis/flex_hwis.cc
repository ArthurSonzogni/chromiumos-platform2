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

FlexHwisSender::FlexHwisSender(const base::FilePath& base_path,
                               policy::PolicyProvider& provider)
    : base_path_(base_path), check_(base_path, provider) {}

void FlexHwisSender::SetTelemetryInfoForTesting(mojom::TelemetryInfoPtr info) {
  mojo_.SetTelemetryInfoForTesting(std::move(info));
}

Result FlexHwisSender::CollectAndSend(MetricsLibraryInterface& metrics,
                                      Debug debug) {
  hwis_proto::Device data;

  // Exit if HWIS runs successfully within 24 hours.
  if (check_.HasRunRecently()) {
    return Result::HasRunRecently;
  }

  // Exit if the device does not have permission to send data to the server.
  PermissionInfo permission_info = check_.CheckPermission();
  SendPermissionMetric(permission_info, metrics);
  if (!permission_info.permission) {
    return Result::NotAuthorized;
  }

  mojo_.SetHwisInfo(&data);
  const UuidInfo info = check_.GetOrCreateUuid();
  if (info.uuid) {
    data.set_uuid(info.uuid.value());
  } else {
    LOG(ERROR) << "Unable to get nor create uuid.";
    return Result::Error;
  }

  // If the UUID was just generated this should be a POST request.
  // If the UUID already exists, then it should be a PUT request.
  if (info.already_exists) {
    // TODO(tinghaolin): Implement server interaction logic to call PUT api.
    LOG(INFO) << "Call PUT API to update the slot";
    SendServerMetric("Platform.FlexHwis.ServerPutResult", true, metrics);
  } else {
    // TODO(tinghaolin): Implement server interaction logic to call POST api.
    LOG(INFO) << "Call POST API to create a new slot";
    SendServerMetric("Platform.FlexHwis.ServerPostResult", true, metrics);
  }

  check_.RecordSendTime();

  if (debug == Debug::Print) {
    LOG(INFO) << data.DebugString();
  }
  return Result::Sent;
}

}  // namespace flex_hwis
