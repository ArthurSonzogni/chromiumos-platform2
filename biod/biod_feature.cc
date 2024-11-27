// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/biod_feature.h"

#include <memory>
#include <utility>

#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>

namespace biod {

BiodFeature::BiodFeature(
    const scoped_refptr<dbus::Bus>& bus,
    const SessionStateManagerInterface* session_state_manager,
    feature::PlatformFeaturesInterface* feature_lib,
    std::unique_ptr<updater::FirmwareSelectorInterface> selector)
    : bus_(bus),
      session_state_manager_(session_state_manager),
      feature_lib_(feature_lib),
      selector_(std::move(selector)) {
  // Firmware beta tests are finished, but some users may still have beta
  // firmware enabled.
  AllowBetaFirmware(false);
}

void BiodFeature::AllowBetaFirmware(bool enable) {
  if (selector_->IsBetaFirmwareAllowed() == enable) {
    return;
  }

  selector_->AllowBetaFirmware(enable);

  // It's okay to switch to production firmware when user reboots chromebook,
  // so there is no need to force reboot here.
}

}  // namespace biod
