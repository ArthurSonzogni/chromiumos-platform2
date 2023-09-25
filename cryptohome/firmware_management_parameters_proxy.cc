// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/firmware_management_parameters_proxy.h"

#include <string>
#include <utility>

#include <base/check.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>

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

bool FirmwareManagementParametersProxy::GetFWMP(
    user_data_auth::FirmwareManagementParameters* fwmp) {
  CHECK(device_management_proxy_);
  device_management::GetFirmwareManagementParametersRequest request;
  device_management::GetFirmwareManagementParametersReply reply;
  brillo::ErrorPtr error;

  if (!device_management_proxy_->GetFirmwareManagementParameters(
          request, &reply, &error, kDefaultTimeout) ||
      error) {
    LOG(ERROR) << "Failed to call GetFirmwareManagementParameters through "
                  "proxy class: "
               << BrilloErrorToString(error.get());
    return false;
  }
  if (reply.error() != device_management::DeviceManagementErrorCode::
                           DEVICE_MANAGEMENT_ERROR_NOT_SET) {
    LOG(ERROR) << "Failed to call GetFirmwareManagementParameters through "
                  "proxy class: status "
               << static_cast<int>(reply.error());
    return false;
  }
  fwmp->set_flags(reply.fwmp().flags());
  fwmp->set_developer_key_hash(reply.fwmp().developer_key_hash());
  return true;
}

bool FirmwareManagementParametersProxy::SetFWMP(
    const user_data_auth::FirmwareManagementParameters& fwmp) {
  CHECK(device_management_proxy_);
  device_management::SetFirmwareManagementParametersRequest request;
  device_management::SetFirmwareManagementParametersReply reply;
  brillo::ErrorPtr error;

  request.mutable_fwmp()->set_flags(fwmp.flags());
  request.mutable_fwmp()->set_developer_key_hash(fwmp.developer_key_hash());

  if (!device_management_proxy_->SetFirmwareManagementParameters(
          request, &reply, &error, kDefaultTimeout) ||
      error) {
    LOG(ERROR) << "Failed to call SetFirmwareManagementParameters through "
                  "proxy class: "
               << BrilloErrorToString(error.get());
    return false;
  }
  if (reply.error() != device_management::DeviceManagementErrorCode::
                           DEVICE_MANAGEMENT_ERROR_NOT_SET) {
    LOG(ERROR) << "Failed to call SetFirmwareManagementParameters through "
                  "proxy class: status "
               << static_cast<int>(reply.error());
    return false;
  }
  return true;
}

bool FirmwareManagementParametersProxy::Destroy() {
  CHECK(device_management_proxy_);
  device_management::RemoveFirmwareManagementParametersRequest request;
  device_management::RemoveFirmwareManagementParametersReply reply;
  brillo::ErrorPtr error;

  if (!device_management_proxy_->RemoveFirmwareManagementParameters(
          request, &reply, &error, kDefaultTimeout) ||
      error) {
    LOG(ERROR) << "Failed to call RemoveFirmwareManagementParameters through "
                  "proxy class: "
               << BrilloErrorToString(error.get());
    return false;
  }
  if (reply.error() != device_management::DeviceManagementErrorCode::
                           DEVICE_MANAGEMENT_ERROR_NOT_SET) {
    LOG(ERROR) << "Failed to call RemoveFirmwareManagementParameters through "
                  "proxy class: status "
               << static_cast<int>(reply.error());
    return false;
  }
  return true;
}

void FirmwareManagementParametersProxy::SetDeviceManagementProxy(
    std::unique_ptr<org::chromium::DeviceManagementProxy> proxy) {
  device_management_proxy_ = std::move(proxy);
}
}  // namespace cryptohome
