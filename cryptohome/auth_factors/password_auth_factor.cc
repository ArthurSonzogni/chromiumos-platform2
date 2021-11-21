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

bool PasswordAuthFactor::AuthenticateAuthFactor(const Credentials& credential,
                                                bool is_ephemeral_user,
                                                MountError* code) {
  if (code) {
    *code = MOUNT_ERROR_NONE;
  }
  // Store key data in current auth_factor for future use.
  key_data_ = credential.key_data();

  if (!is_ephemeral_user) {
    // A persistent mount will always have a persistent key on disk. Here
    // keyset_management tries to fetch that persistent credential.
    vault_keyset_ = keyset_management_->LoadUnwrappedKeyset(credential, code);
    if (!vault_keyset_) {
      return false;
    }
  }

  // Set the credential verifier for this credential.
  credential_verifier_.reset(new ScryptVerifier());
  credential_verifier_->Set(credential.passkey());

  return true;
}

std::unique_ptr<CredentialVerifier>
PasswordAuthFactor::TakeCredentialVerifier() {
  return std::move(credential_verifier_);
}

const cryptohome::KeyData& PasswordAuthFactor::GetKeyData() {
  return key_data_;
}

const FileSystemKeyset PasswordAuthFactor::GetFileSystemKeyset() {
  return FileSystemKeyset(*vault_keyset_);
}

}  // namespace cryptohome
