// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/auth_block_utility_impl.h"

#include <stdint.h>

#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <variant>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>
#include <brillo/cryptohome.h>
#include <chromeos/constants/cryptohome.h>
#include <libhwsec-foundation/status/status_chain_or.h>

#include "cryptohome/auth_blocks/async_challenge_credential_auth_block.h"
#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_blocks/auth_block_utils.h"
#include "cryptohome/auth_blocks/challenge_credential_auth_block.h"
#include "cryptohome/auth_blocks/cryptohome_recovery_auth_block.h"
#include "cryptohome/auth_blocks/double_wrapped_compat_auth_block.h"
#include "cryptohome/auth_blocks/fingerprint_auth_block.h"
#include "cryptohome/auth_blocks/pin_weaver_auth_block.h"
#include "cryptohome/auth_blocks/scrypt_auth_block.h"
#include "cryptohome/auth_blocks/sync_to_async_auth_block_adapter.h"
#include "cryptohome/auth_blocks/tpm_bound_to_pcr_auth_block.h"
#include "cryptohome/auth_blocks/tpm_ecc_auth_block.h"
#include "cryptohome/auth_blocks/tpm_not_bound_to_pcr_auth_block.h"
#include "cryptohome/auth_factor/auth_factor_storage_type.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptorecovery/recovery_crypto.h"
#include "cryptohome/cryptorecovery/recovery_crypto_hsm_cbor_serialization.h"
#include "cryptohome/cryptorecovery/recovery_crypto_impl.h"
#include "cryptohome/cryptorecovery/recovery_crypto_util.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/error/utilities.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/scrypt_verifier.h"
#include "cryptohome/smart_card_verifier.h"
#include "cryptohome/vault_keyset.h"

using cryptohome::error::ContainsActionInStack;
using cryptohome::error::CryptohomeCryptoError;
using cryptohome::error::CryptohomeError;
using cryptohome::error::ErrorAction;
using cryptohome::error::ErrorActionSet;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
using hwsec_foundation::status::StatusChain;

