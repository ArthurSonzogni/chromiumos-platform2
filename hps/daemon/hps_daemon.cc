// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hps/daemon/hps_daemon.h"

#include <utility>

#include <base/location.h>
#include <brillo/errors/error.h>
#include <brillo/errors/error_codes.h>
#include <chromeos/dbus/service_constants.h>

namespace hps {

constexpr char kErrorPath[] = "org.chromium.Hps.GetFeatureResultError";

HpsDaemon::HpsDaemon(std::unique_ptr<HPS> hps)
    : brillo::DBusServiceDaemon(::hps::kHpsServiceName),
      org::chromium::HpsAdaptor(this),
      hps_(std::move(hps)) {}

HpsDaemon::HpsDaemon(scoped_refptr<dbus::Bus> bus, std::unique_ptr<HPS> hps)
    : brillo::DBusServiceDaemon(::hps::kHpsServiceName),
      org::chromium::HpsAdaptor(this),
      dbus_object_(new brillo::dbus_utils::DBusObject(
          nullptr, bus, dbus::ObjectPath(::hps::kHpsServicePath))),
      hps_(std::move(hps)) {}

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

bool HpsDaemon::EnableFeature(brillo::ErrorPtr* error, uint8_t feature) {
  if (!this->hps_->Enable(feature)) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kErrorPath, "hpsd: Unable to enable feature");

    return false;
  } else {
    return true;
  }
}

bool HpsDaemon::DisableFeature(brillo::ErrorPtr* error, uint8_t feature) {
  if (!this->hps_->Disable(feature)) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kErrorPath, "hpsd: Unable to disable feature");

    return false;
  } else {
    return true;
  }
}

bool HpsDaemon::GetFeatureResult(brillo::ErrorPtr* error,
                                 uint8_t feature,
                                 uint16_t* result) {
  int res = this->hps_->Result(feature);
  if (res < 0) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kErrorPath, "hpsd: Feature result not available");

    return false;
  } else {
    *result = res;
    return true;
  }
}

}  // namespace hps
