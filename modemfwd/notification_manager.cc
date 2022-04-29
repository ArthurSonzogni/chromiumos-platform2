// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/notification_manager.h"

#include <string>

#include <base/logging.h>

namespace modemfwd {

NotificationManager::NotificationManager(
    org::chromium::ModemfwdAdaptor* dbus_adaptor, Metrics* metrics)
    : dbus_adaptor_(dbus_adaptor), metrics_(metrics) {}

void NotificationManager::NotifyUpdateFirmwareCompletedSuccess(
    bool fw_installed) {
  dbus_adaptor_->SendUpdateFirmwareCompletedSignal(true, "");
  if (fw_installed)
    metrics_->SendFwInstallResultSuccess();
}

void NotificationManager::NotifyUpdateFirmwareCompletedFailure(
    const brillo::Error* error) {
  DCHECK(error);
  dbus_adaptor_->SendUpdateFirmwareCompletedSignal(false, error->GetCode());
  metrics_->SendFwInstallResultFailure(error);
}

}  // namespace modemfwd
