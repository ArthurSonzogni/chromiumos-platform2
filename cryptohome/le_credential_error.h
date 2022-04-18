// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_LE_CREDENTIAL_ERROR_H_
#define CRYPTOHOME_LE_CREDENTIAL_ERROR_H_

namespace cryptohome {

// List of error values returned from TPM for the Low Entropy Credential
// check routine.
enum LECredBackendError {
  // Credential check was successful.
  LE_TPM_SUCCESS = 0,
  // Check failed due to incorrect Low Entropy credential provided.
  LE_TPM_ERROR_INVALID_LE_SECRET,
  // Reset failed due to incorrect Reset credential provided.
  LE_TPM_ERROR_INVALID_RESET_SECRET,
  // Check failed since the credential has been locked out due to too many
  // attempts per the delay schedule.
  LE_TPM_ERROR_TOO_MANY_ATTEMPTS,
  // Check failed due to the hash tree being out of sync. This should
  // prompt a hash tree resynchronization and retry.
  LE_TPM_ERROR_HASH_TREE_SYNC,
  // Check failed due to an operation failing on the TPM side. This should
  // prompt a hash tree resynchronization and retry.
  LE_TPM_ERROR_TPM_OP_FAILED,
  // The PCR is in unexpected state. The basic way to proceed from here is to
  // reboot the device.
  LE_TPM_ERROR_PCR_NOT_MATCH,
};

// List of all the errors returned by the LECredentialManager interface.
enum LECredError {
  // Operation succeeded.
  LE_CRED_SUCCESS = 0,
  // Check failed due to incorrect Low Entropy(LE) secret.
  LE_CRED_ERROR_INVALID_LE_SECRET,
  // Check failed due to incorrect Reset secret.
  LE_CRED_ERROR_INVALID_RESET_SECRET,
  // Check failed due to too many attempts as per delay schedule.
  LE_CRED_ERROR_TOO_MANY_ATTEMPTS,
  // Error in hash tree synchronization.
  LE_CRED_ERROR_HASH_TREE,
  // Label provided isn't present in hash tree.
  LE_CRED_ERROR_INVALID_LABEL,
  // No free labels available.
  LE_CRED_ERROR_NO_FREE_LABEL,
  // Invalid metadata in label.
  LE_CRED_ERROR_INVALID_METADATA,
  // Unclassified error.
  LE_CRED_ERROR_UNCLASSIFIED,
  // Credential Manager Locked.
  LE_CRED_ERROR_LE_LOCKED,
  // Unexpected PCR state.
  LE_CRED_ERROR_PCR_NOT_MATCH,
  // Sentinel value.
  LE_CRED_ERROR_MAX,
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_LE_CREDENTIAL_ERROR_H_
