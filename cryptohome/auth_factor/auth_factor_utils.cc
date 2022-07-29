// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/auth_factor_utils.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_label.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"

namespace cryptohome {

namespace {
// Set password metadata here, which happens to be empty. For other types of
// factors, there will be more computations involved.
void GetPasswordMetadata(const user_data_auth::AuthFactor& auth_factor,
                         AuthFactorMetadata& out_auth_factor_metadata) {
  out_auth_factor_metadata.metadata = PasswordAuthFactorMetadata();
}

// Set pin metadata here, which happens to be empty.
void GetPinMetadata(const user_data_auth::AuthFactor& auth_factor,
                    AuthFactorMetadata& out_auth_factor_metadata) {
  out_auth_factor_metadata.metadata = PinAuthFactorMetadata();
}

// Set cryptohome recovery metadata here, which happens to be empty.
void GetCryptohomeRecoveryMetadata(
    const user_data_auth::AuthFactor& auth_factor,
    AuthFactorMetadata& out_auth_factor_metadata) {
  out_auth_factor_metadata.metadata = CryptohomeRecoveryAuthFactorMetadata();
}

// Creates a D-Bus proto for a password auth factor.
std::optional<user_data_auth::AuthFactor> ToPasswordProto(
    const PasswordAuthFactorMetadata& metadata) {
  user_data_auth::AuthFactor proto;
  proto.set_type(user_data_auth::AUTH_FACTOR_TYPE_PASSWORD);
  // There's no metadata for password auth factors currently.
  proto.mutable_password_metadata();
  return proto;
}

// Creates a D-Bus proto for a pin auth factor.
std::optional<user_data_auth::AuthFactor> ToPinProto(
    const PinAuthFactorMetadata& metadata) {
  user_data_auth::AuthFactor proto;
  proto.set_type(user_data_auth::AUTH_FACTOR_TYPE_PIN);
  // There's no metadata for pin auth factors currently.
  proto.mutable_pin_metadata();
  return proto;
}

// Creates a D-Bus proto for a cryptohome recovery auth factor.
std::optional<user_data_auth::AuthFactor> ToCryptohomeRecoveryProto(
    const CryptohomeRecoveryAuthFactorMetadata& metadata) {
  user_data_auth::AuthFactor proto;
  proto.set_type(user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY);
  // TODO(b/232896212): There's no metadata for recovery auth factor currently.
  proto.mutable_cryptohome_recovery_metadata();
  return proto;
}
}  // namespace

// GetAuthFactorMetadata sets the metadata inferred from the proto. This
// includes the metadata struct and type.
bool GetAuthFactorMetadata(const user_data_auth::AuthFactor& auth_factor,
                           AuthFactorMetadata& out_auth_factor_metadata,
                           AuthFactorType& out_auth_factor_type,
                           std::string& out_auth_factor_label) {
  switch (auth_factor.type()) {
    case user_data_auth::AUTH_FACTOR_TYPE_PASSWORD:
      DCHECK(auth_factor.has_password_metadata());
      GetPasswordMetadata(auth_factor, out_auth_factor_metadata);
      out_auth_factor_type = AuthFactorType::kPassword;
      break;
    case user_data_auth::AUTH_FACTOR_TYPE_PIN:
      DCHECK(auth_factor.has_pin_metadata());
      GetPinMetadata(auth_factor, out_auth_factor_metadata);
      out_auth_factor_type = AuthFactorType::kPin;
      break;
    case user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY:
      DCHECK(auth_factor.has_cryptohome_recovery_metadata());
      GetCryptohomeRecoveryMetadata(auth_factor, out_auth_factor_metadata);
      out_auth_factor_type = AuthFactorType::kCryptohomeRecovery;
      break;
    default:
      LOG(ERROR) << "Unknown auth factor type " << auth_factor.type();
      return false;
  }

  out_auth_factor_label = auth_factor.label();
  if (!IsValidAuthFactorLabel(out_auth_factor_label)) {
    LOG(ERROR) << "Invalid auth factor label";
    return false;
  }

  return true;
}

std::optional<user_data_auth::AuthFactor> GetAuthFactorProto(
    const AuthFactorMetadata& auth_factor_metadata,
    const AuthFactorType& auth_factor_type,
    const std::string& auth_factor_label) {
  std::optional<user_data_auth::AuthFactor> proto;
  switch (auth_factor_type) {
    case AuthFactorType::kPassword: {
      auto* password_metadata = std::get_if<PasswordAuthFactorMetadata>(
          &auth_factor_metadata.metadata);
      proto = password_metadata ? ToPasswordProto(*password_metadata)
                                : std::nullopt;
      break;
    }
    case AuthFactorType::kPin: {
      auto* pin_metadata =
          std::get_if<PinAuthFactorMetadata>(&auth_factor_metadata.metadata);
      proto = pin_metadata ? ToPinProto(*pin_metadata) : std::nullopt;
      break;
    }
    case AuthFactorType::kCryptohomeRecovery: {
      auto* cryptohome_recovery_metadata =
          std::get_if<CryptohomeRecoveryAuthFactorMetadata>(
              &auth_factor_metadata.metadata);
      proto = cryptohome_recovery_metadata
                  ? ToCryptohomeRecoveryProto(*cryptohome_recovery_metadata)
                  : std::nullopt;
      break;
    }
    case AuthFactorType::kUnspecified: {
      LOG(ERROR) << "Cannot convert unspecified AuthFactor to proto";
      return std::nullopt;
    }
  }
  if (!proto.has_value()) {
    LOG(ERROR) << "Failed to convert auth factor to proto";
    return std::nullopt;
  }
  proto.value().set_label(auth_factor_label);
  return proto;
}

void LoadUserAuthFactorProtos(
    AuthFactorManager* manager,
    const std::string& obfuscated_username,
    google::protobuf::RepeatedPtrField<user_data_auth::AuthFactor>*
        out_auth_factors) {
  for (const auto& [label, auth_factor_type] :
       manager->ListAuthFactors(obfuscated_username)) {
    // Try to load the auth factor. If this fails we just skip it and move on
    // rather than failing the entire operation.
    CryptohomeStatusOr<std::unique_ptr<AuthFactor>> owned_auth_factor =
        manager->LoadAuthFactor(obfuscated_username, auth_factor_type, label);
    if (!owned_auth_factor.ok() || *owned_auth_factor == nullptr) {
      LOG(WARNING) << "Unable to load an AuthFactor with label " << label
                   << ".";
      continue;
    }
    // Use the auth factor to populate the response.
    AuthFactor& auth_factor = **owned_auth_factor;
    auto auth_factor_proto = GetAuthFactorProto(
        auth_factor.metadata(), auth_factor.type(), auth_factor.label());
    if (auth_factor_proto) {
      *out_auth_factors->Add() = std::move(*auth_factor_proto);
    }
  }
}

bool NeedsResetSecret(AuthFactorType auth_factor_type) {
  return auth_factor_type == AuthFactorType::kPin;
}

}  // namespace cryptohome