namespace cryptohome {

namespace {

// Returns candidate AuthBlock types that can be used for the specified factor.
// The list is ordered from the most preferred options towards the least ones.
// Historically, we have more than 1 auth blocks suitable for password.
// But for more recent auth factors, there is either one exact dedicated auth
// block, or no auth block at all.
std::vector<AuthBlockType> GetAuthBlockPriorityListForCreation(
    const AuthFactorType& auth_factor_type) {
  switch (auth_factor_type) {
    case AuthFactorType::kPassword:
    case AuthFactorType::kKiosk: {
      std::vector<AuthBlockType> password_types = {
          // `kTpmEcc` comes first as the fastest auth block.
          AuthBlockType::kTpmEcc,
          // `kTpmBoundToPcr` and `kTpmNotBoundToPcr` are fallbacks when
          // hardware
          // doesn't support ECC. `kTpmBoundToPcr` comes first of the two, as
          // binding to PCR is preferred (but it's unavailable on some old
          // devices).
          AuthBlockType::kTpmBoundToPcr,
          AuthBlockType::kTpmNotBoundToPcr,
      };
      if (USE_TPM_INSECURE_FALLBACK) {
        // On boards that allow this, use `kScrypt` as the last fallback option
        // when TPM is unavailable. On other boards, we don't list it as a
        // candidate, in order to let the error chain (that's taken from the
        // last candidate on failure) contain relevant information about TPM
        // error.
        password_types.push_back(AuthBlockType::kScrypt);
      }
      return password_types;
    }
    case AuthFactorType::kPin:
      return {AuthBlockType::kPinWeaver};
    case AuthFactorType::kSmartCard:
      return {AuthBlockType::kChallengeCredential};
    case AuthFactorType::kCryptohomeRecovery:
      return {AuthBlockType::kCryptohomeRecovery};
    case AuthFactorType::kFingerprint:
      return {AuthBlockType::kFingerprint};
    case AuthFactorType::kLegacyFingerprint:
    case AuthFactorType::kUnspecified:
      return {};
  }
}

}  // namespace

AuthBlockUtilityImpl::AuthBlockUtilityImpl(
    KeysetManagement* keyset_management,
    Crypto* crypto,
    Platform* platform,
    std::unique_ptr<FingerprintAuthBlockService> fp_service,
    base::RepeatingCallback<BiometricsAuthBlockService*()> bio_service_getter)
    : keyset_management_(keyset_management),
      crypto_(crypto),
      platform_(platform),
      fp_service_(std::move(fp_service)),
      bio_service_getter_(bio_service_getter) {
  DCHECK(keyset_management);
  DCHECK(crypto_);
  DCHECK(platform_);
}

AuthBlockUtilityImpl::~AuthBlockUtilityImpl() = default;

bool AuthBlockUtilityImpl::GetLockedToSingleUser() const {
  return platform_->FileExists(base::FilePath(kLockedToSingleUserFile));
}

bool AuthBlockUtilityImpl::IsAuthFactorSupported(
    AuthFactorType auth_factor_type,
    AuthFactorStorageType auth_factor_storage_type,
    const std::set<AuthFactorType>& configured_factors) const {
  // If a kiosk factor is in use, every other type of factor is disabled. For
  // clarity, do this check up front.
  bool user_has_kiosk = configured_factors.find(AuthFactorType::kKiosk) !=
                        configured_factors.end();
  if (user_has_kiosk && auth_factor_type != AuthFactorType::kKiosk) {
    return false;
  }
  // Now do the type-specific checks. We deliberately use a complete switch
  // statement here with no default and no post-switch return so that building
  // this code will produce an error if you add a new AuthFactorType value
  // without updating it.
  switch (auth_factor_type) {
    case AuthFactorType::kPassword:
      return true;
    case AuthFactorType::kPin:
      return PinWeaverAuthBlock::IsSupported(*crypto_).ok();
    case AuthFactorType::kCryptohomeRecovery:
      return (auth_factor_storage_type ==
              AuthFactorStorageType::kUserSecretStash) &&
             CryptohomeRecoveryAuthBlock::IsSupported(*crypto_).ok();
    case AuthFactorType::kKiosk:
      return configured_factors.empty() || user_has_kiosk;
    case AuthFactorType::kSmartCard:
      return AsyncChallengeCredentialAuthBlock::IsSupported(*crypto_).ok();
    case AuthFactorType::kLegacyFingerprint:
      return false;
    case AuthFactorType::kFingerprint: {
      return (auth_factor_storage_type ==
              AuthFactorStorageType::kUserSecretStash) &&
             bio_service_getter_.Run() &&
             FingerprintAuthBlock::IsSupported(*crypto_).ok();
    }
    case AuthFactorType::kUnspecified:
      return false;
  }
}

bool AuthBlockUtilityImpl::IsPrepareAuthFactorRequired(
    AuthFactorType auth_factor_type) const {
  switch (auth_factor_type) {
    case AuthFactorType::kLegacyFingerprint:
    case AuthFactorType::kFingerprint:
      return true;
    case AuthFactorType::kPassword:
    case AuthFactorType::kPin:
    case AuthFactorType::kCryptohomeRecovery:
    case AuthFactorType::kKiosk:
    case AuthFactorType::kSmartCard:
    case AuthFactorType::kUnspecified:
      return false;
  }
}

bool AuthBlockUtilityImpl::IsVerifyWithAuthFactorSupported(
    AuthIntent auth_intent, AuthFactorType auth_factor_type) const {
  // Legacy Fingerprint + WebAuthn is a special case that supports a lightweight
  // verify.
  if (auth_intent == AuthIntent::kWebAuthn &&
      auth_factor_type == AuthFactorType::kLegacyFingerprint) {
    return true;
  }
  // Verify can only be used with verify-only intents, other than the above
  // special cases.
  if (auth_intent != AuthIntent::kVerifyOnly) {
    return false;
  }
  switch (auth_factor_type) {
    case AuthFactorType::kPassword:
    case AuthFactorType::kLegacyFingerprint:
    case AuthFactorType::kSmartCard:
      return true;
    case AuthFactorType::kPin:
    case AuthFactorType::kCryptohomeRecovery:
    case AuthFactorType::kKiosk:
    case AuthFactorType::kFingerprint:
    case AuthFactorType::kUnspecified:
      return false;
  }
}

std::unique_ptr<CredentialVerifier>
AuthBlockUtilityImpl::CreateCredentialVerifier(
    AuthFactorType auth_factor_type,
    const std::string& auth_factor_label,
    const AuthInput& auth_input) const {
  std::unique_ptr<CredentialVerifier> verifier;
  switch (auth_factor_type) {
    case AuthFactorType::kPassword: {
      if (!auth_input.user_input.has_value()) {
        LOG(ERROR) << "Cannot construct a password verifier without a password";
        return nullptr;
      }
      verifier =
          ScryptVerifier::Create(auth_factor_label, *auth_input.user_input);
      if (!verifier) {
        LOG(ERROR) << "Credential verifier initialization failed.";
        return nullptr;
      }
      break;
    }
    case AuthFactorType::kLegacyFingerprint:
      if (!auth_factor_label.empty()) {
        LOG(ERROR) << "Legacy fingerprint verifiers cannot use labels";
        return nullptr;
      }
      if (!fp_service_) {
        LOG(ERROR) << "Cannot construct a legacy fingerprint verifier, "
                      "FP service not available";
        return nullptr;
      }
      verifier = std::make_unique<FingerprintVerifier>(fp_service_.get());
      break;
    case AuthFactorType::kSmartCard: {
      if (!IsChallengeCredentialReady(auth_input)) {
        return nullptr;
      }
      auto key_challenge_service = key_challenge_service_factory_->New(
          auth_input.challenge_credential_auth_input->dbus_service_name);
      verifier = SmartCardVerifier::Create(
          auth_factor_label,
          auth_input.challenge_credential_auth_input->public_key_spki_der,
          challenge_credentials_helper_, key_challenge_service_factory_);
      if (!verifier) {
        LOG(ERROR) << "Credential verifier initialization failed.";
        return nullptr;
      }
      break;
    }
    case AuthFactorType::kPin:
    case AuthFactorType::kCryptohomeRecovery:
    case AuthFactorType::kKiosk:
    case AuthFactorType::kFingerprint:
    case AuthFactorType::kUnspecified: {
      return nullptr;
    }
  }

  DCHECK_EQ(verifier->auth_factor_label(), auth_factor_label);
  DCHECK_EQ(verifier->auth_factor_type(), auth_factor_type);
  return verifier;
}

void AuthBlockUtilityImpl::PrepareAuthFactorForAuth(
    AuthFactorType auth_factor_type,
    const ObfuscatedUsername& username,
    PreparedAuthFactorToken::Consumer callback) {
  switch (auth_factor_type) {
    case AuthFactorType::kLegacyFingerprint: {
      fp_service_->Start(username, std::move(callback));
      return;
    }
    case AuthFactorType::kFingerprint: {
      // TODO(b/262308692): Not implemented for now.
      CryptohomeStatus status = MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(
              kLocAuthBlockUtilUnimplementedPrepareForAuthFingerprint),
          ErrorActionSet(
              {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kAuth}),
          user_data_auth::CryptohomeErrorCode::
              CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
      std::move(callback).Run(std::move(status));
      return;
    }
    case AuthFactorType::kPassword:
    case AuthFactorType::kPin:
    case AuthFactorType::kCryptohomeRecovery:
    case AuthFactorType::kKiosk:
    case AuthFactorType::kSmartCard:
    case AuthFactorType::kUnspecified: {
      // These factors do not require Prepare.
      CryptohomeStatus status = MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocAuthBlockUtilPrepareInvalidAuthFactorType),
          ErrorActionSet(
              {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kAuth}),
          user_data_auth::CryptohomeErrorCode::
              CRYPTOHOME_ERROR_INVALID_ARGUMENT);
      std::move(callback).Run(std::move(status));
      return;
    }
  }
}

