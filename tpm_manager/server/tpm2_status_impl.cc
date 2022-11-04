// Copyright 2015 The ChromiumOS Authors
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

// Keep it with sync to UMA enum list
// https://chromium.googlesource.com/chromium/src/+/HEAD/tools/metrics/histograms/enums.xml
// These values are persisted to logs, and should therefore never be renumbered
// nor reused.
enum class TpmAlerts {
  kCamoBreach = 1,
  kDmemParity = 2,
  kDrfParity = 3,
  kImemParity = 4,
  kPgmFault = 5,
  kCpuDIfBusError = 6,
  kCpuDIfUpdateWatchdog = 7,
  kCpuIIfBusError = 8,
  kCpuIIfUpdateWatchdog = 9,
  kCpuSIfBusError = 10,
  kCpuSIfUpdateWatchdog = 11,
  kDmaIfBusErr = 12,
  kDmaIfUpdateWatchdog = 13,
  kSpsIfBusErr = 14,
  kSpsIfUpdateWatchdog = 15,
  kUsbIfBusErr = 16,
  kUsbIfUpdateWatchdog = 17,
  kFuseDefaults = 18,
  kDiffFail = 19,
  kSoftwareAlert0 = 20,
  kSoftwareAlert1 = 21,
  kSoftwareAlert2 = 22,
  kSoftwareAlert3 = 23,
  kHearbitFail = 24,
  kProcOpcodeHash = 25,
  kSramParityScrub = 26,
  kAesExecCtrMax = 27,
  kAesHkey = 28,
  kCertLookup = 29,
  kFlashEntry = 30,
  kPw = 31,
  kShaExecCtrMax = 32,
  kShaFault = 33,
  kShaHkey = 34,
  kPmuBatteryMon = 35,
  kPmuWatchdog = 36,
  kRtcDead = 37,
  kTempMax = 38,
  kTempMaxDiff = 39,
  kTempMin = 40,
  kRngOutOfSpec = 41,
  kRngTimeout = 42,
  kVoltageError = 43,
  kXoJitteryTrim = 44,

  kTPMAlertNumBuckets,  // Must be the last entry.
};

constexpr size_t kTPMAlertNumBuckets =
    static_cast<size_t>(TpmAlerts::kTPMAlertNumBuckets);

static_assert(kTPMAlertNumBuckets <= trunks::kAlertsMaxSize + 1,
              "Number of UMA enums less than alerts set size");

// Maps alerts identifiers received from TMP firmware to UMA identifiers
constexpr TpmAlerts kH1AlertsMap[trunks::kH1AlertsSize] = {
    TpmAlerts::kCamoBreach,
    TpmAlerts::kDmemParity,
    TpmAlerts::kDrfParity,
    TpmAlerts::kImemParity,
    TpmAlerts::kPgmFault,
    TpmAlerts::kCpuDIfBusError,
    TpmAlerts::kCpuDIfUpdateWatchdog,
    TpmAlerts::kCpuIIfBusError,
    TpmAlerts::kCpuIIfUpdateWatchdog,
    TpmAlerts::kCpuSIfBusError,
    TpmAlerts::kCpuSIfUpdateWatchdog,
    TpmAlerts::kDmaIfBusErr,
    TpmAlerts::kDmaIfUpdateWatchdog,
    TpmAlerts::kSpsIfBusErr,
    TpmAlerts::kSpsIfUpdateWatchdog,
    TpmAlerts::kUsbIfBusErr,
    TpmAlerts::kUsbIfUpdateWatchdog,
    TpmAlerts::kFuseDefaults,
    TpmAlerts::kDiffFail,
    TpmAlerts::kSoftwareAlert0,
    TpmAlerts::kSoftwareAlert1,
    TpmAlerts::kSoftwareAlert2,
    TpmAlerts::kSoftwareAlert3,
    TpmAlerts::kHearbitFail,
    TpmAlerts::kProcOpcodeHash,
    TpmAlerts::kSramParityScrub,
    TpmAlerts::kAesExecCtrMax,
    TpmAlerts::kAesHkey,
    TpmAlerts::kCertLookup,
    TpmAlerts::kFlashEntry,
    TpmAlerts::kPw,
    TpmAlerts::kShaExecCtrMax,
    TpmAlerts::kShaFault,
    TpmAlerts::kShaHkey,
    TpmAlerts::kPmuBatteryMon,
    TpmAlerts::kPmuWatchdog,
    TpmAlerts::kRtcDead,
    TpmAlerts::kTempMax,
    TpmAlerts::kTempMaxDiff,
    TpmAlerts::kTempMin,
    TpmAlerts::kRngOutOfSpec,
    TpmAlerts::kRngTimeout,
    TpmAlerts::kVoltageError,
    TpmAlerts::kXoJitteryTrim,
};

tpm_manager::RoVerificationStatus MapRoStatus(
    const trunks::TpmUtility::ApRoStatus& raw_status) {
  switch (raw_status) {
    case trunks::TpmUtility::ApRoStatus::kApRoNotRun:
      return tpm_manager::RO_STATUS_NOT_TRIGGERED;
    case trunks::TpmUtility::ApRoStatus::kApRoPass:
      return tpm_manager::RO_STATUS_PASS;
    case trunks::TpmUtility::ApRoStatus::kApRoFail:
      return tpm_manager::RO_STATUS_FAIL;
    case trunks::TpmUtility::ApRoStatus::kApRoUnsupportedUnknown:
      return tpm_manager::RO_STATUS_UNSUPPORTED;
    case trunks::TpmUtility::ApRoStatus::kApRoUnsupportedNotTriggered:
      return tpm_manager::RO_STATUS_UNSUPPORTED_NOT_TRIGGERED;
    case trunks::TpmUtility::ApRoStatus::kApRoUnsupportedTriggered:
      return tpm_manager::RO_STATUS_UNSUPPORTED_TRIGGERED;
  }
  NOTREACHED();
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
  return true;
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

bool Tpm2StatusImpl::GetAlertsData(AlertsData* alerts) {
  trunks::TpmAlertsData trunks_alerts;
  TPM_RC result = trunks_tpm_utility_->GetAlertsData(&trunks_alerts);
  if (result == trunks::TPM_RC_NO_SUCH_COMMAND) {
    LOG(INFO) << "TPM GetAlertsData vendor command is not implemented";
    return false;
  } else if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error getting alerts data: "
               << trunks::GetErrorString(result);
    memset(alerts, 0, sizeof(AlertsData));
    return true;
  } else if (trunks_alerts.chip_family != trunks::kFamilyH1) {
    // Currently we support only H1 alerts
    LOG(ERROR) << "Unknown alerts family: " << trunks_alerts.chip_family;
    return false;
  }
  memset(alerts, 0, sizeof(AlertsData));
  for (int i = 0; i < trunks_alerts.alerts_num; i++) {
    size_t uma_idx = static_cast<size_t>(kH1AlertsMap[i]);
    if (uma_idx <= 0 || uma_idx >= kTPMAlertNumBuckets) {
      LOG(ERROR) << "Alert index " << i << " maps into invalid UMA enum index "
                 << uma_idx;
    } else {
      alerts->counters[uma_idx] = trunks_alerts.counters[i];
    }
  }
  return true;
}

}  // namespace tpm_manager
