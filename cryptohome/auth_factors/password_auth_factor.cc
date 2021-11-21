// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factors/password_auth_factor.h"

#include <memory>
#include <utility>

#include "cryptohome/scrypt_verifier.h"
namespace cryptohome {

PasswordAuthFactor::PasswordAuthFactor(KeysetManagement* keyset_management)
    : keyset_management_(keyset_management) {}

MountError PasswordAuthFactor::AuthenticateAuthFactor(
    const Credentials& credential, bool is_ephemeral_user) {
  // Store key data in current auth_factor for future use.
  key_data_ = credential.key_data();

  if (!is_ephemeral_user) {
    // A persistent mount will always have a persistent key on disk. Here
    // keyset_management tries to fetch that persistent credential.
    MountError error = MOUNT_ERROR_NONE;
    // TODO(dlunev): fix conditional error when we switch to StatusOr.
    vault_keyset_ = keyset_management_->LoadUnwrappedKeyset(credential, &error);
    if (!vault_keyset_) {
      return error == MOUNT_ERROR_NONE ? MOUNT_ERROR_FATAL : error;
    }
  }

  // Set the credential verifier for this credential.
  credential_verifier_.reset(new ScryptVerifier());
  credential_verifier_->Set(credential.passkey());

  return MOUNT_ERROR_NONE;
}

std::unique_ptr<CredentialVerifier>
PasswordAuthFactor::TakeCredentialVerifier() {
  return std::move(credential_verifier_);
}

const cryptohome::KeyData& PasswordAuthFactor::GetKeyData() const {
  return key_data_;
}

const FileSystemKeyset PasswordAuthFactor::GetFileSystemKeyset() const {
  return FileSystemKeyset(*vault_keyset_);
}

}  // namespace cryptohome
