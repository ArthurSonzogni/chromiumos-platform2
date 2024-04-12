// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/system/device_management_client_impl.h"

#include <memory>
#include <utility>

#include <base/logging.h>
#include <base/memory/scoped_refptr.h>
#include <dbus/bus.h>
#include <device_management-client/device_management/dbus-proxies.h>

namespace rmad {

DeviceManagementClientImpl::DeviceManagementClientImpl(
    const scoped_refptr<dbus::Bus>& bus) {
  device_management_proxy_ =
      std::make_unique<org::chromium::DeviceManagementProxy>(bus);
}

DeviceManagementClientImpl::DeviceManagementClientImpl(
    std::unique_ptr<org::chromium::DeviceManagementProxyInterface>
        device_management_proxy)
    : device_management_proxy_(std::move(device_management_proxy)) {}

DeviceManagementClientImpl::~DeviceManagementClientImpl() = default;

bool DeviceManagementClientImpl::IsCcdBlocked() {
  uint32_t fwmp_flags;
  if (!GetFwmp(&fwmp_flags)) {
    return false;
  }
  return (fwmp_flags &
          cryptohome::DEVELOPER_DISABLE_CASE_CLOSED_DEBUGGING_UNLOCK) != 0;
}

bool DeviceManagementClientImpl::GetFwmp(uint32_t* flags) {
  device_management::GetFirmwareManagementParametersRequest request;
  device_management::GetFirmwareManagementParametersReply reply;

  brillo::ErrorPtr error;
  if (!device_management_proxy_->GetFirmwareManagementParameters(
          request, &reply, &error) ||
      error) {
    LOG(ERROR) << "Failed to call GetFirmwareManagementParameters from "
               << "device_management proxy";
    return false;
  }

  // This can be expected when the device doesn't have FWMP.
  if (reply.error() != device_management::DEVICE_MANAGEMENT_ERROR_NOT_SET) {
    VLOG(1) << "Failed to get FWMP. Error code " << reply.error();
    return false;
  }

  VLOG(1) << "Get FWMP flags: " << reply.fwmp().flags();
  if (flags) {
    *flags = reply.fwmp().flags();
  }
  return true;
}

}  // namespace rmad
