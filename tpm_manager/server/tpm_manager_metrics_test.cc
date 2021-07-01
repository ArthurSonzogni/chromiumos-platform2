// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tpm_manager/server/tpm_manager_metrics.h"

#include <type_traits>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/tpm/tpm_version.h>
#include <metrics/metrics_library_mock.h>

#include "tpm_manager/server/tpm_manager_metrics_names.h"

namespace tpm_manager {

namespace {

using ::testing::_;
using ::testing::Return;
using ::testing::StrictMock;

SecretStatus ToSecretStatus(int flags) {
  const SecretStatus status = {
      .has_owner_password =
          static_cast<bool>(flags & kSecretStatusHasOwnerPassword),
      .has_endorsement_password =
          static_cast<bool>(flags & kSecretStatusHasEndorsementPassword),
      .has_lockout_password =
          static_cast<bool>(flags & kSecretStatusHasLockoutPassword),
      .has_owner_delegate =
          static_cast<bool>(flags & kSecretStatusHasOwnerDelegate),
      .has_reset_lock_permissions =
          static_cast<bool>(flags & kSecretStatusHasResetLockPermissions),
  };
  return status;
}

}  // namespace

class TpmManagerMetricsTest : public ::testing::Test {
 public:
  TpmManagerMetricsTest() {
    SET_DEFAULT_TPM_FOR_TESTING;
    tpm_manager_metrics_.set_metrics_library_for_testing(
        &mock_metrics_library_);
  }

 protected:
  StrictMock<MetricsLibraryMock> mock_metrics_library_;
  TpmManagerMetrics tpm_manager_metrics_;
};

TEST_F(TpmManagerMetricsTest, ReportDictionaryAttackResetStatus) {
  // Selectively tests the enums to see if the parameters are correctly passed.
  const DictionaryAttackResetStatus statuses[]{
      kResetNotNecessary,
      kResetAttemptSucceeded,
      kResetAttemptFailed,
  };
  for (auto status : statuses) {
    EXPECT_CALL(mock_metrics_library_,
                SendEnumToUMA(kDictionaryAttackResetStatusHistogram, status,
                              DictionaryAttackResetStatus::
                                  kDictionaryAttackResetStatusNumBuckets))
        .WillOnce(Return(true));
    tpm_manager_metrics_.ReportDictionaryAttackResetStatus(status);
  }
}

TEST_F(TpmManagerMetricsTest, ReportDictionaryAttackCounter) {
  EXPECT_CALL(mock_metrics_library_,
              SendEnumToUMA(kDictionaryAttackCounterHistogram, 0, _))
      .WillOnce(Return(true));
  tpm_manager_metrics_.ReportDictionaryAttackCounter(0);
  EXPECT_CALL(mock_metrics_library_,
              SendEnumToUMA(kDictionaryAttackCounterHistogram, 10, _))
      .WillOnce(Return(true));
  tpm_manager_metrics_.ReportDictionaryAttackCounter(10);
}

TEST_F(TpmManagerMetricsTest, ReportSecretStatus) {
  for (int i = 0; i < (1 << 5); ++i) {
    int expected_entry = i;

    TPM_SELECT_BEGIN;
    TPM2_SECTION({ expected_entry |= kSecretStatusIsTpm2; });
    OTHER_TPM_SECTION();
    TPM_SELECT_END;

    EXPECT_CALL(mock_metrics_library_,
                SendEnumToUMA(kSecretStatusHitogram, expected_entry, _))
        .WillOnce(Return(true));
    tpm_manager_metrics_.ReportSecretStatus(ToSecretStatus(i));
  }
}

TEST_F(TpmManagerMetricsTest, ReportVersionFingerprint) {
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kTPMVersionFingerprint, 0xdeadbeaf))
      .WillOnce(Return(true));
  tpm_manager_metrics_.ReportVersionFingerprint(0xdeadbeaf);
}

TEST_F(TpmManagerMetricsTest, ReportTimeToTakeOwnership) {
  const base::TimeDelta elapsed_time = base::TimeDelta::FromMinutes(3);
  EXPECT_CALL(mock_metrics_library_,
              SendToUMA(kTPMTimeToTakeOwnership, elapsed_time.InMilliseconds(),
                        _, _, _))
      .WillOnce(Return(true));
  tpm_manager_metrics_.ReportTimeToTakeOwnership(elapsed_time);
}

}  // namespace tpm_manager
