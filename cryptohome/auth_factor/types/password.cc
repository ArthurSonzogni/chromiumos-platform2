// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/password.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "cryptohome/auth_blocks/pin_weaver_auth_block.h"
#include "cryptohome/auth_factor/label_arity.h"
#include "cryptohome/auth_factor/metadata.h"
#include "cryptohome/auth_factor/protobuf.h"
#include "cryptohome/auth_factor/verifiers/scrypt.h"
#include "cryptohome/auth_session/intent.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"

namespace cryptohome {
namespace {

bool IsCredentialVerifierSupported(base::span<const AuthBlockType> block_types,
                                   Crypto* crypto,
                                   AuthFactorDriver::UserType user_type) {
  switch (user_type) {
    case AuthFactorDriver::UserType::kEphemeral:
      return true;
    case AuthFactorDriver::UserType::kPersistent: {
      bool is_pinweaver_used =
          std::find(block_types.begin(), block_types.end(),
                    AuthBlockType::kPinWeaver) != block_types.end() &&
          PinWeaverAuthBlock::IsSupported(*crypto->GetHwsec()).ok();
      return !is_pinweaver_used;
    }
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
