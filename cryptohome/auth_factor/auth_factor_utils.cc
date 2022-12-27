// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/auth_factor_utils.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include <base/system/sys_info.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>

#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_label.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_prepare_purpose.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_session_proto_utils.h"

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

// Set kiosk metadata here.
void GetKioskMetadata(const user_data_auth::AuthFactor& auth_factor,
                      AuthFactorMetadata& out_auth_factor_metadata) {
  out_auth_factor_metadata.metadata = KioskAuthFactorMetadata();
}

// Set smart card metadata here, which includes public_key_spki_der.
void GetSmartCardMetadata(const user_data_auth::AuthFactor& auth_factor,
                          AuthFactorMetadata& out_auth_factor_metadata) {
  out_auth_factor_metadata.metadata = SmartCardAuthFactorMetadata{
      .public_key_spki_der = brillo::BlobFromString(
          auth_factor.smart_card_metadata().public_key_spki_der())};
}

void GetFingerprintMetadata(const user_data_auth::AuthFactor& auth_factor,
                            AuthFactorMetadata& out_auth_factor_metadata) {
  out_auth_factor_metadata.metadata = FingerprintAuthFactorMetadata();
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

// Creates a D-Bus proto for a kiosk auth factor.
std::optional<user_data_auth::AuthFactor> ToKioskProto(
    const KioskAuthFactorMetadata& metadata) {
  user_data_auth::AuthFactor proto;
  proto.set_type(user_data_auth::AUTH_FACTOR_TYPE_KIOSK);
  proto.mutable_kiosk_metadata();
  return proto;
}

// Creates a D-Bus proto for a smart card auth factor.
std::optional<user_data_auth::AuthFactor> ToSmartCardProto(
    const SmartCardAuthFactorMetadata& metadata) {
  user_data_auth::AuthFactor proto;
  proto.set_type(user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD);
  proto.mutable_smart_card_metadata()->set_public_key_spki_der(
      brillo::BlobToString(metadata.public_key_spki_der));
  return proto;
}

// Creates a D-Bus proto for a legacy fingerprint auth factor.
std::optional<user_data_auth::AuthFactor> ToLegacyFingerprintProto() {
  user_data_auth::AuthFactor proto;
  proto.set_type(user_data_auth::AUTH_FACTOR_TYPE_LEGACY_FINGERPRINT);
  return proto;
}

// Creates a D-Bus proto for a fingerprint auth factor.
std::optional<user_data_auth::AuthFactor> ToFingerprintProto() {
  user_data_auth::AuthFactor proto;
  proto.set_type(user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT);
  return proto;
}

}  // namespace

user_data_auth::AuthFactorType AuthFactorTypeToProto(AuthFactorType type) {
  switch (type) {
    case AuthFactorType::kPassword:
      return user_data_auth::AUTH_FACTOR_TYPE_PASSWORD;
    case AuthFactorType::kPin:
      return user_data_auth::AUTH_FACTOR_TYPE_PIN;
    case AuthFactorType::kCryptohomeRecovery:
      return user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY;
    case AuthFactorType::kKiosk:
      return user_data_auth::AUTH_FACTOR_TYPE_KIOSK;
    case AuthFactorType::kSmartCard:
      return user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD;
    case AuthFactorType::kLegacyFingerprint:
      return user_data_auth::AUTH_FACTOR_TYPE_LEGACY_FINGERPRINT;
    case AuthFactorType::kFingerprint:
      return user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT;
    case AuthFactorType::kUnspecified:
      return user_data_auth::AUTH_FACTOR_TYPE_UNSPECIFIED;
  }
}

std::optional<AuthFactorType> AuthFactorTypeFromProto(
    user_data_auth::AuthFactorType type) {
  switch (type) {
    case user_data_auth::AUTH_FACTOR_TYPE_UNSPECIFIED:
      return AuthFactorType::kUnspecified;
    case user_data_auth::AUTH_FACTOR_TYPE_PASSWORD:
      return AuthFactorType::kPassword;
    case user_data_auth::AUTH_FACTOR_TYPE_PIN:
      return AuthFactorType::kPin;
    case user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY:
      return AuthFactorType::kCryptohomeRecovery;
    case user_data_auth::AUTH_FACTOR_TYPE_KIOSK:
      return AuthFactorType::kKiosk;
    case user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD:
      return AuthFactorType::kSmartCard;
    case user_data_auth::AUTH_FACTOR_TYPE_LEGACY_FINGERPRINT:
      return AuthFactorType::kLegacyFingerprint;
    case user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT:
      return AuthFactorType::kFingerprint;
    default:
      return std::nullopt;
  }
}

