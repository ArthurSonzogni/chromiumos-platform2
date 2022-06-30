// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm1/backend.h"

using hwsec_foundation::status::MakeStatus;

namespace hwsec {

using PinWeaverTpm1 = BackendTpm1::PinWeaverTpm1;

StatusOr<bool> PinWeaverTpm1::IsEnabled() {
  return false;
}

StatusOr<uint8_t> PinWeaverTpm1::GetVersion() {
  return MakeStatus<TPMError>("Unsupported", TPMRetryAction::kNoRetry);
}

StatusOr<PinWeaverTpm1::CredentialTreeResult> PinWeaverTpm1::Reset(
    uint32_t bits_per_level, uint32_t length_labels) {
  return MakeStatus<TPMError>("Unsupported", TPMRetryAction::kNoRetry);
}

StatusOr<PinWeaverTpm1::CredentialTreeResult> PinWeaverTpm1::InsertCredential(
    const std::vector<OperationPolicySetting>& policies,
    const uint64_t label,
    const std::vector<brillo::Blob>& h_aux,
    const brillo::SecureBlob& le_secret,
    const brillo::SecureBlob& he_secret,
    const brillo::SecureBlob& reset_secret,
    const DelaySchedule& delay_schedule) {
  return MakeStatus<TPMError>("Unsupported", TPMRetryAction::kNoRetry);
}

StatusOr<PinWeaverTpm1::CredentialTreeResult> PinWeaverTpm1::CheckCredential(
    const uint64_t label,
    const std::vector<brillo::Blob>& h_aux,
    const brillo::Blob& orig_cred_metadata,
    const brillo::SecureBlob& le_secret) {
  return MakeStatus<TPMError>("Unsupported", TPMRetryAction::kNoRetry);
}

StatusOr<PinWeaverTpm1::CredentialTreeResult> PinWeaverTpm1::RemoveCredential(
    const uint64_t label,
    const std::vector<std::vector<uint8_t>>& h_aux,
    const std::vector<uint8_t>& mac) {
  return MakeStatus<TPMError>("Unsupported", TPMRetryAction::kNoRetry);
}

StatusOr<PinWeaverTpm1::CredentialTreeResult> PinWeaverTpm1::ResetCredential(
    const uint64_t label,
    const std::vector<std::vector<uint8_t>>& h_aux,
    const std::vector<uint8_t>& orig_cred_metadata,
    const brillo::SecureBlob& reset_secret) {
  return MakeStatus<TPMError>("Unsupported", TPMRetryAction::kNoRetry);
}

StatusOr<PinWeaverTpm1::GetLogResult> PinWeaverTpm1::GetLog(
    const brillo::Blob& cur_disk_root_hash) {
  return MakeStatus<TPMError>("Unsupported", TPMRetryAction::kNoRetry);
}

StatusOr<PinWeaverTpm1::ReplayLogOperationResult>
PinWeaverTpm1::ReplayLogOperation(const brillo::Blob& log_entry_root,
                                  const std::vector<brillo::Blob>& h_aux,
                                  const brillo::Blob& orig_cred_metadata) {
  return MakeStatus<TPMError>("Unsupported", TPMRetryAction::kNoRetry);
}

StatusOr<int> PinWeaverTpm1::GetWrongAuthAttempts(
    const brillo::Blob& cred_metadata) {
  return MakeStatus<TPMError>("Unsupported", TPMRetryAction::kNoRetry);
}

StatusOr<PinWeaverTpm1::DelaySchedule> PinWeaverTpm1::GetDelaySchedule(
    const brillo::Blob& cred_metadata) {
  return MakeStatus<TPMError>("Unsupported", TPMRetryAction::kNoRetry);
}

StatusOr<uint32_t> PinWeaverTpm1::GetDelayInSeconds(
    const brillo::Blob& cred_metadata) {
  return MakeStatus<TPMError>("Unsupported", TPMRetryAction::kNoRetry);
}
}  // namespace hwsec
