// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/protobuf.h"

#include <optional>
#include <string>
#include <utility>

#include <base/logging.h>
#include <base/system/sys_info.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>

#include "cryptohome/auth_factor/label.h"
#include "cryptohome/auth_factor/metadata.h"
#include "cryptohome/auth_factor/type.h"
#include "cryptohome/features.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"

namespace cryptohome {
namespace {

std::optional<SerializedKnowledgeFactorHashInfo>
KnowledgeFactorHashInfoFromProto(
    const user_data_auth::KnowledgeFactorHashInfo& hash_info) {
  std::optional<SerializedKnowledgeFactorHashAlgorithm> algorithm =
      SerializedKnowledgeFactorAlgorithmFromProto(hash_info.algorithm());
  if (!algorithm.has_value()) {
    return std::nullopt;
  }
  return SerializedKnowledgeFactorHashInfo{
      .algorithm = *algorithm,
      .salt = brillo::BlobFromString(hash_info.salt()),
      .should_generate_key_store = hash_info.should_generate_key_store(),
  };
}

// Set password metadata here.
void GetPasswordMetadata(const user_data_auth::AuthFactor& auth_factor,
                         AuthFactorMetadata& out_auth_factor_metadata) {
  PasswordMetadata password_metadata;
  if (auth_factor.password_metadata().has_hash_info()) {
    password_metadata.hash_info = KnowledgeFactorHashInfoFromProto(
        auth_factor.password_metadata().hash_info());
  }
  out_auth_factor_metadata.metadata = password_metadata;
}

// Set pin metadata here.
void GetPinMetadata(const user_data_auth::AuthFactor& auth_factor,
                    AuthFactorMetadata& out_auth_factor_metadata) {
  PinMetadata pin_metadata;
  if (auth_factor.pin_metadata().has_hash_info()) {
    pin_metadata.hash_info = KnowledgeFactorHashInfoFromProto(
        auth_factor.pin_metadata().hash_info());
  }
  out_auth_factor_metadata.metadata = pin_metadata;
}

// Set cryptohome recovery metadata here, which includes mediator_pub_key.
void GetCryptohomeRecoveryMetadata(
    const user_data_auth::AuthFactor& auth_factor,
    AuthFactorMetadata& out_auth_factor_metadata) {
  out_auth_factor_metadata.metadata = CryptohomeRecoveryMetadata{
      .mediator_pub_key = brillo::BlobFromString(
          auth_factor.cryptohome_recovery_metadata().mediator_pub_key())};
}

// Set kiosk metadata here.
void GetKioskMetadata(const user_data_auth::AuthFactor& auth_factor,
                      AuthFactorMetadata& out_auth_factor_metadata) {
  out_auth_factor_metadata.metadata = KioskMetadata();
}

// Set smart card metadata here, which includes public_key_spki_der.
void GetSmartCardMetadata(const user_data_auth::AuthFactor& auth_factor,
                          AuthFactorMetadata& out_auth_factor_metadata) {
  out_auth_factor_metadata.metadata = SmartCardMetadata{
      .public_key_spki_der = brillo::BlobFromString(
          auth_factor.smart_card_metadata().public_key_spki_der())};
}

void GetFingerprintMetadata(const user_data_auth::AuthFactor& auth_factor,
                            AuthFactorMetadata& out_auth_factor_metadata) {
  out_auth_factor_metadata.metadata = FingerprintMetadata{
      .was_migrated = auth_factor.fingerprint_metadata().was_migrated()};
}

SerializedLockoutPolicy LockoutPolicyFromAuthFactorProto(
    const user_data_auth::LockoutPolicy& policy) {
  switch (policy) {
    case user_data_auth::LOCKOUT_POLICY_ATTEMPT_LIMITED:
      return SerializedLockoutPolicy::ATTEMPT_LIMITED;
    case user_data_auth::LOCKOUT_POLICY_TIME_LIMITED:
      return SerializedLockoutPolicy::TIME_LIMITED;
    case user_data_auth::LOCKOUT_POLICY_NONE:
    // Usually, LOCKOUT_POLICY_UNKNOWN will will be an error for invalid
    // argument, but until chrome implements the change, we will continue to
    // keep it default to kNoLockout.
    case user_data_auth::LOCKOUT_POLICY_UNKNOWN:
    // Default covers for proto min and max.
    default:
      return SerializedLockoutPolicy::NO_LOCKOUT;
  }
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

KnowledgeFactorHashAlgorithm SerializedKnowledgeFactorAlgorithmToProto(
    const SerializedKnowledgeFactorHashAlgorithm& algorithm) {
  switch (algorithm) {
    case SerializedKnowledgeFactorHashAlgorithm::PBKDF2_AES256_1234:
      return KnowledgeFactorHashAlgorithm::HASH_TYPE_PBKDF2_AES256_1234;
    case SerializedKnowledgeFactorHashAlgorithm::SHA256_TOP_HALF:
      return KnowledgeFactorHashAlgorithm::HASH_TYPE_SHA256_TOP_HALF;
  }
}

std::optional<SerializedKnowledgeFactorHashAlgorithm>
SerializedKnowledgeFactorAlgorithmFromProto(
    const KnowledgeFactorHashAlgorithm& algorithm) {
  switch (algorithm) {
    case KnowledgeFactorHashAlgorithm::HASH_TYPE_PBKDF2_AES256_1234:
      return SerializedKnowledgeFactorHashAlgorithm::PBKDF2_AES256_1234;
    case KnowledgeFactorHashAlgorithm::HASH_TYPE_SHA256_TOP_HALF:
      return SerializedKnowledgeFactorHashAlgorithm::SHA256_TOP_HALF;
    default:
      return std::nullopt;
  }
}

std::optional<user_data_auth::KnowledgeFactorHashInfo>
KnowledgeFactorHashInfoToProto(
    const SerializedKnowledgeFactorHashInfo& hash_info) {
  user_data_auth::KnowledgeFactorHashInfo hash_info_proto;
  if (!hash_info.algorithm.has_value()) {
    return std::nullopt;
  }
  hash_info_proto.set_algorithm(
      SerializedKnowledgeFactorAlgorithmToProto(*hash_info.algorithm));
  hash_info_proto.set_salt(brillo::BlobToString(hash_info.salt));
  hash_info_proto.set_should_generate_key_store(
      hash_info.should_generate_key_store.value_or(false));
  return hash_info_proto;
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

bool AuthFactorPropertiesFromProto(
    const user_data_auth::AuthFactor& auth_factor,
    const AsyncInitFeatures& features,
    AuthFactorType& out_auth_factor_type,
    std::string& out_auth_factor_label,
    AuthFactorMetadata& out_auth_factor_metadata) {
  // Extract the common metadata.
  out_auth_factor_metadata.common.chromeos_version_last_updated =
      auth_factor.common_metadata().chromeos_version_last_updated();
  out_auth_factor_metadata.common.chrome_version_last_updated =
      auth_factor.common_metadata().chrome_version_last_updated();
  out_auth_factor_metadata.common.lockout_policy =
      LockoutPolicyFromAuthFactorProto(
          auth_factor.common_metadata().lockout_policy());
  out_auth_factor_metadata.common.user_specified_name =
      auth_factor.common_metadata().user_specified_name();

  // Extract the factor type and use it to try and extract the factor-specific
  // metadata. Returns false if this fails.
  switch (auth_factor.type()) {
    case user_data_auth::AUTH_FACTOR_TYPE_PASSWORD:
      if (!auth_factor.has_password_metadata()) {
        LOG(ERROR) << "Password auth factor does not have password metadata.";
        return false;
      }
      GetPasswordMetadata(auth_factor, out_auth_factor_metadata);
      out_auth_factor_type = AuthFactorType::kPassword;
      break;
    case user_data_auth::AUTH_FACTOR_TYPE_PIN:
      if (!auth_factor.has_pin_metadata()) {
        LOG(ERROR) << "PIN auth factor does not have PIN metadata.";
        return false;
      }
      GetPinMetadata(auth_factor, out_auth_factor_metadata);
      out_auth_factor_type = AuthFactorType::kPin;
      // All new PINs use modern time-limited lockouts.
      out_auth_factor_metadata.common.lockout_policy =
          SerializedLockoutPolicy::TIME_LIMITED;
      break;
    case user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY:
      if (!auth_factor.has_cryptohome_recovery_metadata()) {
        LOG(ERROR) << "Recovery auth factor does not have recovery metadata.";
        return false;
      }
      GetCryptohomeRecoveryMetadata(auth_factor, out_auth_factor_metadata);
      out_auth_factor_type = AuthFactorType::kCryptohomeRecovery;
      break;
    case user_data_auth::AUTH_FACTOR_TYPE_KIOSK:
      if (!auth_factor.has_kiosk_metadata()) {
        LOG(ERROR) << "Kiosk auth factor does not have kiosk metadata.";
        return false;
      }
      GetKioskMetadata(auth_factor, out_auth_factor_metadata);
      out_auth_factor_type = AuthFactorType::kKiosk;
      break;
    case user_data_auth::AUTH_FACTOR_TYPE_SMART_CARD:
      if (!auth_factor.has_smart_card_metadata()) {
        LOG(ERROR)
            << "Smart card auth factor does not have smart card metadata.";
        return false;
      }
      GetSmartCardMetadata(auth_factor, out_auth_factor_metadata);
      out_auth_factor_type = AuthFactorType::kSmartCard;
      break;
    case user_data_auth::AUTH_FACTOR_TYPE_LEGACY_FINGERPRINT:
      // Legacy fingerprint factor has empty metadata, skip the metadata
      // extraction.
      out_auth_factor_type = AuthFactorType::kLegacyFingerprint;
      break;
    case user_data_auth::AUTH_FACTOR_TYPE_FINGERPRINT:
      if (!auth_factor.has_fingerprint_metadata()) {
        LOG(ERROR)
            << "Fingerprint auth factor does not have fingerprint metadata.";
        return false;
      }
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

}  // namespace cryptohome
