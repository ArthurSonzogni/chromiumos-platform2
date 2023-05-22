// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_management/device_management_service.h"
#include "device_management/proto_bindings/device_management_interface.pb.h"
#include "fwmp/firmware_management_parameters.h"

#include <memory>
#include <vector>

#include <base/logging.h>
#include <libhwsec/factory/factory_impl.h>
#include <libhwsec/structures/threading_mode.h>

namespace device_management {

DeviceManagementService::DeviceManagementService()
    : firmware_management_parameters_(nullptr) {}
DeviceManagementService::~DeviceManagementService() {}

void DeviceManagementService::Initialize(
    const hwsec::CryptohomeFrontend& hwsec_) {
  firmware_management_parameters_ =
      fwmp::FirmwareManagementParameters::CreateInstance(&hwsec_);
}

device_management::DeviceManagementErrorCode
DeviceManagementService::GetFirmwareManagementParameters(
    device_management::FirmwareManagementParameters* fwmp) {
  if (!firmware_management_parameters_->Load()) {
    return device_management::
        DEVICE_MANAGEMENT_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID;
  }

  uint32_t flags;
  if (firmware_management_parameters_->GetFlags(&flags)) {
    fwmp->set_flags(flags);
  } else {
    LOG(WARNING)
        << "Failed to GetFlags() for GetFirmwareManagementParameters().";
    return device_management::
        DEVICE_MANAGEMENT_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID;
  }

  std::vector<uint8_t> hash;
  if (firmware_management_parameters_->GetDeveloperKeyHash(&hash)) {
    *fwmp->mutable_developer_key_hash() = {hash.begin(), hash.end()};
  } else {
    LOG(WARNING) << "Failed to GetDeveloperKeyHash() for "
                    "GetFirmwareManagementParameters().";
    return device_management::
        DEVICE_MANAGEMENT_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID;
  }

  return device_management::DEVICE_MANAGEMENT_ERROR_NOT_SET;
}

device_management::DeviceManagementErrorCode
DeviceManagementService::SetFirmwareManagementParameters(
    const device_management::FirmwareManagementParameters& fwmp) {
  if (!firmware_management_parameters_->Create()) {
    return device_management::
        DEVICE_MANAGEMENT_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE;
  }

  uint32_t flags = fwmp.flags();
  std::unique_ptr<std::vector<uint8_t>> hash;

  if (!fwmp.developer_key_hash().empty()) {
    hash.reset(new std::vector<uint8_t>(fwmp.developer_key_hash().begin(),
                                        fwmp.developer_key_hash().end()));
  }

  if (!firmware_management_parameters_->Store(flags, hash.get())) {
    return device_management::
        DEVICE_MANAGEMENT_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE;
  }

  return device_management::DEVICE_MANAGEMENT_ERROR_NOT_SET;
}

bool DeviceManagementService::RemoveFirmwareManagementParameters() {
  return firmware_management_parameters_->Destroy();
}
}  // namespace device_management
