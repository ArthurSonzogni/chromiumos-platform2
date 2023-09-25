// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/install_attributes_proxy.h"

#include <base/check.h>
#include <base/logging.h>

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

bool InstallAttributesProxy::Get(const std::string& name,
                                 brillo::Blob* value) const {
  CHECK(value);
  device_management::InstallAttributesGetRequest req;
  device_management::InstallAttributesGetReply reply;
  brillo::ErrorPtr error;

  req.set_name(name);

  CHECK(device_management_proxy_);
  if (!device_management_proxy_->InstallAttributesGet(req, &reply, &error,
                                                      kDefaultTimeout) ||
      error) {
    LOG(ERROR) << "InstallAttributesGet() call failed: "
               << BrilloErrorToString(error.get());
    return false;
  }
  if (reply.error() != device_management::DeviceManagementErrorCode::
                           DEVICE_MANAGEMENT_ERROR_NOT_SET) {
    LOG(ERROR) << "InstallAttributesGet() failed: "
               << BrilloErrorToString(error.get());
    return false;
  }
  *value = brillo::BlobFromString(reply.value());
  return true;
}

bool InstallAttributesProxy::Set(const std::string& name,
                                 const brillo::Blob& value) {
  device_management::InstallAttributesSetRequest req;
  device_management::InstallAttributesSetReply reply;
  brillo::ErrorPtr error;

  req.set_name(name);
  req.set_value(brillo::BlobToString(value));

  CHECK(device_management_proxy_);
  if (!device_management_proxy_->InstallAttributesSet(req, &reply, &error,
                                                      kDefaultTimeout) ||
      error) {
    LOG(ERROR) << "InstallAttributesSet() call failed: "
               << BrilloErrorToString(error.get());
    return false;
  }
  if (reply.error() != device_management::DeviceManagementErrorCode::
                           DEVICE_MANAGEMENT_ERROR_NOT_SET) {
    LOG(ERROR) << "InstallAttributesSet() failed: " << reply.error();
    return false;
  }
  return true;
}

bool InstallAttributesProxy::Finalize() {
  device_management::InstallAttributesFinalizeRequest req;
  device_management::InstallAttributesFinalizeReply reply;
  brillo::ErrorPtr error;

  CHECK(device_management_proxy_);
  if (!device_management_proxy_->InstallAttributesFinalize(req, &reply, &error,
                                                           kDefaultTimeout) ||
      error) {
    LOG(ERROR) << "InstallAttributesFinalize() call failed";
    return false;
  }
  if (reply.error() != device_management::DeviceManagementErrorCode::
                           DEVICE_MANAGEMENT_ERROR_NOT_SET) {
    LOG(ERROR) << "InstallAttributesFinalize() failed: " << reply.error();
    return false;
  }
  return true;
}

int InstallAttributesProxy::Count() const {
  device_management::InstallAttributesGetStatusRequest req;
  device_management::InstallAttributesGetStatusReply reply;
  brillo::ErrorPtr error;

  CHECK(device_management_proxy_);
  if (!device_management_proxy_->InstallAttributesGetStatus(req, &reply, &error,
                                                            kDefaultTimeout) ||
      error) {
    LOG(ERROR) << "InstallAttributesGetStatus() call failed: "
               << BrilloErrorToString(error.get());
    return false;
  }
  if (reply.error() != device_management::DeviceManagementErrorCode::
                           DEVICE_MANAGEMENT_ERROR_NOT_SET) {
    LOG(ERROR) << "InstallAttributesGetStatus() failed: " << reply.error();
    return false;
  }
  return reply.count();
}

bool InstallAttributesProxy::IsSecure() {
  device_management::InstallAttributesGetStatusRequest req;
  device_management::InstallAttributesGetStatusReply reply;
  brillo::ErrorPtr error;

  CHECK(device_management_proxy_);
  if (!device_management_proxy_->InstallAttributesGetStatus(req, &reply, &error,
                                                            kDefaultTimeout) ||
      error) {
    LOG(ERROR) << "InstallAttributesGetStatus() call failed: "
               << BrilloErrorToString(error.get());
    return false;
  }
  if (reply.error() != device_management::DeviceManagementErrorCode::
                           DEVICE_MANAGEMENT_ERROR_NOT_SET) {
    LOG(ERROR) << "InstallAttributesGetStatus() failed: " << reply.error();
    return false;
  }
  return reply.is_secure();
}

InstallAttributesProxy::Status InstallAttributesProxy::status() {
  device_management::InstallAttributesGetStatusRequest req;
  device_management::InstallAttributesGetStatusReply reply;
  brillo::ErrorPtr error;

  CHECK(device_management_proxy_);
  if (!device_management_proxy_->InstallAttributesGetStatus(req, &reply, &error,
                                                            kDefaultTimeout) ||
      error) {
    LOG(ERROR) << "InstallAttributesGetStatus() call failed: "
               << BrilloErrorToString(error.get());
  }
  if (reply.error() != device_management::DeviceManagementErrorCode::
                           DEVICE_MANAGEMENT_ERROR_NOT_SET) {
    LOG(ERROR) << "InstallAttributesGetStatus() failed: " << reply.error();
    return InstallAttributesProxy::Status::kUnknown;
  }

  switch (reply.state()) {
    case device_management::InstallAttributesState::UNKNOWN:
      return InstallAttributesProxy::Status::kUnknown;
    case device_management::InstallAttributesState::TPM_NOT_OWNED:
      return InstallAttributesProxy::Status::kTpmNotOwned;
    case device_management::InstallAttributesState::FIRST_INSTALL:
      return InstallAttributesProxy::Status::kFirstInstall;
    case device_management::InstallAttributesState::VALID:
      return InstallAttributesProxy::Status::kValid;
    case device_management::InstallAttributesState::INVALID:
      return InstallAttributesProxy::Status::kInvalid;
    default:
      return InstallAttributesProxy::Status::kInvalid;
  }
}

void InstallAttributesProxy::SetDeviceManagementProxy(
    std::unique_ptr<org::chromium::DeviceManagementProxy> proxy) {
  device_management_proxy_ = std::move(proxy);
}

}  // namespace cryptohome
