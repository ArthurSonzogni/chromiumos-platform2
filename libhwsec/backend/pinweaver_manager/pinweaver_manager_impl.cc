// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/pinweaver_manager/pinweaver_manager_impl.h"

#include <utility>

#include <base/files/file_util.h>
#include <base/notreached.h>
#include <libhwsec-foundation/status/status_chain.h>
#include <libhwsec-foundation/status/status_chain_macros.h>

#include "libhwsec/backend/pinweaver.h"
#include "libhwsec/error/pinweaver_error.h"
#include "libhwsec/error/tpm_error.h"
#include "libhwsec/error/tpm_retry_action.h"
#include "libhwsec/status.h"

namespace {

// Constants used to define the hash tree.
constexpr uint32_t kLengthLabels = 14;
constexpr uint32_t kBitsPerLevel = 2;

}  // namespace

using hwsec_foundation::status::OkStatus;
using CredentialTreeResult = hwsec::PinWeaver::CredentialTreeResult;
using GetLogResult = hwsec::PinWeaver::GetLogResult;
using hwsec_foundation::status::MakeStatus;

namespace hwsec {

Status LECredentialManagerImpl::StateIsReady() {
  if (is_initialized_) {
    if (!hash_tree_->IsValid()) {
      return MakeStatus<TPMError>("Invalid hash tree",
                                  TPMRetryAction::kNoRetry);
    }
    if (is_locked_) {
      return MakeStatus<TPMError>(
          "PinWeaver Manager locked due to"
          "previous failing disk update",
          TPMRetryAction::kReboot);
    }

    return OkStatus();
  }

  ASSIGN_OR_RETURN(bool is_enabled, pinweaver_.IsEnabled());
  if (!is_enabled) {
    return MakeStatus<TPMError>("Pinweaver Unsupported",
                                TPMRetryAction::kNoRetry);
  }

  // Check if hash tree already exists.
  bool new_hash_tree = !base::PathExists(basedir_);

  hash_tree_ =
      std::make_unique<SignInHashTree>(kLengthLabels, kBitsPerLevel, basedir_);
  if (!hash_tree_->IsValid()) {
    return MakeStatus<TPMError>(
        "Failed to initialize LE credential manager:"
        "invalid hash tree",
        TPMRetryAction::kNoRetry);
  }

  // Reset the root hash in the TPM to its initial value.
  if (new_hash_tree) {
    ASSIGN_OR_RETURN(const CredentialTreeResult& result,
                     pinweaver_.Reset(kBitsPerLevel, kLengthLabels));
    root_hash_ = result.new_root;
    hash_tree_->GenerateAndStoreHashCache();
  } else {
    // Since we're using mmap leaf cache, we don't need to read all leaf data
    // from PLT(disk). Here we still need to generates the inner hash array
    // (since it's not stored on disk).
    hash_tree_->GenerateInnerHashArray();
  }

  is_initialized_ = true;
  return OkStatus();
}

StatusOr<uint64_t> LECredentialManagerImpl::InsertCredential(
    const std::vector<hwsec::OperationPolicySetting>& policies,
    const brillo::SecureBlob& le_secret,
    const brillo::SecureBlob& he_secret,
    const brillo::SecureBlob& reset_secret,
    const LECredentialManager::DelaySchedule& delay_sched,
    std::optional<uint32_t> expiration_delay) {
  RETURN_IF_ERROR(StateIsReady());
  NOTREACHED_NORETURN();
}

StatusOr<LECredentialManagerImpl::CheckCredentialReply>
LECredentialManagerImpl::CheckCredential(uint64_t label,
                                         const brillo::SecureBlob& le_secret) {
  RETURN_IF_ERROR(StateIsReady());
  NOTREACHED_NORETURN();
}

Status LECredentialManagerImpl::ResetCredential(
    uint64_t label,
    const brillo::SecureBlob& reset_secret,
    ResetType reset_type) {
  RETURN_IF_ERROR(StateIsReady());
  NOTREACHED_NORETURN();
}

Status LECredentialManagerImpl::RemoveCredential(uint64_t label) {
  RETURN_IF_ERROR(StateIsReady());
  NOTREACHED_NORETURN();
}

StatusOr<uint32_t> LECredentialManagerImpl::GetWrongAuthAttempts(
    uint64_t label) {
  RETURN_IF_ERROR(StateIsReady());
  NOTREACHED_NORETURN();
}

StatusOr<uint32_t> LECredentialManagerImpl::GetDelayInSeconds(uint64_t label) {
  RETURN_IF_ERROR(StateIsReady());
  NOTREACHED_NORETURN();
}

StatusOr<std::optional<uint32_t>>
LECredentialManagerImpl::GetExpirationInSeconds(uint64_t label) {
  RETURN_IF_ERROR(StateIsReady());
  NOTREACHED_NORETURN();
}

StatusOr<LECredentialManagerImpl::DelaySchedule>
LECredentialManagerImpl::GetDelaySchedule(uint64_t label) {
  RETURN_IF_ERROR(StateIsReady());
  NOTREACHED_NORETURN();
}

StatusOr<uint64_t> LECredentialManagerImpl::InsertRateLimiter(
    uint8_t auth_channel,
    const std::vector<hwsec::OperationPolicySetting>& policies,
    const brillo::SecureBlob& reset_secret,
    const DelaySchedule& delay_sched,
    std::optional<uint32_t> expiration_delay) {
  RETURN_IF_ERROR(StateIsReady());
  NOTREACHED_NORETURN();
}

StatusOr<LECredentialManagerImpl::StartBiometricsAuthReply>
LECredentialManagerImpl::StartBiometricsAuth(uint8_t auth_channel,
                                             uint64_t label,
                                             const brillo::Blob& client_nonce) {
  RETURN_IF_ERROR(StateIsReady());
  NOTREACHED_NORETURN();
}

}  // namespace hwsec
