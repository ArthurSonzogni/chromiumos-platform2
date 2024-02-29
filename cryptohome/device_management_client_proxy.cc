// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/device_management_client_proxy.h"

#include <string>

#include <base/check.h>

namespace cryptohome {
namespace {
// Converts a brillo::Error* to string for printing.
std::string BrilloErrorToString(brillo::Error* err) {
  std::string result;
  if (err) {
    result = "(" + err->GetDomain() + ", " + err->GetCode() + ", " +
             err->GetMessage() + ")";
  } else {
    result = "(null)";
  }
  return result;
}
}  // namespace

DeviceManagementClientProxy::DeviceManagementClientProxy() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus = new dbus::Bus(options);
  if (!bus->Connect()) {
    LOG(ERROR) << "D-Bus system bus is not ready";
    return;
  }
  device_management_proxy_ =
      std::make_unique<org::chromium::DeviceManagementProxy>(bus);
}

DeviceManagementClientProxy::DeviceManagementClientProxy(
    scoped_refptr<dbus::Bus> bus) {
  device_management_proxy_ =
      std::make_unique<org::chromium::DeviceManagementProxy>(bus);
}

bool DeviceManagementClientProxy::IsEnterpriseOwned() {
  CHECK(device_management_proxy_);
  device_management::EnterpriseOwnedGetStatusRequest req;
  device_management::EnterpriseOwnedGetStatusReply reply;
  brillo::ErrorPtr error;
  if (!device_management_proxy_->EnterpriseOwnedGetStatus(req, &reply, &error,
                                                          kDefaultTimeout) ||
      error) {
    LOG(ERROR) << "EnterpriseOwnedGetStatus() call failed from proxy: "
               << BrilloErrorToString(error.get());
    return false;
  }
  if (reply.error() == device_management::DeviceManagementErrorCode::
                           DEVICE_MANAGEMENT_ERROR_NOT_ENTERPRISED_OWNED) {
    return false;
  }
  return true;
}

bool DeviceManagementClientProxy::IsInstallAttributesReady() {
  device_management::InstallAttributesGetStatusRequest req;
  device_management::InstallAttributesGetStatusReply reply;
  brillo::ErrorPtr error;
  if (!device_management_proxy_->InstallAttributesGetStatus(req, &reply, &error,
                                                            kDefaultTimeout) ||
      error) {
    LOG(ERROR) << "InstallAttributesGetStatus() call failed from proxy: "
               << BrilloErrorToString(error.get());
    return false;
  }

  if (reply.state() == device_management::InstallAttributesState::UNKNOWN ||
      reply.state() ==
          device_management::InstallAttributesState::TPM_NOT_OWNED) {
    LOG(ERROR) << "InstallAttributes() is not ready.";
    return false;
  }
  return true;
}

bool DeviceManagementClientProxy::InstallAttributesFinalize() {
  CHECK(device_management_proxy_);

  // Make sure install attributes are ready.
  if (!IsInstallAttributesReady()) {
    return false;
  }

  device_management::InstallAttributesFinalizeRequest req;
  device_management::InstallAttributesFinalizeReply reply;
  brillo::ErrorPtr error;
  error.reset();
  if (!device_management_proxy_->InstallAttributesFinalize(req, &reply, &error,
                                                           kDefaultTimeout) ||
      error) {
    LOG(ERROR) << "InstallAttributesFinalize() call failed from proxy: "
               << BrilloErrorToString(error.get());
    return false;
  }
  if (reply.error() != device_management::DeviceManagementErrorCode::
                           DEVICE_MANAGEMENT_ERROR_NOT_SET) {
    return false;
  }
  return true;
}

bool DeviceManagementClientProxy::InstallAttributesSet(
    const std::string& name, const std::vector<uint8_t>& data) {
  CHECK(device_management_proxy_);
  device_management::InstallAttributesSetRequest req;
  device_management::InstallAttributesSetReply reply;
  brillo::ErrorPtr error;
  error.reset();
  req.set_name(name);
  req.set_value(
      std::string(reinterpret_cast<const char*>(data.data()), data.size()));
  if (!device_management_proxy_->InstallAttributesSet(req, &reply, &error,
                                                      kDefaultTimeout) ||
      error) {
    LOG(ERROR) << "InstallAttributesSet() call failed from proxy: "
               << BrilloErrorToString(error.get());
    return false;
  }
  if (reply.error() != device_management::DeviceManagementErrorCode::
                           DEVICE_MANAGEMENT_ERROR_NOT_SET) {
    return false;
  }
  return true;
}

bool DeviceManagementClientProxy::IsInstallAttributesFirstInstall() {
  CHECK(device_management_proxy_);
  device_management::InstallAttributesGetStatusRequest req;
  device_management::InstallAttributesGetStatusReply reply;
  brillo::ErrorPtr error;
  if (!device_management_proxy_->InstallAttributesGetStatus(req, &reply, &error,
                                                            kDefaultTimeout) ||
      error) {
    LOG(ERROR) << "InstallAttributesGetStatus() call failed from proxy: "
               << BrilloErrorToString(error.get());
    return false;
  }
  if (reply.error() != device_management::DeviceManagementErrorCode::
                           DEVICE_MANAGEMENT_ERROR_NOT_SET) {
    return false;
  }
  if (reply.state() !=
      device_management::InstallAttributesState::FIRST_INSTALL) {
    return false;
  }
  return true;
}

}  // namespace cryptohome
