// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/auth_factor.h"

#include <memory>
#include <utility>

#include "cryptohome/scrypt_verifier.h"
namespace cryptohome {

AuthFactor::AuthFactor(KeysetManagement* keyset_management)
    : keyset_management_(keyset_management) {}

MountError AuthFactor::AuthenticateAuthFactor(const Credentials& credential,
                                              bool is_ephemeral_user) {
  // Store key data in current auth_factor for future use.
  key_data_ = credential.key_data();

  if (!is_ephemeral_user) {
    // A persistent mount will always have a persistent key on disk. Here
    // keyset_management tries to fetch that persistent credential.
    MountError error = MOUNT_ERROR_NONE;
    // TODO(dlunev): fix conditional error when we switch to StatusOr.
    vault_keyset_ = keyset_management_->GetValidKeyset(credential, &error);
    if (!vault_keyset_) {
      return error == MOUNT_ERROR_NONE ? MOUNT_ERROR_FATAL : error;
    }
    // Add the missing fields in the keyset, if any, and resave.
    keyset_management_->ReSaveKeysetIfNeeded(credential, vault_keyset_.get());
  }

  // Set the credential verifier for this credential.
  credential_verifier_.reset(new ScryptVerifier());
  credential_verifier_->Set(credential.passkey());

  return MOUNT_ERROR_NONE;
}

std::unique_ptr<CredentialVerifier> AuthFactor::TakeCredentialVerifier() {
  return std::move(credential_verifier_);
}

const cryptohome::KeyData& AuthFactor::GetKeyData() const {
  return key_data_;
}

const FileSystemKeyset AuthFactor::GetFileSystemKeyset() const {
  return FileSystemKeyset(*vault_keyset_);
}

}  // namespace cryptohome
