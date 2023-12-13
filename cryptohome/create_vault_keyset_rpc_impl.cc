// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/create_vault_keyset_rpc_impl.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/strings/string_number_conversions.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/hmac.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/status/status_chain_or.h>

#include "cryptohome/auth_factor/protobuf.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/signature_sealing/structures_proto.h"

using cryptohome::error::CryptohomeCryptoError;
using cryptohome::error::CryptohomeError;
using cryptohome::error::ErrorActionSet;
using cryptohome::error::PossibleAction;
using hwsec_foundation::CreateRandomBlob;
using hwsec_foundation::CreateSecureRandomBlob;
using hwsec_foundation::HmacSha256Kdf;
using hwsec_foundation::kAesBlockSize;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;

namespace cryptohome {

namespace {
constexpr uint32_t kTpm12Family = 0x312E3200;
}  // namespace

CreateVaultKeysetRpcImpl::CreateVaultKeysetRpcImpl(
    KeysetManagement* keyset_management,
    const hwsec::CryptohomeFrontend* hwsec,
    AuthBlockUtility* auth_block_utility,
    AuthFactorDriverManager* auth_factor_driver_manager)
    : keyset_management_(keyset_management),
      hwsec_(hwsec),
      auth_block_utility_(auth_block_utility),
      auth_factor_driver_manager_(auth_factor_driver_manager) {}

bool CreateVaultKeysetRpcImpl::ClearKeyDataFromInitialKeyset(
    const ObfuscatedUsername& obfuscated_username, bool disable_key_data) {
  // Remove KeyBlobs from the VaultKeyset and resave, as the
  // keyset_management flags need a valid KeyBlobs to operate.
  // Used only for the testing of legacy keysets which were created
  // KeyBlobs was not a concept.
  if (disable_key_data) {
    // Load the freshly created VaultKeyset.
    std::unique_ptr<VaultKeyset> created_vk =
        keyset_management_->GetVaultKeyset(obfuscated_username,
                                           initial_vault_keyset_->GetLabel());
    if (created_vk) {
      created_vk->ClearKeyData();
      if (!created_vk->Save(created_vk->GetSourceFile())) {
        LOG(ERROR) << "Failed to clear key blobs from the vault_keyset.";
        return false;
      }
    }
  }

  return true;
}

void CreateVaultKeysetRpcImpl::CreateVaultKeyset(
    const user_data_auth::CreateVaultKeysetRequest& request,
    AuthSession& auth_session,
    StatusCallback on_done) {
  // Preconditions:
  CHECK_EQ(request.auth_session_id(), auth_session.serialized_token());
  // At this point AuthSession should be authenticated as it needs
  // FileSystemKeys to wrap the new credentials.
  if (!auth_session.authorized_intents().contains(AuthIntent::kDecrypt)) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocCreateVaultKeysetRpcImplUnauthedInCreateVaultKeyset),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_UNAUTHENTICATED_AUTH_SESSION));
    return;
  }

  // Create and initialize AuthFactorType.
  std::optional<AuthFactorType> type = AuthFactorTypeFromProto(request.type());
  if (!type) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocCreateVaultKeysetRpcImplNoAuthFactorType),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
    return;
  }

  // Determine the AuthBlock type to use for auth block creation.
  const AuthFactorDriver& factor_driver =
      auth_factor_driver_manager_->GetDriver(*type);
  CryptoStatusOr<AuthBlockType> auth_block_type =
      auth_block_utility_->SelectAuthBlockTypeForCreation(
          factor_driver.block_types());
  if (!auth_block_type.ok()) {
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocCreateVaultKeysetRpcImplyInvalidBlockType),
            user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE)
            .Wrap(std::move(auth_block_type).status()));
    return;
  }

  // Create and initialize AuthInput.
  AuthInput auth_input = {
      .user_input = brillo::SecureBlob(request.passkey()),
      .locked_to_single_user = auth_block_utility_->GetLockedToSingleUser(),
      .username = auth_session.username(),
      .obfuscated_username = auth_session.obfuscated_username()};
  AuthFactorMetadata auth_factor_metadata;

  // Generate the reset seed for AuthInput.
  if (factor_driver.NeedsResetSecret()) {
    // When using VaultKeyset, reset is implemented via a seed that's shared
    // among all of the user's VKs. Hence copy it from the previously loaded VK.
    if (!initial_vault_keyset_) {
      std::move(on_done).Run(MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocCreateVaultKeysetRpcImplNoInitialVaultKeyset),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
          user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE));
    }
    auth_input.reset_seed = initial_vault_keyset_->GetResetSeed();
    auth_input.reset_salt = CreateRandomBlob(kAesBlockSize);
    auth_input.reset_secret = HmacSha256Kdf(auth_input.reset_salt.value(),
                                            auth_input.reset_seed.value());
    LOG(INFO) << "Reset seed, to generate the reset_secret for the test PIN "
                 "VaultKeyset, "
                 "is obtained from password VaultKeyset with label: "
              << initial_vault_keyset_->GetLabel();
  }

  KeyData key_data;
  key_data.set_label(request.key_label());
  switch (*type) {
    case AuthFactorType::kPassword:
      key_data.set_type(KeyData::KEY_TYPE_PASSWORD);
      break;
    case AuthFactorType::kPin:
      key_data.set_type(KeyData::KEY_TYPE_PASSWORD);
      key_data.mutable_policy()->set_low_entropy_credential(true);
      break;
    case AuthFactorType::kKiosk:
      key_data.set_type(KeyData::KEY_TYPE_KIOSK);
      break;
    case AuthFactorType::kSmartCard: {
      key_data.set_type(KeyData::KEY_TYPE_CHALLENGE_RESPONSE);

      // Assign public_key_spki_der to AuthInput and KeyData structures.
      std::string challenge_spki;
      if (!base::HexStringToString(request.public_key_spki_der(),
                                   &challenge_spki)) {
        LOG(ERROR) << "Challenge SPKI Public Key DER is not hex encoded.";
        std::move(on_done).Run(MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocCreateVaultKeysetRpcImplKeyNotHexEncoded),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
        return;
      }

      hwsec::StatusOr<uint32_t> family = hwsec_->GetFamily();
      if (!family.ok()) {
        LOG(ERROR) << "Failed to get the TPM family: " << family.status();
        std::move(on_done).Run(MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(kLocCreateVaultKeysetRpcImplFailedTPMFamily),
            ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
        return;
      }
      // TPM 1.2 only supports SHA1, otherwise default to entire list of
      // SHA algorithms.
      std::vector<SerializedChallengeSignatureAlgorithm> algos;
      if (family.value() == kTpm12Family) {
        algos.push_back(
            SerializedChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha1);
      } else {
        for (auto algo :
             {SerializedChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha512,
              SerializedChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha384,
              SerializedChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha256,
              SerializedChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha1}) {
          algos.push_back(algo);
        }
      }

      // Initializes a ChallengeCredentialAuthInput.
      // Append challenge algorithm and key_delegate_dbus_service_name for
      // testing to ChallengeCredentialAuthInput struct.
      if (!request.key_delegate_dbus_service_name().empty() &&
          !challenge_spki.empty()) {
        auth_input.challenge_credential_auth_input =
            ChallengeCredentialAuthInput{
                .challenge_signature_algorithms = algos,
                .dbus_service_name = request.key_delegate_dbus_service_name(),
            };
        auto* challenge_key = key_data.add_challenge_response_key();
        challenge_key->set_public_key_spki_der(challenge_spki);
        auth_factor_metadata.metadata = SmartCardMetadata{
            .public_key_spki_der = brillo::BlobFromString(challenge_spki)};
      }
      break;
    }
    default:
      LOG(ERROR) << "Unimplemented AuthFactorType.";
      std::move(on_done).Run(MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(
              kLocCreateVaultKeysetRpcImplUnspecifiedAuthFactorType),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
          user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT));
      return;
  }

  auto create_callback = base::BindOnce(
      &CreateVaultKeysetRpcImpl::CreateAndPersistVaultKeyset,
      weak_factory_.GetWeakPtr(), key_data, request.disable_key_data(),
      std::ref(auth_session), std::move(on_done));

  auth_block_utility_->CreateKeyBlobsWithAuthBlock(
      auth_block_type.value(), auth_input, auth_factor_metadata,
      std::move(create_callback));
}