void AuthBlockUtilityImpl::PrepareAuthFactorForAdd(
    AuthFactorType auth_factor_type,
    const ObfuscatedUsername& username,
    PreparedAuthFactorToken::Consumer callback) {
  switch (auth_factor_type) {
    case AuthFactorType::kFingerprint: {
      BiometricsAuthBlockService* bio_service = bio_service_getter_.Run();
      if (!bio_service) {
        CryptohomeStatus status = MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthBlockUtilPrepareForAuthFingerprintNoService),
            ErrorActionSet(
                {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kAuth}),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
        std::move(callback).Run(std::move(status));
        return;
      }
      bio_service->StartEnrollSession(AuthFactorType::kFingerprint, username,
                                      std::move(callback));
      return;
    }
    case AuthFactorType::kLegacyFingerprint:
    case AuthFactorType::kPassword:
    case AuthFactorType::kPin:
    case AuthFactorType::kCryptohomeRecovery:
    case AuthFactorType::kKiosk:
    case AuthFactorType::kSmartCard:
    case AuthFactorType::kUnspecified: {
      // These factors do not require Prepare.
      CryptohomeStatus status = MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(
              kLocAuthBlockUtilPrepareForAddInvalidAuthFactorType),
          ErrorActionSet(
              {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kAuth}),
          user_data_auth::CryptohomeErrorCode::
              CRYPTOHOME_ERROR_INVALID_ARGUMENT);
      std::move(callback).Run(std::move(status));
      return;
    }
  }
}

CryptoStatus AuthBlockUtilityImpl::CreateKeyBlobsWithAuthBlock(
    AuthBlockType auth_block_type,
    const Credentials& credentials,
    const std::optional<brillo::SecureBlob>& reset_secret,
    AuthBlockState& out_state,
    KeyBlobs& out_key_blobs) const {
  CryptoStatusOr<std::unique_ptr<SyncAuthBlock>> auth_block =
      GetAuthBlockWithType(auth_block_type);
  if (!auth_block.ok()) {
    LOG(ERROR) << "Failed to retrieve auth block.";
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(kLocAuthBlockUtilNoAuthBlockInCreateKeyBlobs))
        .Wrap(std::move(auth_block).status());
  }
  ReportCreateAuthBlock(auth_block_type);

  // |reset_secret| is not processed in the AuthBlocks, the value is copied to
  // the |out_key_blobs| directly. |reset_secret| will be added to the
  // |out_key_blobs| in the VaultKeyset, if missing.
  AuthInput user_input = {
      credentials.passkey(),
      /*locked_to_single_user*=*/std::nullopt,
      /*username=*/credentials.username(),
      brillo::cryptohome::home::SanitizeUserName(credentials.username()),
      reset_secret};

  CryptoStatus error =
      auth_block.value()->Create(user_input, &out_state, &out_key_blobs);
  if (!error.ok()) {
    LOG(ERROR) << "Failed to create per credential secret: " << error;
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocAuthBlockUtilCreateFailedInCreateKeyBlobs))
        .Wrap(std::move(error));
  }

  return OkStatus<CryptohomeCryptoError>();
}

