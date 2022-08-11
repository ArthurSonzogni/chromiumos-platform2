// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tpm_manager/server/tpm_manager_metrics.h"
#include "tpm_manager/server/tpm_manager_metrics_names.h"

#include <libhwsec-foundation/tpm/tpm_version.h>

namespace tpm_manager {

namespace {

constexpr int kDictionaryAttackCounterNumBuckets = 100;
constexpr int kSecretStatusNumBuckets = kSecretMaxBit << 1;

constexpr base::TimeDelta kTimeToTakeOwnershipMin = base::Milliseconds(1);
constexpr base::TimeDelta kTimeToTakeOwnershipMax = base::Minutes(5);
constexpr int kTimeToTakeOwnershipNumBuckets = 50;

}  // namespace


void TpmManagerMetrics::ReportDictionaryAttackResetStatus(
    DictionaryAttackResetStatus status) {
  metrics_library_->SendEnumToUMA(kDictionaryAttackResetStatusHistogram, status,
                                  kDictionaryAttackResetStatusNumBuckets);
}

void TpmManagerMetrics::ReportDictionaryAttackCounter(int counter) {
  metrics_library_->SendEnumToUMA(kDictionaryAttackCounterHistogram, counter,
                                  kDictionaryAttackCounterNumBuckets);
}

void TpmManagerMetrics::ReportSecretStatus(const SecretStatus& status) {
  int flags = 0;

  TPM_SELECT_BEGIN;
  TPM2_SECTION({ flags |= kSecretStatusIsTpm2; });
  OTHER_TPM_SECTION();
  TPM_SELECT_END;

  if (status.has_owner_password) {
    flags |= kSecretStatusHasOwnerPassword;
  }
  if (status.has_endorsement_password) {
    flags |= kSecretStatusHasEndorsementPassword;
  }
  if (status.has_lockout_password) {
    flags |= kSecretStatusHasLockoutPassword;
  }
  if (status.has_owner_delegate) {
    flags |= kSecretStatusHasOwnerDelegate;
  }
  if (status.has_reset_lock_permissions) {
    flags |= kSecretStatusHasResetLockPermissions;
  }
  metrics_library_->SendEnumToUMA(kSecretStatusHitogram, flags,
                                  kSecretStatusNumBuckets);
}

void TpmManagerMetrics::ReportVersionFingerprint(int fingerprint) {
  metrics_library_->SendSparseToUMA(kTPMVersionFingerprint, fingerprint);
}

void TpmManagerMetrics::ReportAlertsData(const TpmStatus::AlertsData& alerts) {
  for (int i = 0; i < std::size(alerts.counters); i++) {
    uint16_t counter = alerts.counters[i];
    if (counter) {
      LOG(INFO) << "TPM alert of type " << i << " reported " << counter
                << " time(s)";
    }
    for (int c = 0; c < counter; c++) {
      metrics_library_->SendEnumToUMA(kTPMAlertsHistogram, i,
                                      std::size(alerts.counters));
    }
  }
}

void TpmManagerMetrics::ReportTimeToTakeOwnership(
    base::TimeDelta elapsed_time) {
  metrics_library_->SendToUMA(
      kTPMTimeToTakeOwnership, elapsed_time.InMilliseconds(),
      kTimeToTakeOwnershipMin.InMilliseconds(),
      kTimeToTakeOwnershipMax.InMilliseconds(), kTimeToTakeOwnershipNumBuckets);
}

void TpmManagerMetrics::ReportPowerWashResult(TPMPowerWashResult result) {
  constexpr auto max_value = static_cast<int>(TPMPowerWashResult::kMaxValue);
  metrics_library_->SendEnumToUMA(kTPMPowerWashResult, static_cast<int>(result),
                                  max_value + 1);
}

}  // namespace tpm_manager
