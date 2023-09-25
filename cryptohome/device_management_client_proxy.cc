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

}  // namespace cryptohome
