// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tpm_manager/server/tpm2_status_impl.h"

#include <string>

#include <base/check.h>
#include <base/logging.h>
#include <trunks/error_codes.h>
#include <trunks/tpm_generated.h>
#include <trunks/trunks_factory_impl.h>

using trunks::TPM_RC;
using trunks::TPM_RC_SUCCESS;

namespace tpm_manager {

Tpm2StatusImpl::Tpm2StatusImpl(const trunks::TrunksFactory& factory)
    : trunks_factory_(factory),
      trunks_tpm_state_(trunks_factory_.GetTpmState()),
      trunks_tpm_utility_(trunks_factory_.GetTpmUtility()) {}

bool Tpm2StatusImpl::IsTpmEnabled() {
  // For 2.0, TPM is always enabled.
  return true;
}

bool Tpm2StatusImpl::GetTpmOwned(TpmStatus::TpmOwnershipStatus* status) {
  CHECK(status);
  if (kTpmOwned == ownership_status_) {
    *status = kTpmOwned;
    return true;
  }

  if (!Refresh()) {
    return false;
  }

  if (trunks_tpm_state_->IsOwned()) {
    ownership_status_ = kTpmOwned;
  } else if (trunks_tpm_state_->IsOwnerPasswordSet()) {
    ownership_status_ = kTpmPreOwned;
  }

  *status = ownership_status_;
  return true;
}

bool Tpm2StatusImpl::GetDictionaryAttackInfo(uint32_t* counter,
                                             uint32_t* threshold,
                                             bool* lockout,
                                             uint32_t* seconds_remaining) {
  CHECK(counter);
  CHECK(threshold);
  CHECK(lockout);
  CHECK(seconds_remaining);

  if (!Refresh()) {
    return false;
  }

  *counter = trunks_tpm_state_->GetLockoutCounter();
  *threshold = trunks_tpm_state_->GetLockoutThreshold();
  *lockout = trunks_tpm_state_->IsInLockout();
  *seconds_remaining = trunks_tpm_state_->GetLockoutCounter() *
                       trunks_tpm_state_->GetLockoutInterval();
  return true;
}

bool Tpm2StatusImpl::IsDictionaryAttackMitigationEnabled(bool* is_enabled) {
  CHECK(is_enabled);

  if (!Refresh()) {
    return false;
  }
  *is_enabled = trunks_tpm_state_->GetLockoutInterval() != 0 ||
                trunks_tpm_state_->GetLockoutRecovery() != 0;
  return true;
}

bool Tpm2StatusImpl::GetVersionInfo(uint32_t* family,
                                    uint64_t* spec_level,
                                    uint32_t* manufacturer,
                                    uint32_t* tpm_model,
                                    uint64_t* firmware_version,
                                    std::vector<uint8_t>* vendor_specific) {
  CHECK(family);
  CHECK(spec_level);
  CHECK(manufacturer);
  CHECK(tpm_model);
  CHECK(firmware_version);
  CHECK(vendor_specific);

  if (!Refresh()) {
    return false;
  }

  *family = trunks_tpm_state_->GetTpmFamily();

  uint64_t level = trunks_tpm_state_->GetSpecificationLevel();
  uint64_t revision = trunks_tpm_state_->GetSpecificationRevision();
  *spec_level = (level << 32) | revision;

  *manufacturer = trunks_tpm_state_->GetManufacturer();
  *tpm_model = trunks_tpm_state_->GetTpmModel();
  *firmware_version = trunks_tpm_state_->GetFirmwareVersion();

  std::string vendor_id_string = trunks_tpm_state_->GetVendorIDString();
  const uint8_t* data =
      reinterpret_cast<const uint8_t*>(vendor_id_string.data());
  vendor_specific->assign(data, data + vendor_id_string.size());
  return true;
}

bool Tpm2StatusImpl::Refresh() {
  TPM_RC result = trunks_tpm_state_->Initialize();
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error initializing trunks tpm state: "
               << trunks::GetErrorString(result);
    return false;
  }
  initialized_ = true;
  return true;
}

void Tpm2StatusImpl::MarkRandomOwnerPasswordSet() {
  LOG(ERROR) << __func__ << ": Not implemented";
}

bool Tpm2StatusImpl::SupportU2f() {
  // We support U2F on Cr50.
  if (trunks_tpm_utility_->IsCr50()) {
    return true;
  }

  return false;
}

}  // namespace tpm_manager
