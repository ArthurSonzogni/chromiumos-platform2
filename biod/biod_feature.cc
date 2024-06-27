// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include "biod/biod_feature.h"

#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>

namespace biod {

const struct VariationsFeature kCrOSLateBootAllowFpmcuBetaFirmware = {
    .name = "CrOSLateBootAllowFpmcuBetaFirmware",
    .default_state = FEATURE_DISABLED_BY_DEFAULT,
};

void OnHandlerRegistrationFinish(bool success) {
  if (success) {
    LOG(INFO) << "Listening for feature changes";
  } else {
    LOG(WARNING) << "Failed to register signal handler";
  }
}

BiodFeature::BiodFeature(
    const scoped_refptr<dbus::Bus>& bus,
    feature::PlatformFeaturesInterface* feature_lib,
    std::unique_ptr<updater::FirmwareSelectorInterface> selector)
    : bus_(bus), feature_lib_(feature_lib), selector_(std::move(selector)) {
  feature_lib_->ListenForRefetchNeeded(
      base::BindRepeating(&BiodFeature::CheckFeatures, base::Unretained(this)),
      base::BindOnce(&OnHandlerRegistrationFinish));

  CheckFeatures();
}

void BiodFeature::CheckFeatures() {
  feature_lib_->IsEnabled(
      kCrOSLateBootAllowFpmcuBetaFirmware,
      base::BindOnce(&BiodFeature::AllowBetaFirmware, base::Unretained(this)));
}

void BiodFeature::AllowBetaFirmware(bool enable) {
  if (selector_->IsBetaFirmwareAllowed() == enable) {
    return;
  }

  LOG(INFO) << "Beta firmware switch status: " << std::boolalpha << enable;
  selector_->AllowBetaFirmware(enable);

  LOG(INFO) << "Ask powerd to reboot";
  dbus::ObjectProxy* powerd_proxy = bus_->GetObjectProxy(
      power_manager::kPowerManagerServiceName,
      dbus::ObjectPath(power_manager::kPowerManagerServicePath));

  dbus::MethodCall method_call(power_manager::kPowerManagerInterface,
                               power_manager::kRequestRestartMethod);
  dbus::MessageWriter writer(&method_call);
  writer.AppendInt32(
      power_manager::RequestRestartReason::REQUEST_RESTART_OTHER);
  writer.AppendString("User changed fingerprint beta firmware feature flag");

  base::expected<std::unique_ptr<dbus::Response>, dbus::Error> response =
      powerd_proxy->CallMethodAndBlock(&method_call,
                                       dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);

  if (!response.has_value()) {
    dbus::Error error = std::move(response.error());
    if (!error.IsValid()) {
      LOG(ERROR) << "Get invalid error when calling "
                 << power_manager::kRequestRestartMethod << " from "
                 << power_manager::kPowerManagerInterface << " interface.";
      return;
    }
    LOG(ERROR) << "Error while requesting reboot: " << error.name();
    LOG(ERROR) << "Error details: " << error.message();
  }
}

}  // namespace biod
