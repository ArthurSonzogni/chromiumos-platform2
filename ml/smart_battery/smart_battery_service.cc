// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/smart_battery/smart_battery_service.h"

#include <utility>

namespace ml {

SmartBatteryService::SmartBatteryService(
    std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object)
    : org::chromium::MachineLearningSmartBatteryAdaptor(this),
      dbus_object_(std::move(dbus_object)) {}

SmartBatteryService::~SmartBatteryService() = default;

void SmartBatteryService::RegisterAsync(
    const brillo::dbus_utils::AsyncEventSequencer::CompletionAction&
        completion_callback) {
  RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(completion_callback);
}

void SmartBatteryService::RequestSmartBatteryDecision(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<bool, std::vector<double>>>
        response,
    const std::string& serialized_example_proto) {
  // TODO(alanlxl): call the model to do the inference and extract the output.
  response->Return(true, std::vector<double>{4.0, 4.0, 4.0});
}

}  // namespace ml