void AuthBlockUtilityImpl::CreateKeyBlobsWithAuthBlockAsync(
    AuthBlockType auth_block_type,
    const AuthInput& auth_input,
    AuthBlock::CreateCallback create_callback) {
  CryptoStatusOr<std::unique_ptr<AuthBlock>> auth_block =
      GetAsyncAuthBlockWithType(auth_block_type, auth_input);
  if (!auth_block.ok()) {
    LOG(ERROR) << "Failed to retrieve auth block.";
    std::move(create_callback)
        .Run(MakeStatus<CryptohomeCryptoError>(
                 CRYPTOHOME_ERR_LOC(
                     kLocAuthBlockUtilNoAuthBlockInCreateKeyBlobsAsync))
                 .Wrap(std::move(auth_block).status()),
             nullptr, nullptr);
    return;
  }
  ReportCreateAuthBlock(auth_block_type);

  // This lambda functions to keep the auth_block reference valid until
  // the results are returned through create_callback.
  AuthBlock* auth_block_ptr = auth_block->get();
  auto managed_callback = base::BindOnce(
      [](std::unique_ptr<AuthBlock> owned_auth_block,
         AuthBlock::CreateCallback callback, CryptohomeStatus error,
         std::unique_ptr<KeyBlobs> key_blobs,
         std::unique_ptr<AuthBlockState> auth_block_state) {
        std::move(callback).Run(std::move(error), std::move(key_blobs),
                                std::move(auth_block_state));
      },
      std::move(auth_block.value()), std::move(create_callback));
  auth_block_ptr->Create(auth_input, std::move(managed_callback));
}

CryptoStatus AuthBlockUtilityImpl::DeriveKeyBlobsWithAuthBlock(
    AuthBlockType auth_block_type,
    const Credentials& credentials,
    const AuthBlockState& auth_state,
    KeyBlobs& out_key_blobs) const {
  AuthInput auth_input = {credentials.passkey(),
                          /*locked_to_single_user=*/std::nullopt};

  auth_input.locked_to_single_user = GetLockedToSingleUser();

  CryptoStatusOr<std::unique_ptr<SyncAuthBlock>> auth_block =
      GetAuthBlockWithType(auth_block_type);
  if (!auth_block.ok()) {
    LOG(ERROR) << "Keyset wrapped with unknown method.";
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(kLocAuthBlockUtilNoAuthBlockInDeriveKeyBlobs))
        .Wrap(std::move(auth_block).status());
  }
  ReportDeriveAuthBlock(auth_block_type);

  CryptoStatus error =
      auth_block.value()->Derive(auth_input, auth_state, &out_key_blobs);
  if (error.ok()) {
    return OkStatus<CryptohomeCryptoError>();
  }
  LOG(ERROR) << "Failed to derive per credential secret: " << error;

  // For LE credentials, if deriving the key blobs failed due to too many
  // attempts, set auth_locked=true in the corresponding keyset. Then save it
  // for future callers who can Load it w/o Decrypt'ing to check that flag.
  // When the pin is entered wrong and AuthBlock fails to derive the KeyBlobs
  // it doesn't make it into the VaultKeyset::Decrypt(); so auth_lock should
  // be set here.
  if (ContainsActionInStack(error, error::ErrorAction::kLeLockedOut)) {
    // Get the corresponding encrypted vault keyset for the user and the label
    // to set the auth_locked.
    std::unique_ptr<VaultKeyset> vk = keyset_management_->GetVaultKeyset(
        brillo::cryptohome::home::SanitizeUserName(credentials.username()),
        credentials.key_data().label());

    if (vk == nullptr) {
      LOG(ERROR)
          << "No vault keyset is found on disk for the given label. Cannot "
             "decide on the AuthBlock type without vault keyset metadata.";
      return MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(kLocAuthBlockUtilNoVaultKeysetInDeriveKeyBlobs),
          ErrorActionSet({ErrorAction::kAuth, ErrorAction::kReboot}),
          CryptoError::CE_OTHER_CRYPTO);
    }
    vk->SetAuthLocked(true);
    vk->Save(vk->GetSourceFile());
  }
  return MakeStatus<CryptohomeCryptoError>(
             CRYPTOHOME_ERR_LOC(kLocAuthBlockUtilDeriveFailedInDeriveKeyBlobs))
      .Wrap(std::move(error));
}

void AuthBlockUtilityImpl::DeriveKeyBlobsWithAuthBlockAsync(
    AuthBlockType auth_block_type,
    const AuthInput& auth_input,
    const AuthBlockState& auth_state,
    AuthBlock::DeriveCallback derive_callback) {
  CryptoStatusOr<std::unique_ptr<AuthBlock>> auth_block =
      GetAsyncAuthBlockWithType(auth_block_type, auth_input);
  if (!auth_block.ok()) {
    LOG(ERROR) << "Failed to retrieve auth block.";
    std::move(derive_callback)
        .Run(MakeStatus<CryptohomeCryptoError>(
                 CRYPTOHOME_ERR_LOC(
                     kLocAuthBlockUtilNoAuthBlockInDeriveKeyBlobsAsync))
                 .Wrap(std::move(auth_block).status()),
             nullptr);
    return;
  }
  ReportDeriveAuthBlock(auth_block_type);

  // This lambda functions to keep the auth_block reference valid until
  // the results are returned through derive_callback.
  AuthBlock* auth_block_ptr = auth_block->get();
  auto managed_callback = base::BindOnce(
      [](std::unique_ptr<AuthBlock> owned_auth_block,
         AuthBlock::DeriveCallback callback, CryptohomeStatus error,
         std::unique_ptr<KeyBlobs> key_blobs) {
        std::move(callback).Run(std::move(error), std::move(key_blobs));
      },
      std::move(auth_block.value()), std::move(derive_callback));

  auth_block_ptr->Derive(auth_input, auth_state, std::move(managed_callback));
}