void CreateVaultKeysetRpcImpl::CreateAndPersistVaultKeyset(
    const KeyData& key_data,
    const bool disable_key_data,
    AuthSession& auth_session,
    StatusCallback on_done,
    CryptohomeStatus callback_error,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::unique_ptr<AuthBlockState> auth_state) {
  // callback_error, key_blobs and auth_state are returned by
  // AuthBlock::CreateCallback.
  if (!callback_error.ok() || key_blobs == nullptr || auth_state == nullptr) {
    if (callback_error.ok()) {
      callback_error = MakeStatus<CryptohomeCryptoError>(
          CRYPTOHOME_ERR_LOC(
              kLocCreateVaultKeysetRpcImplNullParamInCallbackInAddKeyset),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
          CryptoError::CE_OTHER_CRYPTO,
          user_data_auth::CryptohomeErrorCode::
              CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
    }
    LOG(ERROR) << "KeyBlobs derivation failed before adding keyset.";
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocCreateVaultKeysetRpcImplCreateFailedInAddKeyset),
            user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED)
            .Wrap(std::move(callback_error)));
    return;
  }

  CryptohomeStatus status = AddVaultKeyset(
      key_data.label(), key_data, auth_session.obfuscated_username(),
      auth_session.file_system_keyset(),
      !auth_session.auth_factor_map().HasFactorWithStorage(
          AuthFactorStorageType::kVaultKeyset),
      VaultKeysetIntent{.backup = false}, std::move(key_blobs),
      std::move(auth_state));

  if (!status.ok()) {
    std::move(on_done).Run(
        MakeStatus<CryptohomeError>(
            CRYPTOHOME_ERR_LOC(
                kLocCreateVaultKeysetRpcImplAddVaultKeysetFailed),
            user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED)
            .Wrap(std::move(status)));
    return;
  }

  if (!ClearKeyDataFromInitialKeyset(auth_session.obfuscated_username(),
                                     disable_key_data)) {
    std::move(on_done).Run(MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocCreateVaultKeysetRpcImplClearKeyDataFromInitialKeysetFailed),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED));
    return;
  }

  // A stateless object to convert AuthFactor API to VaultKeyset KeyData and
  // VaultKeysets to AuthFactor API.
  AuthFactorVaultKeysetConverter converter(keyset_management_);

  // Add VaultKeyset as an AuthFactor to the linked AuthSession.
  if (std::optional<AuthFactor> added_auth_factor =
          converter.VaultKeysetToAuthFactor(auth_session.obfuscated_username(),
                                            key_data.label())) {
    auth_session.RegisterVaultKeysetAuthFactor(*added_auth_factor);
  } else {
    LOG(WARNING) << "Failed to convert added keyset to AuthFactor.";
  }

  std::move(on_done).Run(OkStatus<CryptohomeError>());
}

