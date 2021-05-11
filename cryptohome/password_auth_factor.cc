// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/password_auth_factor.h"

#include <memory>
#include <utility>

#include "cryptohome/scrypt_password_verifier.h"
namespace cryptohome {

PasswordAuthFactor::PasswordAuthFactor(KeysetManagement* keyset_management)
    : keyset_management_(keyset_management) {}

bool PasswordAuthFactor::AuthenticateAuthFactor(const Credentials& credential,
                                                MountError* code) {
  if (code) {
    *code = MOUNT_ERROR_NONE;
  }
  vault_keyset_ = keyset_management_->LoadUnwrappedKeyset(credential, code);
  if (vault_keyset_) {
    password_verifier_.reset(new ScryptPasswordVerifier());
    password_verifier_->Set(credential.passkey());
  }
  return vault_keyset_ != nullptr;
}

std::unique_ptr<PasswordVerifier> PasswordAuthFactor::TakePasswordVerifier() {
  return std::move(password_verifier_);
}

const cryptohome::KeyData& PasswordAuthFactor::GetKeyData() {
  return vault_keyset_->GetKeyData();
}

const int PasswordAuthFactor::GetKeyIndex() {
  return vault_keyset_->GetLegacyIndex();
}

const FileSystemKeyset PasswordAuthFactor::GetFileSystemKeyset() {
  return FileSystemKeyset(*vault_keyset_);
}

}  // namespace cryptohome
