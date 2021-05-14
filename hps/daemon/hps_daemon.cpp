// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hps/daemon/hps_daemon.h"

#include <chromeos/dbus/service_constants.h>

namespace hps {

HpsDaemon::HpsDaemon()
    : brillo::DBusServiceDaemon(::hps::kHpsServiceName),
      org::chromium::HpsAdaptor(this) {}

HpsDaemon::~HpsDaemon() = default;

void HpsDaemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  dbus_object_ = std::make_unique<brillo::dbus_utils::DBusObject>(
      /*object_manager=*/nullptr, bus_,
      org::chromium::HpsAdaptor::GetObjectPath());
  RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(
      sequencer->GetHandler(/*descriptive_message=*/"RegisterAsync failed.",
                            /*failure_is_fatal=*/true));
}

bool HpsDaemon::GetFeatureResult(brillo::ErrorPtr* error,
                                 uint32_t feature,
                                 uint16_t* result) {
  *result = 0;
  return true;
}

}  // namespace hps