void PopulateAuthFactorProtoWithSysinfo(
    user_data_auth::AuthFactor& auth_factor) {
  // Populate the ChromeOS version. Note that reading GetLsbReleaseValue can
  // fail but in that case we still populate the metadata with an empty string.
  std::string chromeos_version;
  base::SysInfo::GetLsbReleaseValue("CHROMEOS_RELEASE_VERSION",
                                    &chromeos_version);
  auth_factor.mutable_common_metadata()->set_chromeos_version_last_updated(
      std::move(chromeos_version));
}

bool GetAuthFactorMetadata(const user_data_auth::AuthFactor& auth_factor,
                           AuthFactorMetadata& out_auth_factor_metadata,
                           AuthFactorType& out_auth_factor_type,
                           std::string& out_auth_factor_label) {
  // Extract the common metadata.
  out_auth_factor_metadata.common.chromeos_version_last_updated =
      auth_factor.common_metadata().chromeos_version_last_updated();
  out_auth_factor_metadata.common.chrome_version_last_updated =
      auth_factor.common_metadata().chrome_version_last_updated();

  // Extract the factor type and use it to try and extract the factor-specific
  // metadata. Returns false if this fails.
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
    case user_data_auth::AUTH_FACTOR_TYPE_KIOSK:
      DCHECK(auth_factor.has_kiosk_metadata());
      GetKioskMetadata(auth_factor, out_auth_factor_metadata);
      out_auth_factor_type = AuthFactorType::kKiosk;
      break;
    case user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD:
      DCHECK(auth_factor.has_smart_card_metadata());
      GetSmartCardMetadata(auth_factor, out_auth_factor_metadata);
      out_auth_factor_type = AuthFactorType::kSmartCard;
      break;
    case user_data_auth::AUTH_FACTOR_TYPE_LEGACY_FINGERPRINT:
      DCHECK(auth_factor.has_legacy_fingerprint_metadata());
      // Legacy fingerprint factor has empty metadata, skip the metadata
      // extraction.
      out_auth_factor_type = AuthFactorType::kLegacyFingerprint;
      break;
    case user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT:
      DCHECK(auth_factor.has_fingerprint_metadata());
      GetFingerprintMetadata(auth_factor, out_auth_factor_metadata);
      out_auth_factor_type = AuthFactorType::kFingerprint;
      break;
    default:
      LOG(ERROR) << "Unknown auth factor type " << auth_factor.type();
      return false;
  }

  // Extract the label. Returns false if it isn't formatted correctly.
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
  // Try to populate the factor-specific data into the proto.
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
    case AuthFactorType::kKiosk: {
      auto* kiosk_metadata =
          std::get_if<KioskAuthFactorMetadata>(&auth_factor_metadata.metadata);
      proto = kiosk_metadata ? ToKioskProto(*kiosk_metadata) : std::nullopt;
      break;
    }
    case AuthFactorType::kSmartCard: {
      auto* smart_card_metadata = std::get_if<SmartCardAuthFactorMetadata>(
          &auth_factor_metadata.metadata);
      proto = smart_card_metadata ? ToSmartCardProto(*smart_card_metadata)
                                  : std::nullopt;
      break;
    }
    case AuthFactorType::kLegacyFingerprint: {
      proto = ToLegacyFingerprintProto();
      break;
    }
    case AuthFactorType::kFingerprint: {
      proto = ToFingerprintProto();
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
  // If we get here we were able to populate a proto with all the
  // factor-specific data. Now fill in the common metadata and the label.
  // This step cannot fail.
  proto->set_label(auth_factor_label);
  proto->mutable_common_metadata()->set_chromeos_version_last_updated(
      auth_factor_metadata.common.chromeos_version_last_updated);
  proto->mutable_common_metadata()->set_chrome_version_last_updated(
      auth_factor_metadata.common.chrome_version_last_updated);
  return proto;
}

void LoadUserAuthFactorProtos(
    AuthFactorManager* manager,
    const AuthBlockUtility& auth_block_utility,
    const std::string& obfuscated_username,
    google::protobuf::RepeatedPtrField<user_data_auth::AuthFactorWithStatus>*
        out_auth_factors_status) {
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
      user_data_auth::AuthFactorWithStatus auth_factor_with_status;
      *auth_factor_with_status.mutable_auth_factor() =
          std::move(*auth_factor_proto);
      auto supported_intents = auth_block_utility.GetSupportedIntentsFromState(
          auth_factor.auth_block_state());
      for (const auto& auth_intent : supported_intents) {
        auth_factor_with_status.add_available_for_intents(
            AuthIntentToProto(auth_intent));
      }

      *out_auth_factors_status->Add() = std::move(auth_factor_with_status);
    }
  }
}

