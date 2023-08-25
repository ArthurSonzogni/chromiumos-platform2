// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cecservice/cecservice_dbus_adaptor.h"

#include <algorithm>
#include <iterator>
#include <utility>

#include <chromeos/dbus/service_constants.h>
#include <dbus/object_path.h>

#include "cecservice/udev.h"

namespace cecservice {

namespace {
void GetTvsPowerStatusCallback(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<std::vector<int32_t>>> response,
    const std::vector<TvPowerStatus>& results) {
  std::vector<int32_t> return_value;
  std::copy(results.begin(), results.end(), std::back_inserter(return_value));
  response->Return(return_value);
}

void SendStandByToAllDevicesCallback(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response) {
  VLOG(1) << "Standby request complete";
  response->Return();
}

void SendWakeUpToAllDevicesCallback(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response) {
  VLOG(1) << "Wakeup request complete";
  response->Return();
}
}  // namespace

CecServiceDBusAdaptor::CecServiceDBusAdaptor(scoped_refptr<dbus::Bus> bus)
    : org::chromium::CecServiceAdaptor(this),
      cec_device_factory_(&cec_fd_opener_),
      cec_(UdevFactoryImpl(), cec_device_factory_),
      dbus_object_(nullptr, bus, dbus::ObjectPath(kCecServicePath)) {}

CecServiceDBusAdaptor::~CecServiceDBusAdaptor() = default;

void CecServiceDBusAdaptor::RegisterAsync(
    brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(std::move(cb));
}

void CecServiceDBusAdaptor::SendStandByToAllDevices(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response) {
  cec_.SetStandBy(
      base::BindOnce(&SendStandByToAllDevicesCallback, std::move(response)));
}

void CecServiceDBusAdaptor::SendWakeUpToAllDevices(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response) {
  cec_.SetWakeUp(
      base::BindOnce(&SendWakeUpToAllDevicesCallback, std::move(response)));
}

void CecServiceDBusAdaptor::GetTvsPowerStatus(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<std::vector<int32_t>>>
        response) {
  cec_.GetTvsPowerStatus(
      base::BindOnce(&GetTvsPowerStatusCallback, std::move(response)));
}

}  // namespace cecservice
