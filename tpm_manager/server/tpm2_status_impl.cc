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

namespace {

tpm_manager::RoVerificationStatus MapRoStatus(
    const trunks::TpmUtility::ApRoStatus& raw_status) {
  switch (raw_status) {
    case trunks::TpmUtility::ApRoStatus::kApRoNotRun:
      return tpm_manager::RO_STATUS_NOT_TRIGGERED;
    case trunks::TpmUtility::ApRoStatus::kApRoPass:
      return tpm_manager::RO_STATUS_PASS;
    case trunks::TpmUtility::ApRoStatus::kApRoFail:
      return tpm_manager::RO_STATUS_FAIL;
    case trunks::TpmUtility::ApRoStatus::kApRoUnsupported:
      return tpm_manager::RO_STATUS_UNSUPPORTED;
  }
}

}  // namespace

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

  if (trunks_tpm_state_->IsOwned() && TestTpmSrkAndSaltingSession()) {
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

bool Tpm2StatusImpl::SupportPinweaver() {
  uint8_t protocol_version;

  if (trunks_tpm_utility_->PinWeaverIsSupported(0, &protocol_version) ==
      TPM_RC_SUCCESS) {
    return true;
  }

  return false;
}

GscVersion Tpm2StatusImpl::GetGscVersion() {
  // Currently we don't have method to distinguish Ti50.

  if (trunks_tpm_utility_->IsCr50()) {
    return GscVersion::GSC_VERSION_CR50;
  }

  return GscVersion::GSC_VERSION_NOT_GSC;
}

bool Tpm2StatusImpl::TestTpmSrkAndSaltingSession() {
  trunks::TPMT_PUBLIC public_area;
  TPM_RC result = trunks_tpm_utility_->GetKeyPublicArea(trunks::kStorageRootKey,
                                                        &public_area);
  if (result != TPM_RC_SUCCESS) {
    LOG(WARNING) << "Failed to get the SRK public area: "
                 << trunks::GetErrorString(result);
    return false;
  }
  if (!(public_area.object_attributes & trunks::kSensitiveDataOrigin)) {
    LOG(WARNING) << "SRK doesn't have kSensitiveDataOrigin attribute.";
    return false;
  }
  if (!(public_area.object_attributes & trunks::kUserWithAuth)) {
    LOG(WARNING) << "SRK doesn't have kUserWithAuth attribute.";
    return false;
  }
  if (!(public_area.object_attributes & trunks::kNoDA)) {
    LOG(WARNING) << "SRK doesn't have kNoDA attribute.";
    return false;
  }
  if (!(public_area.object_attributes & trunks::kRestricted)) {
    LOG(WARNING) << "SRK doesn't have kRestricted attribute.";
    return false;
  }
  if (!(public_area.object_attributes & trunks::kDecrypt)) {
    LOG(WARNING) << "SRK doesn't have kDecrypt attribute.";
    return false;
  }

  // Check the salting session.
  std::unique_ptr<trunks::HmacSession> session =
      trunks_factory_.GetHmacSession();
  result = session->StartUnboundSession(true, false);
  if (result != TPM_RC_SUCCESS) {
    LOG(WARNING) << "Failed to create unbound session: "
                 << trunks::GetErrorString(result);
    return false;
  }

  return true;
}

bool Tpm2StatusImpl::GetRoVerificationStatus(
    tpm_manager::RoVerificationStatus* status) {
  trunks::TpmUtility::ApRoStatus raw_status;
  TPM_RC result = trunks_tpm_utility_->GetRoVerificationStatus(&raw_status);
  if (result != TPM_RC_SUCCESS) {
    return false;
  }
  *status = MapRoStatus(raw_status);
  return true;
}

}  // namespace tpm_manager