CryptohomeStatus CreateVaultKeysetRpcImpl::AddVaultKeyset(
    const std::string& key_label,
    const KeyData& key_data,
    const ObfuscatedUsername& obfuscated_username,
    const FileSystemKeyset& file_system_keyset,
    bool is_initial_keyset,
    VaultKeysetIntent vk_backup_intent,
    std::unique_ptr<KeyBlobs> key_blobs,
    std::unique_ptr<AuthBlockState> auth_state) {
  CHECK(key_blobs);
  CHECK(auth_state);
  if (is_initial_keyset) {
    // TODO(b/229825202): Migrate KeysetManagement and wrap the returned error.
    CryptohomeStatusOr<std::unique_ptr<VaultKeyset>> vk_status =
        keyset_management_->AddInitialKeyset(
            vk_backup_intent, obfuscated_username, key_data,
            /*challenge_credentials_keyset_info*/ std::nullopt,
            file_system_keyset, std::move(*key_blobs.get()),
            std::move(auth_state));
    if (!vk_status.ok()) {
      initial_vault_keyset_ = nullptr;
      return MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(
              kLocCreateVaultKeysetRpcImplAddInitialFailedInAddKeyset),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                          PossibleAction::kReboot}),
          user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED);
    }
    LOG(INFO) << "CreateVaultKeysetRpcImpl: added initial keyset "
              << key_data.label() << ".";
    initial_vault_keyset_ = std::move(vk_status).value();
  } else {
    if (!initial_vault_keyset_) {
      // This shouldn't normally happen, but is possible if, e.g., the backup VK
      // is corrupted and the authentication completed via USS.
      return MakeStatus<CryptohomeError>(
          CRYPTOHOME_ERR_LOC(kLocCreateVaultKeysetRpcImplNoVkInAddKeyset),
          ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
          user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED);
    }
    CryptohomeStatus status = keyset_management_->AddKeyset(
        vk_backup_intent, obfuscated_username, key_label, key_data,
        *initial_vault_keyset_.get(), std::move(*key_blobs.get()),
        std::move(auth_state), true /*clobber*/);
    if (!status.ok()) {
      return MakeStatus<CryptohomeError>(
                 CRYPTOHOME_ERR_LOC(
                     kLocCreateVaultKeysetRpcImplAddFailedInAddKeyset))
          .Wrap(std::move(status));
    }
    LOG(INFO) << "CreateVaultKeysetRpcImpl: added additional keyset "
              << key_label << ".";
  }

  return OkStatus<CryptohomeError>();
}

}  // namespace cryptohome
