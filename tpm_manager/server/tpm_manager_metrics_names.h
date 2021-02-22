// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TPM_MANAGER_SERVER_TPM_MANAGER_METRICS_NAMES_H_
#define TPM_MANAGER_SERVER_TPM_MANAGER_METRICS_NAMES_H_

namespace tpm_manager {

constexpr char kDictionaryAttackResetStatusHistogram[] =
    "Platform.TPM.DictionaryAttackResetStatus";
constexpr char kDictionaryAttackCounterHistogram[] =
    "Platform.TPM.DictionaryAttackCounter";
constexpr char kTPMVersionFingerprint[] = "Platform.TPM.VersionFingerprint";

// The secret status records the presence of each secret in the bit fields along
// with the TPM version information. Currently only the least significant byte
// is used.
constexpr char kSecretStatusHitogram[] = "Platform.TPM.TpmManagerSecretStatus";
constexpr int kSecretStatusHasResetLockPermissions = 1 << 0;
constexpr int kSecretStatusHasOwnerDelegate = 1 << 1;
constexpr int kSecretStatusHasLockoutPassword = 1 << 2;
constexpr int kSecretStatusHasEndorsementPassword = 1 << 3;
constexpr int kSecretStatusHasOwnerPassword = 1 << 4;
// (1<<5) ~ (1<<6) are reserved for future use.
constexpr int kSecretStatusIsTpm2 = 1 << 7;
constexpr int kSecretMaxBit = kSecretStatusIsTpm2;

}  // namespace tpm_manager

#endif  // TPM_MANAGER_SERVER_TPM_MANAGER_METRICS_NAMES_H_
