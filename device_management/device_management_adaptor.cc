// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_management/device_management_adaptor.h"
#include "device_management/device_management_service.h"

#include "device_management/proto_bindings/device_management_interface.pb.h"
// Requires `device_management/device_management_interface.pb.h`
#include "device_management/dbus_adaptors/org.chromium.DeviceManagement.h"

#include <string>
#include <utility>

#include <base/logging.h>
#include <brillo/dbus/dbus_method_response.h>

namespace device_management {
DeviceManagementServiceAdaptor::DeviceManagementServiceAdaptor(
    scoped_refptr<dbus::Bus> bus, DeviceManagementService* service)
    : org::chromium::DeviceManagementAdaptor(this),
      service_(service),
      dbus_object_(nullptr,
                   bus,
                   org::chromium::DeviceManagementAdaptor::GetObjectPath()) {}

void DeviceManagementServiceAdaptor::RegisterAsync(
    brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(std::move(cb));
}

void DeviceManagementServiceAdaptor::GetFirmwareManagementParameters(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        device_management::GetFirmwareManagementParametersReply>> response,
    const device_management::GetFirmwareManagementParametersRequest& request) {
  VLOG(1) << __func__;
  device_management::GetFirmwareManagementParametersReply reply;
  device_management::FirmwareManagementParameters fwmp;

  CHECK(service_);
  auto status = service_->GetFirmwareManagementParameters(&fwmp);
  // Note, if there's no error, then |status| is set to
  // DEVICE_MANAGEMENT_ERROR_NOT_SET to indicate that.
  reply.set_error(status);

  if (status == device_management::DEVICE_MANAGEMENT_ERROR_NOT_SET) {
    *reply.mutable_fwmp() = fwmp;
  }
  response->Return(reply);
}

void DeviceManagementServiceAdaptor::SetFirmwareManagementParameters(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        device_management::SetFirmwareManagementParametersReply>> response,
    const device_management::SetFirmwareManagementParametersRequest& request) {
  VLOG(1) << __func__;
  device_management::SetFirmwareManagementParametersReply reply;

  CHECK(service_);
  auto status = service_->SetFirmwareManagementParameters(request.fwmp());
  // Note, if there's no error, then |status| is set to
  // DEVICE_MANAGEMENT_ERROR_NOT_SET to indicate that.
  reply.set_error(status);
  response->Return(reply);
}

void DeviceManagementServiceAdaptor::RemoveFirmwareManagementParameters(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        device_management::RemoveFirmwareManagementParametersReply>> response,
    const device_management::RemoveFirmwareManagementParametersRequest&
        request) {
  VLOG(1) << __func__;
  device_management::RemoveFirmwareManagementParametersReply reply;

  CHECK(service_);
  if (!service_->RemoveFirmwareManagementParameters()) {
    reply.set_error(
        device_management::
            DEVICE_MANAGEMENT_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_REMOVE);  // NOLINT(whitespace/line_length)
  }
  response->Return(reply);
}
}  // namespace device_management
