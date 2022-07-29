// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_input_utils.h"

#include <optional>
#include <string>

#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>

#include "cryptohome/key_objects.h"
#include "cryptohome/signature_sealing/structures_proto.h"

using brillo::SecureBlob;

namespace cryptohome {

namespace {

AuthInput FromPasswordAuthInput(
    const user_data_auth::PasswordAuthInput& proto) {
  return AuthInput{
      .user_input = SecureBlob(proto.secret()),
  };
}

AuthInput FromPinAuthInput(const user_data_auth::PinAuthInput& proto) {
  return AuthInput{
      .user_input = SecureBlob(proto.secret()),
  };
}

AuthInput FromCryptohomeRecoveryAuthInput(
    const user_data_auth::CryptohomeRecoveryAuthInput& proto,
    const std::optional<brillo::SecureBlob>&
        cryptohome_recovery_ephemeral_pub_key) {
  CryptohomeRecoveryAuthInput recovery_auth_input{
      // These fields are used for `Create`:
      .mediator_pub_key = SecureBlob(proto.mediator_pub_key()),
      // These fields are used for `Derive`:
      .epoch_response = SecureBlob(proto.epoch_response()),
      .ephemeral_pub_key =
          cryptohome_recovery_ephemeral_pub_key.value_or(SecureBlob()),
      .recovery_response = SecureBlob(proto.recovery_response()),
  };

  return AuthInput{.cryptohome_recovery_auth_input = recovery_auth_input};
}

AuthInput FromSmartCardAuthInput(
    const user_data_auth::SmartCardAuthInput& proto) {
  ChallengeCredentialAuthInput chall_cred_auth_input;
  for (const auto& content : proto.signature_algorithms()) {
    std::optional<structure::ChallengeSignatureAlgorithm> signature_algorithm =
        proto::FromProto(ChallengeSignatureAlgorithm(content));
    if (signature_algorithm.has_value()) {
      chall_cred_auth_input.challenge_signature_algorithms.push_back(
          signature_algorithm.value());
    } else {
      // One of the signature algorithm's parsed is CHALLENGE_NOT_SPECIFIED.
      return AuthInput{
          .challenge_credential_auth_input = std::nullopt,
      };
    }
  }

  return AuthInput{
      .challenge_credential_auth_input = chall_cred_auth_input,
  };
}

}  // namespace

std::optional<AuthInput> CreateAuthInput(
    const user_data_auth::AuthInput& auth_input_proto,
    const std::string& obfuscated_username,
    bool locked_to_single_user,
    const std::optional<brillo::SecureBlob>&
        cryptohome_recovery_ephemeral_pub_key) {
  std::optional<AuthInput> auth_input;
  switch (auth_input_proto.input_case()) {
    case user_data_auth::AuthInput::kPasswordInput:
      auth_input = FromPasswordAuthInput(auth_input_proto.password_input());
      break;
    case user_data_auth::AuthInput::kPinInput:
      auth_input = FromPinAuthInput(auth_input_proto.pin_input());
      break;
    case user_data_auth::AuthInput::kCryptohomeRecoveryInput:
      auth_input = FromCryptohomeRecoveryAuthInput(
          auth_input_proto.cryptohome_recovery_input(),
          cryptohome_recovery_ephemeral_pub_key);
      break;
    case user_data_auth::AuthInput::kKioskInput:
      break;
    case user_data_auth::AuthInput::kSmartCardInput:
      auth_input = FromSmartCardAuthInput(auth_input_proto.smart_card_input());
      break;
    case user_data_auth::AuthInput::INPUT_NOT_SET:
      break;
  }

  if (!auth_input.has_value()) {
    LOG(ERROR) << "Empty or unknown auth input";
    return std::nullopt;
  }

  // Fill out common fields.
  auth_input.value().obfuscated_username = obfuscated_username;
  auth_input.value().locked_to_single_user = locked_to_single_user;

  return auth_input;
}

}  // namespace cryptohome
