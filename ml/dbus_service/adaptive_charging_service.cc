// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/dbus_service/adaptive_charging_service.h"

#include <utility>

namespace ml {

AdaptiveChargingService::AdaptiveChargingService(
    std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object)
    : org::chromium::MachineLearning::AdaptiveChargingAdaptor(this),
      dbus_object_(std::move(dbus_object)) {}

AdaptiveChargingService::~AdaptiveChargingService() = default;

void AdaptiveChargingService::RegisterAsync(
    const brillo::dbus_utils::AsyncEventSequencer::CompletionAction&
        completion_callback) {
  RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(completion_callback);
}

void AdaptiveChargingService::RequestAdaptiveChargingDecision(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<bool, std::vector<double>>>
        response,
    const std::string& serialized_example_proto) {
  // TODO(alanlxl): call the model to do the inference and extract the output.
  response->Return(true, std::vector<double>{3.0, 4.0, 5.0});
}

}  // namespace ml
