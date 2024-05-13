// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "regmon/dbus/dbus_adaptor.h"

#include <utility>

#include <base/logging.h>
#include <dbus/bus.h>
#include <regmon/proto_bindings/regmon_service.pb.h>

#include "regmon/regmon/regmon_service.h"

namespace regmon {

DBusAdaptor::DBusAdaptor(scoped_refptr<dbus::Bus> bus,
                         std::unique_ptr<RegmonService> regmon)
    : org::chromium::RegmondAdaptor(this),
      dbus_object_(/*object_manager=*/nullptr,
                   bus,
                   org::chromium::RegmondAdaptor::GetObjectPath()),
      regmon_(std::move(regmon)) {}

void DBusAdaptor::RegisterAsync(
    brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(std::move(cb));
}

void DBusAdaptor::RecordPolicyViolation(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        RecordPolicyViolationResponse>> out_response,
    const RecordPolicyViolationRequest& in_request) {
  regmon_->RecordPolicyViolation(in_request, std::move(out_response));
}

}  // namespace regmon
