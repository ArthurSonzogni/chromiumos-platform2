// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/write_protect_utils_impl.h"

#include <memory>
#include <utility>

#include <base/logging.h>

#include "rmad/utils/crossystem_utils_impl.h"
#include "rmad/utils/ec_utils_impl.h"
#include "rmad/utils/flashrom_utils_impl.h"

namespace rmad {

WriteProtectUtilsImpl::WriteProtectUtilsImpl()
    : crossystem_utils_(std::make_unique<CrosSystemUtilsImpl>()),
      ec_utils_(std::make_unique<EcUtilsImpl>()),
      flashrom_utils_(std::make_unique<FlashromUtilsImpl>()) {}

WriteProtectUtilsImpl::WriteProtectUtilsImpl(
    std::unique_ptr<CrosSystemUtils> crossystem_utils,
    std::unique_ptr<EcUtils> ec_utils,
    std::unique_ptr<FlashromUtils> flashrom_utils)
    : crossystem_utils_(std::move(crossystem_utils)),
      ec_utils_(std::move(ec_utils)),
      flashrom_utils_(std::move(flashrom_utils)) {}

bool WriteProtectUtilsImpl::GetHardwareWriteProtectionStatus(
    bool* enabled) const {
  int hwwp_status;
  if (!crossystem_utils_->GetHwwpStatus(&hwwp_status)) {
    LOG(ERROR) << "Failed to get hardware write protect with crossystem utils.";
    return false;
  }

  *enabled = (hwwp_status == 1);
  return true;
}

bool WriteProtectUtilsImpl::GetApWriteProtectionStatus(bool* enabled) const {
  if (!flashrom_utils_->GetApWriteProtectionStatus(enabled)) {
    LOG(ERROR) << "Failed to get AP write protect with flashrom utils.";
    return false;
  }
  return true;
}

bool WriteProtectUtilsImpl::GetEcWriteProtectionStatus(bool* enabled) const {
  if (!flashrom_utils_->GetEcWriteProtectionStatus(enabled)) {
    LOG(ERROR) << "Failed to get EC write protect with flashrom utils.";
    return false;
  }
  return true;
}

bool WriteProtectUtilsImpl::DisableSoftwareWriteProtection() {
  return flashrom_utils_->DisableSoftwareWriteProtection();
}

bool WriteProtectUtilsImpl::EnableSoftwareWriteProtection() {
  // Enable EC write protection.
  if (!ec_utils_->EnableSoftwareWriteProtection()) {
    LOG(ERROR) << "Failed to enable EC SWWP";
    return false;
  }

  // Enable AP write protection.
  return flashrom_utils_->EnableApSoftwareWriteProtection();
}

}  // namespace rmad