CryptoStatusOr<AuthBlockType> AuthBlockUtilityImpl::GetAuthBlockTypeForCreation(
    const AuthFactorType& auth_factor_type) const {
  // Default status if there are no entries in the returned priority list.
  CryptoStatus status = MakeStatus<CryptohomeCryptoError>(
      CRYPTOHOME_ERR_LOC(kLocAuthBlockUtilEmptyListInGetAuthBlockWithType),
      ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
      CryptoError::CE_OTHER_CRYPTO);
  for (AuthBlockType candidate_type :
       GetAuthBlockPriorityListForCreation(auth_factor_type)) {
    status = IsAuthBlockSupported(candidate_type);
    if (status.ok()) {
      return candidate_type;
    }
  }
  // No suitable block was found. As we need to return only one error, take it
  // from the last attempted candidate (it's likely the most permissive one), if
  // any.
  return MakeStatus<CryptohomeCryptoError>(
             CRYPTOHOME_ERR_LOC(
                 kLocAuthBlockUtilNoSupportedInGetAuthBlockWithType),
             ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}))
      .Wrap(std::move(status));
}

CryptoStatus AuthBlockUtilityImpl::IsAuthBlockSupported(
    AuthBlockType auth_block_type) const {
  switch (auth_block_type) {
    case AuthBlockType::kPinWeaver:
      return PinWeaverAuthBlock::IsSupported(*crypto_);
    case AuthBlockType::kChallengeCredential:
      return AsyncChallengeCredentialAuthBlock::IsSupported(*crypto_);
    case AuthBlockType::kDoubleWrappedCompat:
      return DoubleWrappedCompatAuthBlock::IsSupported(*crypto_);
    case AuthBlockType::kTpmBoundToPcr:
      return TpmBoundToPcrAuthBlock::IsSupported(*crypto_);
    case AuthBlockType::kTpmNotBoundToPcr:
      return TpmNotBoundToPcrAuthBlock::IsSupported(*crypto_);
    case AuthBlockType::kScrypt:
      // `ScryptAuthBlock` has no `IsSupported()` method. This AuthBlock is
      // pruned in factor creation by `GetAuthBlockPriorityListForCreation()`.
      return OkStatus<CryptohomeCryptoError>();
    case AuthBlockType::kCryptohomeRecovery:
      return CryptohomeRecoveryAuthBlock::IsSupported(*crypto_);
    case AuthBlockType::kTpmEcc:
      return TpmEccAuthBlock::IsSupported(*crypto_);
    case AuthBlockType::kFingerprint:
      if (!bio_service_getter_.Run()) {
        return MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthBlockUtilFingerprintNoServiceInIsAuthBlockSupported),
            ErrorActionSet({ErrorAction::kAuth}), CryptoError::CE_OTHER_CRYPTO);
      }
      return FingerprintAuthBlock::IsSupported(*crypto_);
  }
}

CryptoStatusOr<std::unique_ptr<SyncAuthBlock>>
AuthBlockUtilityImpl::GetAuthBlockWithType(
    const AuthBlockType& auth_block_type) const {
  if (auto status = IsAuthBlockSupported(auth_block_type); !status.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocAuthBlockUtilNotSupportedInGetAuthBlockWithType))
        .Wrap(std::move(status));
  }

  switch (auth_block_type) {
    case AuthBlockType::kPinWeaver:
      return std::make_unique<PinWeaverAuthBlock>(crypto_->le_manager());

    case AuthBlockType::kChallengeCredential:
      return std::make_unique<ChallengeCredentialAuthBlock>();

    case AuthBlockType::kDoubleWrappedCompat:
      return std::make_unique<DoubleWrappedCompatAuthBlock>(
          crypto_->GetHwsec(), crypto_->cryptohome_keys_manager());

    case AuthBlockType::kTpmEcc:
      return std::make_unique<TpmEccAuthBlock>(
          crypto_->GetHwsec(), crypto_->cryptohome_keys_manager());

    case AuthBlockType::kTpmBoundToPcr:
      return std::make_unique<TpmBoundToPcrAuthBlock>(
          crypto_->GetHwsec(), crypto_->cryptohome_keys_manager());

    case AuthBlockType::kTpmNotBoundToPcr:
      return std::make_unique<TpmNotBoundToPcrAuthBlock>(
          crypto_->GetHwsec(), crypto_->cryptohome_keys_manager());

    case AuthBlockType::kScrypt:
      return std::make_unique<ScryptAuthBlock>();

    case AuthBlockType::kCryptohomeRecovery:
      return std::make_unique<CryptohomeRecoveryAuthBlock>(
          crypto_->GetHwsec(), crypto_->GetRecoveryCrypto(),
          crypto_->le_manager(), platform_);

    // Synchronous FingerprintAuthBlock isn't supported.
    case AuthBlockType::kFingerprint:
      LOG(ERROR) << "Unsupported AuthBlockType.";

      return MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(
              kLocAuthBlockUtilMaxValueUnsupportedInGetAuthBlockWithType),
          ErrorActionSet(
              {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kAuth}),
          CryptoError::CE_OTHER_CRYPTO);
  }
  return MakeStatus<CryptohomeCryptoError>(
      CRYPTOHOME_ERR_LOC(
          kLocAuthBlockUtilUnknownUnsupportedInGetAuthBlockWithType),
      ErrorActionSet(
          {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kAuth}),
      CryptoError::CE_OTHER_CRYPTO);
}

