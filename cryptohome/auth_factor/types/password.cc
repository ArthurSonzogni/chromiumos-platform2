// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/password.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <variant>

#include <absl/functional/overload.h>
#include <base/time/time.h>
#include <libhwsec-foundation/status/status_chain.h>

#include "cryptohome/auth_blocks/pin_weaver_auth_block.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/label_arity.h"
#include "cryptohome/auth_factor/metadata.h"
#include "cryptohome/auth_factor/protobuf.h"
#include "cryptohome/auth_factor/verifiers/scrypt.h"
#include "cryptohome/auth_session/intent.h"
#include "cryptohome/error/action.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/error/cryptohome_tpm_error.h"
#include "cryptohome/error/locations.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"

namespace cryptohome {
namespace {

using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::ErrorActionSet;
using ::cryptohome::error::PossibleAction;
using ::hwsec_foundation::status::MakeStatus;

bool IsPinWeaverUsed(base::span<const AuthBlockType> block_types,
                     Crypto* crypto) {
  return std::find(block_types.begin(), block_types.end(),
                   AuthBlockType::kPinWeaver) != block_types.end() &&
         PinWeaverAuthBlock::IsSupported(*crypto->GetHwsec()).ok();
}

bool IsCredentialVerifierSupported(base::span<const AuthBlockType> block_types,
                                   Crypto* crypto,
                                   AuthFactorDriver::UserType user_type) {
  switch (user_type) {
    case AuthFactorDriver::UserType::kEphemeral:
      return true;
    case AuthFactorDriver::UserType::kPersistent:
      return !IsPinWeaverUsed(block_types, crypto);
  }
}

CryptohomeStatusOr<base::TimeDelta> GetPinWeaverFactorDelay(
    const PinWeaverAuthBlockState& state, Crypto* crypto) {
  if (!state.le_label) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorPasswordGetFactorDelayMissingLabel),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }
  // Try and extract the delay from pinweaver manager.
  auto delay_in_seconds =
      crypto->GetPinWeaverManager()->GetDelayInSeconds(*state.le_label);
  if (!delay_in_seconds.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(
                   kLocAuthFactorPasswordGetFactorDelayReadFailed))
        .Wrap(MakeStatus<error::CryptohomeTPMError>(
            std::move(delay_in_seconds).err_status()));
  }
  // Return the extracted time, handling the max value case.
  if (*delay_in_seconds == std::numeric_limits<uint32_t>::max()) {
    return base::TimeDelta::Max();
  } else {
    return base::Seconds(*delay_in_seconds);
  }
}

}  // namespace

bool AfDriverWithPasswordBlockTypes::NeedsResetSecret() const {
  // Reset secrets are only used for pinweaver based passwords but since we
  // don't necessarily know at the call site what kind of auth block will be
  // selected we're stuck assuming that it will be needed.
  auto types = block_types();
  bool is_pinweaver_enabled =
      std::find(types.begin(), types.end(), AuthBlockType::kPinWeaver) !=
      types.end();
  return is_pinweaver_enabled;
}

bool PasswordAuthFactorDriver::IsSupportedByHardware() const {
  return true;
}

bool PasswordAuthFactorDriver::IsLightAuthSupported(AuthIntent auth_intent,
                                                    UserType user_type) const {
  if (auth_intent != AuthIntent::kVerifyOnly) {
    return false;
  }
  return IsCredentialVerifierSupported(block_types(), crypto_, user_type);
}

std::unique_ptr<CredentialVerifier>
PasswordAuthFactorDriver::CreateCredentialVerifier(
    const std::string& auth_factor_label,
    const AuthInput& auth_input,
    const AuthFactorMetadata& auth_factor_metadata,
    UserType user_type) const {
  if (!IsCredentialVerifierSupported(block_types(), crypto_, user_type)) {
    return nullptr;
  }
  if (!auth_input.user_input.has_value()) {
    LOG(ERROR) << "Cannot construct a password verifier without a password";
    return nullptr;
  }
  std::unique_ptr<CredentialVerifier> verifier = ScryptVerifier::Create(
      auth_factor_label, auth_factor_metadata, *auth_input.user_input);
  if (!verifier) {
    LOG(ERROR) << "Credential verifier initialization failed.";
    return nullptr;
  }
  return verifier;
}

bool PasswordAuthFactorDriver::IsDelaySupported() const {
  return IsPinWeaverUsed(block_types(), crypto_);
}

CryptohomeStatusOr<base::TimeDelta> PasswordAuthFactorDriver::GetFactorDelay(
    const ObfuscatedUsername& username, const AuthFactor& factor) const {
  // Do all the error checks to make sure the input is useful.
  if (factor.type() != type()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorPasswordGetFactorDelayWrongFactorType),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }
  return std::visit<CryptohomeStatusOr<base::TimeDelta>>(
      absl::Overload(
          [this](const PinWeaverAuthBlockState& state) {
            return GetPinWeaverFactorDelay(state, crypto_);
          },
          [](const TpmEccAuthBlockState&) { return base::TimeDelta(); },
          [](const TpmBoundToPcrAuthBlockState&) { return base::TimeDelta(); },
          [](const TpmNotBoundToPcrAuthBlockState&) {
            return base::TimeDelta();
          },
          [](const DoubleWrappedCompatAuthBlockState&) {
            return base::TimeDelta();
          },
          [](const ScryptAuthBlockState&) { return base::TimeDelta(); },
          [](const auto&) -> CryptohomeStatusOr<base::TimeDelta> {
            return MakeStatus<CryptohomeError>(
                CRYPTOHOME_ERR_LOC(
                    kLocAuthFactorPasswordGetFactorDelayInvalidBlockState),
                ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
                user_data_auth::CryptohomeErrorCode::
                    CRYPTOHOME_ERROR_INVALID_ARGUMENT);
          }),
      factor.auth_block_state().state);
}

AuthFactorLabelArity PasswordAuthFactorDriver::GetAuthFactorLabelArity() const {
  return AuthFactorLabelArity::kSingle;
}

std::optional<user_data_auth::AuthFactor>
PasswordAuthFactorDriver::TypedConvertToProto(
    const CommonMetadata& common,
    const PasswordMetadata& typed_metadata) const {
  user_data_auth::AuthFactor proto;
  proto.set_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  user_data_auth::PasswordMetadata& password_metadata =
      *proto.mutable_password_metadata();
  if (typed_metadata.hash_info.has_value()) {
    std::optional<user_data_auth::KnowledgeFactorHashInfo> hash_info_proto =
        KnowledgeFactorHashInfoToProto(*typed_metadata.hash_info);
    if (hash_info_proto.has_value()) {
      *password_metadata.mutable_hash_info() = std::move(*hash_info_proto);
    }
  }
  return proto;
}

}  // namespace cryptohome
