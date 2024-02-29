// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/fp_migration/utility.h"

#include <string>
#include <utility>

#include <libhwsec/frontend/cryptohome/frontend.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/status/status_chain.h>
#include <libhwsec-foundation/status/status_chain_macros.h>

#include "cryptohome/error/action.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/error/cryptohome_tpm_error.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/error/locations.h"

namespace cryptohome {
namespace {

using ::cryptohome::error::CryptohomeCryptoError;
using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::CryptohomeTPMError;
using ::cryptohome::error::ErrorActionSet;
using ::cryptohome::error::PossibleAction;
using ::hwsec::PinWeaverManagerFrontend;
using ::hwsec::PinWeaverManagerFrontend::AuthChannel::kFingerprintAuthChannel;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;

}  // namespace

// static
std::string FpMigrationUtility::MigratedLegacyFpLabel(size_t index) {
  return base::StringPrintf("legacy-fp-%zu", index);
}

void FpMigrationUtility::PrepareLegacyTemplate(const AuthInput& auth_input,
                                               StatusCallback callback) {
  if (!bio_service_) {
    std::move(callback).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocFpMigrationPrepareLegacyTemplateNoService),
        ErrorActionSet({PossibleAction::kRetry}),
        user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL));
    return;
  }

  bio_service_->GetNonce(base::BindOnce(
      &FpMigrationUtility::EnrollLegacyTemplate, weak_factory_.GetWeakPtr(),
      std::move(callback), auth_input));
}

void FpMigrationUtility::EnrollLegacyTemplate(
    StatusCallback callback,
    const AuthInput& auth_input,
    std::optional<brillo::Blob> nonce) {
  if (!auth_input.rate_limiter_label.has_value() ||
      !auth_input.fingerprint_auth_input.has_value() ||
      !auth_input.fingerprint_auth_input->legacy_record_id.has_value()) {
    std::move(callback).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocFpMigrationPrepareTemplateBadAuthInput),
        ErrorActionSet({PossibleAction::kRetry}),
        user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL));
    return;
  }
  if (!nonce.has_value()) {
    std::move(callback).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocFpMigrationGetNonceFailed),
        ErrorActionSet({PossibleAction::kRetry}),
        user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL));
    return;
  }

  hwsec::StatusOr<PinWeaverManagerFrontend::StartBiometricsAuthReply> reply =
      crypto_->GetPinWeaverManager()->StartBiometricsAuth(
          kFingerprintAuthChannel, *auth_input.rate_limiter_label,
          std::move(*nonce));
  if (!reply.ok()) {
    std::move(callback).Run(
        MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(kLocFpMigrationStartBioAuthFailed))
            .Wrap(
                MakeStatus<CryptohomeTPMError>(std::move(reply).err_status())));
    return;
  }
  BiometricsAuthBlockService::OperationInput input{
      .nonce = std::move(reply->server_nonce),
      .encrypted_label_seed = std::move(reply->encrypted_he_secret),
      .iv = std::move(reply->iv),
  };
  bio_service_->EnrollLegacyTemplate(
      AuthFactorType::kFingerprint,
      *auth_input.fingerprint_auth_input->legacy_record_id, std::move(input),
      std::move(callback));
}

void FpMigrationUtility::ListLegacyRecords(LegacyRecordsCallback callback) {
  if (!bio_service_) {
    std::move(callback).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocFpMigrationListLegacyRecordsNoService),
        ErrorActionSet({PossibleAction::kRetry}),
        user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL));
    return;
  }

  bio_service_->ListLegacyRecords(std::move(callback));
}

}  // namespace cryptohome