CryptoStatusOr<std::unique_ptr<AuthBlock>>
AuthBlockUtilityImpl::GetAsyncAuthBlockWithType(
    const AuthBlockType& auth_block_type, const AuthInput& auth_input) {
  if (auto status = IsAuthBlockSupported(auth_block_type); !status.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocAuthBlockUtilNotSupportedInGetAsyncAuthBlockWithType))
        .Wrap(std::move(status));
  }

  switch (auth_block_type) {
    case AuthBlockType::kPinWeaver:
      return std::make_unique<SyncToAsyncAuthBlockAdapter>(
          std::make_unique<PinWeaverAuthBlock>(crypto_->le_manager()));

    case AuthBlockType::kChallengeCredential:
      if (IsChallengeCredentialReady(auth_input)) {
        auto key_challenge_service = key_challenge_service_factory_->New(
            auth_input.challenge_credential_auth_input->dbus_service_name);
        return std::make_unique<AsyncChallengeCredentialAuthBlock>(
            challenge_credentials_helper_, std::move(key_challenge_service),
            auth_input.username);
      }
      LOG(ERROR) << "No valid ChallengeCredentialsHelper, "
                    "KeyChallengeService, or account id in AuthBlockUtility";
      return MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(
              kLocAuthBlockUtilNoChalInGetAsyncAuthBlockWithType),
          ErrorActionSet(
              {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kAuth}),
          CryptoError::CE_OTHER_CRYPTO);

    case AuthBlockType::kDoubleWrappedCompat:
      return std::make_unique<SyncToAsyncAuthBlockAdapter>(
          std::make_unique<DoubleWrappedCompatAuthBlock>(
              crypto_->GetHwsec(), crypto_->cryptohome_keys_manager()));

    case AuthBlockType::kTpmEcc:
      return std::make_unique<SyncToAsyncAuthBlockAdapter>(
          std::make_unique<TpmEccAuthBlock>(
              crypto_->GetHwsec(), crypto_->cryptohome_keys_manager()));

    case AuthBlockType::kTpmBoundToPcr:
      return std::make_unique<SyncToAsyncAuthBlockAdapter>(
          std::make_unique<TpmBoundToPcrAuthBlock>(
              crypto_->GetHwsec(), crypto_->cryptohome_keys_manager()));

    case AuthBlockType::kTpmNotBoundToPcr:
      return std::make_unique<SyncToAsyncAuthBlockAdapter>(
          std::make_unique<TpmNotBoundToPcrAuthBlock>(
              crypto_->GetHwsec(), crypto_->cryptohome_keys_manager()));

    case AuthBlockType::kScrypt:
      return std::make_unique<SyncToAsyncAuthBlockAdapter>(
          std::make_unique<ScryptAuthBlock>());

    case AuthBlockType::kCryptohomeRecovery:
      return std::make_unique<SyncToAsyncAuthBlockAdapter>(
          std::make_unique<CryptohomeRecoveryAuthBlock>(
              crypto_->GetHwsec(), crypto_->GetRecoveryCrypto(),
              crypto_->le_manager(), platform_));

    case AuthBlockType::kFingerprint: {
      BiometricsAuthBlockService* bio_service = bio_service_getter_.Run();
      if (!bio_service) {
        return MakeStatus<CryptohomeCryptoError>(
            CRYPTOHOME_ERR_LOC(
                kLocAuthBlockUtilFingerprintNoServiceInGetAsyncAuthBlock),
            ErrorActionSet(
                {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kAuth}),
            CryptoError::CE_OTHER_CRYPTO);
      }
      return std::make_unique<FingerprintAuthBlock>(crypto_->le_manager(),
                                                    bio_service);
    }
  }
  return MakeStatus<CryptohomeCryptoError>(
      CRYPTOHOME_ERR_LOC(
          kLocAuthBlockUtilUnknownUnsupportedInGetAsyncAuthBlockWithType),
      ErrorActionSet(
          {ErrorAction::kDevCheckUnexpectedState, ErrorAction::kAuth}),
      CryptoError::CE_OTHER_CRYPTO);
}

void AuthBlockUtilityImpl::InitializeChallengeCredentialsHelper(
    ChallengeCredentialsHelper* challenge_credentials_helper,
    KeyChallengeServiceFactory* key_challenge_service_factory) {
  if (!challenge_credentials_helper_) {
    challenge_credentials_helper_ = challenge_credentials_helper;
  } else {
    LOG(WARNING) << "ChallengeCredentialsHelper already initialized in "
                    "AuthBlockUtility.";
  }
  if (!key_challenge_service_factory_) {
    key_challenge_service_factory_ = key_challenge_service_factory;
  } else {
    LOG(WARNING) << "KeyChallengeServiceFactory already initialized in "
                    "AuthBlockUtility.";
  }
}

