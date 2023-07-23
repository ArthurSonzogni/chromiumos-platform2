// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_hwis.h"

#include <utility>

#include <base/logging.h>

namespace flex_hwis {
namespace mojom = ::ash::cros_healthd::mojom;

FlexHwisSender::FlexHwisSender(const base::FilePath& base_path,
                               policy::PolicyProvider& provider)
    : base_path_(base_path), check_(base_path, provider) {}

void FlexHwisSender::SetTelemetryInfoForTesting(mojom::TelemetryInfoPtr info) {
  mojo_.SetTelemetryInfoForTesting(std::move(info));
}

Result FlexHwisSender::CollectAndSend(Debug debug) {
  hwis_proto::Device data;

  // Exit if HWIS runs successfully within 24 hours.
  if (check_.HasRunRecently()) {
    return Result::HasRunRecently;
  }

  // Exit if the device does not have permission to send data to the server.
  bool permission = check_.CheckPermission();
  if (!permission) {
    return Result::NotAuthorized;
  }

  mojo_.SetHwisInfo(&data);
  const UuidInfo info = check_.GetOrCreateUuid();
  mojo_.SetHwisUuid(&data, info.uuid);

  // If the UUID was just generated this should be a POST request.
  // If the UUID already exists, then it should be a PUT request.
  if (info.already_exists) {
    // TODO(tinghaolin): Implement server interaction logic to call PUT api.
    LOG(INFO) << "Call PUT API to update the slot";
  } else {
    // TODO(tinghaolin): Implement server interaction logic to call POST api.
    LOG(INFO) << "Call POST API to create a new slot";
  }

  check_.RecordSendTime();

  if (debug == Debug::Print) {
    LOG(INFO) << data.DebugString();
  }
  return Result::Sent;
}

}  // namespace flex_hwis
