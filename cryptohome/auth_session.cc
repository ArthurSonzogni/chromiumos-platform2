// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_session.h"

#include <memory>
#include <string>
#include <utility>

#include <brillo/cryptohome.h>
#include <cryptohome/scrypt_verifier.h>

#include "cryptohome/keyset_management.h"
#include "cryptohome/password_auth_factor.h"
#include "cryptohome/storage/mount_utils.h"
#include "cryptohome/vault_keyset.h"

using brillo::cryptohome::home::SanitizeUserName;

namespace cryptohome {

// Size of the values used serialization of UnguessableToken.
constexpr int kSizeOfSerializedValueInToken = sizeof(uint64_t);
// Number of uint64 used serialization of UnguessableToken.
constexpr int kNumberOfSerializedValuesInToken = 2;
// Offset where the high value is used in Serialized string.
constexpr int kHighTokenOffset = 0;
// Offset where the low value is used in Serialized string.
constexpr int kLowTokenOffset = kSizeOfSerializedValueInToken;
// AuthSession will time out if it is active after this time interval.
constexpr base::TimeDelta kAuthSessionTimeoutInMinutes =
    base::TimeDelta::FromMinutes(5);

AuthSession::AuthSession(
    std::string username,
    unsigned int flags,
    base::OnceCallback<void(const base::UnguessableToken&)> on_timeout,
    KeysetManagement* keyset_management)
    : username_(username),
      on_timeout_(std::move(on_timeout)),
      keyset_management_(keyset_management) {
  token_ = base::UnguessableToken::Create();
  ProcessFlags(flags);
  timer_.Start(
      FROM_HERE, kAuthSessionTimeoutInMinutes,
      base::Bind(&AuthSession::AuthSessionTimedOut, base::Unretained(this)));
  user_exists_ = keyset_management_->UserExists(SanitizeUserName(username_));
  if (user_exists_)
    keyset_management_->GetVaultKeysetLabelsAndData(SanitizeUserName(username_),
                                                    &key_label_data_);
}

AuthSession::~AuthSession() = default;

void AuthSession::ProcessFlags(unsigned int flags) {
  is_kiosk_user_ =
      flags & user_data_auth::AuthSessionFlags::AUTH_SESSION_FLAGS_KIOSK_USER;
}

void AuthSession::AuthSessionTimedOut() {
  status_ = AuthStatus::kAuthStatusTimedOut;
  // After this call back to |UserDataAuth|, |this| object will be deleted.
  std::move(on_timeout_).Run(token_);
}

user_data_auth::CryptohomeErrorCode AuthSession::AddCredentials(
    const user_data_auth::AddCredentialsRequest& request) {
  MountError code;
  auto credentials = GetCredentials(request.authorization(), &code);
  if (!credentials) {
    return MountErrorToCryptohomeError(code);
  }
  if (user_exists_) {
    // Check the privileges to ensure Add is allowed.
    // Keys without extended data are considered fully privileged.
    if (auth_factor_->vault_keyset().HasKeyData() &&
        !auth_factor_->vault_keyset().GetKeyData().privileges().add()) {
      LOG(WARNING) << "Add Credentials: no add() privilege";
      return user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_DENIED;
    }

    return keyset_management_->AddKeyset(
        *credentials, auth_factor_->vault_keyset(), request.clobber_if_exists(),
        request.authorization().key().has_data() /* Has new data */);
  }

  user_data_auth::CryptohomeErrorCode errorCode =
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET;
  // An assumption here is that keyset management saves the user keys on disk.
  if (!keyset_management_->AddInitialKeyset(*credentials)) {
    errorCode = user_data_auth::CRYPTOHOME_ADD_CREDENTIALS_FAILED;
  }
  return errorCode;
}

user_data_auth::CryptohomeErrorCode AuthSession::Authenticate(
    const cryptohome::AuthorizationRequest& authorization_request) {
  MountError code;
  auto credentials = GetCredentials(authorization_request, &code);
  if (!credentials) {
    return MountErrorToCryptohomeError(code);
  }
  if (authorization_request.key().data().type() == KeyData::KEY_TYPE_PASSWORD) {
    auth_factor_ = std::make_unique<PasswordAuthFactor>(keyset_management_);
  }

  bool authenticated =
      auth_factor_->AuthenticateAuthFactor(*credentials, &code);
  if (authenticated) {
    status_ = AuthStatus::kAuthStatusAuthenticated;
  }
  return MountErrorToCryptohomeError(code);
}

std::unique_ptr<CredentialVerifier> AuthSession::TakeCredentialVerifier() {
  return auth_factor_->TakeCredentialVerifier();
}

// static
base::Optional<std::string> AuthSession::GetSerializedStringFromToken(
    const base::UnguessableToken& token) {
  if (token == base::UnguessableToken::Null()) {
    LOG(ERROR) << "Invalid UnguessableToken given";
    return base::nullopt;
  }
  std::string serialized_token;
  serialized_token.resize(kSizeOfSerializedValueInToken *
                          kNumberOfSerializedValuesInToken);
  uint64_t high = token.GetHighForSerialization();
  uint64_t low = token.GetLowForSerialization();
  memcpy(&serialized_token[kHighTokenOffset], &high, sizeof(high));
  memcpy(&serialized_token[kLowTokenOffset], &low, sizeof(low));
  return serialized_token;
}

// static
base::Optional<base::UnguessableToken>
AuthSession::GetTokenFromSerializedString(const std::string& serialized_token) {
  if (serialized_token.size() !=
      kSizeOfSerializedValueInToken * kNumberOfSerializedValuesInToken) {
    LOG(ERROR) << "Incorrect serialized string size";
    return base::nullopt;
  }
  uint64_t high, low;
  memcpy(&high, &serialized_token[kHighTokenOffset], sizeof(high));
  memcpy(&low, &serialized_token[kLowTokenOffset], sizeof(low));
  return base::UnguessableToken::Deserialize(high, low);
}

std::unique_ptr<Credentials> AuthSession::GetCredentials(
    const cryptohome::AuthorizationRequest& authorization_request,
    MountError* error) {
  auto credentials = std::make_unique<Credentials>(
      username_, brillo::SecureBlob(authorization_request.key().secret()));
  credentials->set_key_data(authorization_request.key().data());

  if (is_kiosk_user_) {
    brillo::SecureBlob public_mount_passkey =
        keyset_management_->GetPublicMountPassKey(username_);
    if (public_mount_passkey.empty()) {
      LOG(ERROR) << "Could not get public mount passkey.";
      *error = MountError::MOUNT_ERROR_KEY_FAILURE;
      return nullptr;
    }
    credentials->set_passkey(public_mount_passkey);
  }

  return credentials;
}

}  // namespace cryptohome