bool AuthBlockUtilityImpl::IsChallengeCredentialReady(
    const AuthInput& auth_input) const {
  return (
      challenge_credentials_helper_ != nullptr &&
      key_challenge_service_factory_ != nullptr &&
      auth_input.challenge_credential_auth_input &&
      !auth_input.challenge_credential_auth_input->dbus_service_name.empty());
}

bool AuthBlockUtilityImpl::GetAuthBlockStateFromVaultKeyset(
    const std::string& label,
    const ObfuscatedUsername& obfuscated_username,
    AuthBlockState& out_state) const {
  std::unique_ptr<VaultKeyset> vault_keyset =
      keyset_management_->GetVaultKeyset(obfuscated_username, label);
  // If there is no keyset on the disk for the given user and label (or for the
  // empty label as a wildcard), AuthBlock state cannot be obtained.
  if (vault_keyset == nullptr) {
    LOG(ERROR)
        << "No vault keyset is found on disk for the given label. Cannot "
           "obtain AuthBlockState without vault keyset metadata.";
    return false;
  }

  return GetAuthBlockState(*vault_keyset, out_state);
}

void AuthBlockUtilityImpl::AssignAuthBlockStateToVaultKeyset(
    const AuthBlockState& auth_state, VaultKeyset& vault_keyset) const {
  if (const auto* state =
          std::get_if<TpmNotBoundToPcrAuthBlockState>(&auth_state.state)) {
    vault_keyset.SetTpmNotBoundToPcrState(*state);
  } else if (const auto* state =
                 std::get_if<TpmBoundToPcrAuthBlockState>(&auth_state.state)) {
    vault_keyset.SetTpmBoundToPcrState(*state);
  } else if (const auto* state =
                 std::get_if<PinWeaverAuthBlockState>(&auth_state.state)) {
    vault_keyset.SetPinWeaverState(*state);
  } else if (const auto* state =
                 std::get_if<ScryptAuthBlockState>(&auth_state.state)) {
    vault_keyset.SetScryptState(*state);
  } else if (const auto* state = std::get_if<ChallengeCredentialAuthBlockState>(
                 &auth_state.state)) {
    vault_keyset.SetChallengeCredentialState(*state);
  } else if (const auto* state =
                 std::get_if<TpmEccAuthBlockState>(&auth_state.state)) {
    vault_keyset.SetTpmEccState(*state);
  } else {
    LOG(ERROR) << "Invalid auth block state type";
    return;
  }
}

std::optional<AuthBlockType> AuthBlockUtilityImpl::GetAuthBlockTypeFromState(
    const AuthBlockState& auth_block_state) const {
  std::optional<AuthBlockType> auth_block_type;
  if (const auto* state = std::get_if<TpmNotBoundToPcrAuthBlockState>(
          &auth_block_state.state)) {
    auth_block_type = AuthBlockType::kTpmNotBoundToPcr;
  } else if (const auto* state = std::get_if<TpmBoundToPcrAuthBlockState>(
                 &auth_block_state.state)) {
    auth_block_type = AuthBlockType::kTpmBoundToPcr;
  } else if (const auto* state = std::get_if<PinWeaverAuthBlockState>(
                 &auth_block_state.state)) {
    auth_block_type = AuthBlockType::kPinWeaver;
  } else if (const auto* state =
                 std::get_if<ScryptAuthBlockState>(&auth_block_state.state)) {
    auth_block_type = AuthBlockType::kScrypt;
  } else if (const auto* state =
                 std::get_if<TpmEccAuthBlockState>(&auth_block_state.state)) {
    auth_block_type = AuthBlockType::kTpmEcc;
  } else if (const auto& state = std::get_if<ChallengeCredentialAuthBlockState>(
                 &auth_block_state.state)) {
    auth_block_type = AuthBlockType::kChallengeCredential;
  } else if (const auto& state = std::get_if<CryptohomeRecoveryAuthBlockState>(
                 &auth_block_state.state)) {
    auth_block_type = AuthBlockType::kCryptohomeRecovery;
  } else if (const auto& state = std::get_if<FingerprintAuthBlockState>(
                 &auth_block_state.state)) {
    auth_block_type = AuthBlockType::kFingerprint;
  }

  return auth_block_type;
}

base::flat_set<AuthIntent> AuthBlockUtilityImpl::GetSupportedIntentsFromState(
    const AuthBlockState& auth_block_state) const {
  // The supported intents. Defaults to all of them.
  base::flat_set<AuthIntent> supported_intents = {
      AuthIntent::kDecrypt, AuthIntent::kVerifyOnly, AuthIntent::kWebAuthn};

  std::optional<AuthBlockType> auth_block_type =
      GetAuthBlockTypeFromState(auth_block_state);

  // Invalid block types support nothing.
  if (!auth_block_type) {
    supported_intents.clear();
    return supported_intents;
  }

  // Non-Pinweaver based AuthFactors are assumed to support all AuthIntents by
  // default.
  if (*auth_block_type != AuthBlockType::kPinWeaver) {
    return supported_intents;
  }

  auto* state = std::get_if<PinWeaverAuthBlockState>(&auth_block_state.state);
  if (!state) {
    supported_intents.clear();
    return supported_intents;
  }
  // Ensure that the AuthFactor has le_label.
  if (!state->le_label.has_value()) {
    LOG(ERROR) << "PinWeaver AuthBlockState does not have le_label";
    supported_intents.clear();
    return supported_intents;
  }
  // Check with PinWeaver and fill the appropriate value.
  if (!crypto_->le_manager()) {
    LOG(ERROR) << "Crypto object does not have a valid LE manager";
    supported_intents.clear();
    return supported_intents;
  }
  if (!crypto_->cryptohome_keys_manager()) {
    LOG(ERROR) << "Crypto object does not have a valid keys manager";
    supported_intents.clear();
    return supported_intents;
  }

  PinWeaverAuthBlock pinweaver_auth_block =
      PinWeaverAuthBlock(crypto_->le_manager());
  if (pinweaver_auth_block.IsLocked(state->le_label.value())) {
    supported_intents.clear();
  }

  return supported_intents;
}

