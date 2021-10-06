// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_PASSWORD_AUTH_FACTOR_H_
#define CRYPTOHOME_PASSWORD_AUTH_FACTOR_H_

#include <memory>

#include "cryptohome/auth_factor.h"
#include "cryptohome/UserDataAuth.pb.h"

namespace cryptohome {

// PasswordAuthFactor defines the behaviour for when AuthSession wants to use
// password to authenticate.
class PasswordAuthFactor : public AuthFactor {
 public:
  explicit PasswordAuthFactor(KeysetManagement* keyset_management);
  virtual ~PasswordAuthFactor() = default;

  // AuthenticateAuthFactor authenticates user credentials if they exist. This
  // currently uses VaultKeyset, but will eventually use AuthBlocks and USS.
  bool AuthenticateAuthFactor(const Credentials& credential,
                              bool is_ephemeral_user,
                              MountError* code) override;

  // Transfer ownership of password verifier that can be used to verify
  // credentials during unlock.
  std::unique_ptr<CredentialVerifier> TakeCredentialVerifier() override;
  // Returns the key data with which this AuthFactor is authenticated with.
  const cryptohome::KeyData& GetKeyData() override;

  // This function returns the current index of the keyset that was used to
  // Authenticate. This is useful during verification of challenge credentials.
  const int GetKeyIndex() override;

  // Get VaultKeyset.
  VaultKeyset vault_keyset() override { return *vault_keyset_; };

  // Return a const reference to FileSystemKeyset.
  const FileSystemKeyset GetFileSystemKeyset() override;

 private:
  // The creator of the PasswordAuthFactor object is responsible for the
  // life of KeysetManagement object.
  KeysetManagement* keyset_management_;
  // This is used by User Session to verify users credentials at unlock.
  std::unique_ptr<CredentialVerifier> credential_verifier_;
  // Used to decrypt/ encrypt & store credentials.
  std::unique_ptr<VaultKeyset> vault_keyset_;
  // Used to store key meta data.
  cryptohome::KeyData key_data_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_PASSWORD_AUTH_FACTOR_H_
