
// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_MANAGEMENT_DEVICE_MANAGEMENT_ADAPTOR_H_
#define DEVICE_MANAGEMENT_DEVICE_MANAGEMENT_ADAPTOR_H_

#include "device_management/dbus_interface.h"
#include "device_management/device_management_service.h"

#include "device_management/proto_bindings/device_management_interface.pb.h"
// Requires `device_management/device_management_interface.pb.h`
#include "device_management/dbus_adaptors/org.chromium.DeviceManagement.h"

#include <memory>
#include <utility>

#include <base/memory/ref_counted.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/dbus/dbus_object.h>

namespace device_management {
class DeviceManagementServiceAdaptor
    : public org::chromium::DeviceManagementInterface,
      public org::chromium::DeviceManagementAdaptor {
 public:
  explicit DeviceManagementServiceAdaptor(scoped_refptr<dbus::Bus> bus,
                                          DeviceManagementService* service);
  DeviceManagementServiceAdaptor(const DeviceManagementServiceAdaptor&) =
      delete;
  DeviceManagementServiceAdaptor& operator=(
      const DeviceManagementServiceAdaptor&) = delete;

  void RegisterAsync(
      brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb);

  // org::chromium::DeviceManagementInterface overrides.
  void InstallAttributesGet(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          device_management::InstallAttributesGetReply>> response,
      const device_management::InstallAttributesGetRequest& request) override;
  void InstallAttributesSet(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          device_management::InstallAttributesSetReply>> response,
      const device_management::InstallAttributesSetRequest& request) override;
  void InstallAttributesFinalize(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          device_management::InstallAttributesFinalizeReply>> response,
      const device_management::InstallAttributesFinalizeRequest& request)
      override;
  void InstallAttributesGetStatus(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          device_management::InstallAttributesGetStatusReply>> response,
      const device_management::InstallAttributesGetStatusRequest& request)
      override;
  void GetFirmwareManagementParameters(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          device_management::GetFirmwareManagementParametersReply>> response,
      const device_management::GetFirmwareManagementParametersRequest& request)
      override;
  void SetFirmwareManagementParameters(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          device_management::SetFirmwareManagementParametersReply>> response,
      const device_management::SetFirmwareManagementParametersRequest& request)
      override;
  void RemoveFirmwareManagementParameters(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          device_management::RemoveFirmwareManagementParametersReply>> response,
      const device_management::RemoveFirmwareManagementParametersRequest&
          request) override;

 private:
  // This is the object that holds most of the states that this adaptor uses,
  // it also contains most of the actual logics.
  // This object is owned by the parent dbus service daemon, and whose lifetime
  // will cover the entire lifetime of this class.
  DeviceManagementService* service_;
  brillo::dbus_utils::DBusObject dbus_object_;
};
}  // namespace device_management

#endif  // DEVICE_MANAGEMENT_DEVICE_MANAGEMENT_ADAPTOR_H_