CryptohomeStatus AuthBlockUtilityImpl::PrepareAuthBlockForRemoval(
    const AuthBlockState& auth_block_state) {
  std::optional<AuthBlockType> auth_block_type =
      GetAuthBlockTypeFromState(auth_block_state);
  if (!auth_block_type) {
    LOG(ERROR) << "Unsupported auth factor type.";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthBlockUtilUnsupportedInPrepareAuthBlockForRemoval),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  // Should not create ChallengeCredential AuthBlock, no underlying
  // removal of the AuthBlock needed. Because of this, auth_input
  // can be an empty input.
  if (auth_block_type == AuthBlockType::kChallengeCredential) {
    return OkStatus<CryptohomeError>();
  }

  AuthInput auth_input;
  CryptoStatusOr<std::unique_ptr<AuthBlock>> auth_block =
      GetAsyncAuthBlockWithType(*auth_block_type, auth_input);
  if (!auth_block.ok()) {
    LOG(ERROR) << "Failed to retrieve auth block.";
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocAuthBlockUtilNoAsyncAuthBlockInPrepareForRemoval))
        .Wrap(std::move(auth_block).status());
  }

  return auth_block.value()->PrepareForRemoval(auth_block_state);
}

CryptoStatus AuthBlockUtilityImpl::GenerateRecoveryRequest(
    const ObfuscatedUsername& obfuscated_username,
    const cryptorecovery::RequestMetadata& request_metadata,
    const brillo::Blob& epoch_response,
    const CryptohomeRecoveryAuthBlockState& state,
    hwsec::RecoveryCryptoFrontend* recovery_hwsec,
    brillo::SecureBlob* out_recovery_request,
    brillo::SecureBlob* out_ephemeral_pub_key) const {
  // Check if the required fields are set on CryptohomeRecoveryAuthBlockState.
  if (state.hsm_payload.empty() || state.channel_pub_key.empty() ||
      state.encrypted_channel_priv_key.empty()) {
    LOG(ERROR) << "CryptohomeRecoveryAuthBlockState is invalid";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocAuthBlockStateInvalidInGenerateRecoveryRequest),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  // Deserialize HSM payload from CryptohomeRecoveryAuthBlockState.
  cryptorecovery::HsmPayload hsm_payload;
  if (!cryptorecovery::DeserializeHsmPayloadFromCbor(state.hsm_payload,
                                                     &hsm_payload)) {
    LOG(ERROR) << "Failed to deserialize HSM payload";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocFailedDeserializeHsmPayloadInGenerateRecoveryRequest),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  // Parse epoch response, which is sent from Chrome, to proto.
  cryptorecovery::CryptoRecoveryEpochResponse epoch_response_proto;
  if (!epoch_response_proto.ParseFromString(
          brillo::BlobToString(epoch_response))) {
    LOG(ERROR) << "Failed to parse epoch response";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocFailedParseEpochResponseInGenerateRecoveryRequest),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  if (!recovery_hwsec) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocFailedToGetRecoveryCryptoBackendInGenerateRecoveryRequest),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  std::unique_ptr<cryptorecovery::RecoveryCryptoImpl> recovery =
      cryptorecovery::RecoveryCryptoImpl::Create(recovery_hwsec, platform_);

  // Generate recovery request proto which will be sent back to Chrome, and then
  // to the recovery server.
  cryptorecovery::GenerateRecoveryRequestRequest
      generate_recovery_request_input_param(
          {.hsm_payload = hsm_payload,
           .request_meta_data = request_metadata,
           .epoch_response = epoch_response_proto,
           .encrypted_rsa_priv_key = state.encrypted_rsa_priv_key,
           .encrypted_channel_priv_key = state.encrypted_channel_priv_key,
           .channel_pub_key = state.channel_pub_key,
           .obfuscated_username = obfuscated_username});
  cryptorecovery::CryptoRecoveryRpcRequest recovery_request;
  if (!recovery->GenerateRecoveryRequest(generate_recovery_request_input_param,
                                         &recovery_request,
                                         out_ephemeral_pub_key)) {
    LOG(ERROR) << "Call to GenerateRecoveryRequest failed";
    // TODO(b/231297066): send more specific error.
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocFailedGenerateRecoveryRequest),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  // Serialize recovery request proto.
  *out_recovery_request =
      brillo::SecureBlob(recovery_request.SerializeAsString());
  return OkStatus<CryptohomeCryptoError>();
}

}  // namespace cryptohome