bool LoadUserAuthFactorByLabel(AuthFactorManager* manager,
                               const AuthBlockUtility& auth_block_utility,
                               const std::string& obfuscated_username,
                               const std::string& factor_label,
                               user_data_auth::AuthFactor* out_auth_factor) {
  for (const auto& [label, auth_factor_type] :
       manager->ListAuthFactors(obfuscated_username)) {
    if (label == factor_label) {
      CryptohomeStatusOr<std::unique_ptr<AuthFactor>> owned_auth_factor =
          manager->LoadAuthFactor(obfuscated_username, auth_factor_type, label);
      if (!owned_auth_factor.ok() || *owned_auth_factor == nullptr) {
        return false;
      }

      AuthFactor& auth_factor = **owned_auth_factor;
      auto auth_factor_proto = GetAuthFactorProto(
          auth_factor.metadata(), auth_factor.type(), auth_factor.label());
      if (!auth_factor_proto) {
        return false;
      }
      *out_auth_factor = std::move(*auth_factor_proto);
      return true;
    }
  }
  return false;
}

bool NeedsResetSecret(AuthFactorType auth_factor_type) {
  return auth_factor_type == AuthFactorType::kPin;
}

std::optional<AuthFactorPreparePurpose> AuthFactorPreparePurposeFromProto(
    user_data_auth::AuthFactorPreparePurpose purpose) {
  switch (purpose) {
    case user_data_auth::PURPOSE_AUTHENTICATE_AUTH_FACTOR:
      return AuthFactorPreparePurpose::kPrepareAuthenticateAuthFactor;
    case user_data_auth::PURPOSE_ADD_AUTH_FACTOR:
      return AuthFactorPreparePurpose::kPrepareAddAuthFactor;
    default:
      return std::nullopt;
  }
}

std::tuple<AuthFactorMap, std::map<std::string, KeyData>> LoadAuthFactorMap(
    const std::string& obfuscated_username,
    Platform& platform,
    AuthFactorVaultKeysetConverter& converter,
    AuthFactorManager& manager) {
  AuthFactorMap auth_factor_map;
  std::map<std::string, KeyData> key_data_map;

  // Load all the VaultKeysets and backup VaultKeysets in disk and convert
  // them to AuthFactor format.
  std::map<std::string, std::unique_ptr<AuthFactor>> backup_factor_map;
  std::map<std::string, std::unique_ptr<AuthFactor>> vk_factor_map;
  converter.VaultKeysetsToAuthFactorsAndKeyLabelData(
      obfuscated_username, vk_factor_map, backup_factor_map, &key_data_map);
  // Load the USS AuthFactors.
  std::map<std::string, std::unique_ptr<AuthFactor>> uss_factor_map =
      manager.LoadAllAuthFactors(obfuscated_username);

  // UserSecretStash is enabled: merge VaultKeyset-AuthFactors with
  // USS-AuthFactors
  if (IsUserSecretStashExperimentEnabled(&platform)) {
    for (auto& [unused, factor] : uss_factor_map) {
      auth_factor_map.Add(std::move(factor),
                          AuthFactorStorageType::kUserSecretStash);
    }
  } else {
    // UserSecretStash is disabled: merge VaultKeyset-AuthFactors with
    // backup-VaultKeyset-AuthFactors.
    for (auto& [unused, factor] : backup_factor_map) {
      auth_factor_map.Add(std::move(factor),
                          AuthFactorStorageType::kVaultKeyset);
    }
  }

  // Duplicate labels are not expected on any use case. However in very rare
  // edge cases where an interrupted USS migration results in having both
  // regular VaultKeyset and USS factor in disk it is safer to use the original
  // VaultKeyset. In that case regular VaultKeyset overrides the existing
  // label in the map.
  for (auto& [unused, factor] : vk_factor_map) {
    if (auth_factor_map.Find(factor->label())) {
      LOG(WARNING) << "Unexpected duplication of label: " << factor->label()
                   << ". Regular VaultKeyset will override the AuthFactor.";
    }
    auth_factor_map.Add(std::move(factor), AuthFactorStorageType::kVaultKeyset);
  }

  return std::tuple(std::move(auth_factor_map), std::move(key_data_map));
}

}  // namespace cryptohome
