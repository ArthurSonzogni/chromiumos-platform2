// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTORS_PASSWORD_AUTH_FACTOR_H_
#define CRYPTOHOME_AUTH_FACTORS_PASSWORD_AUTH_FACTOR_H_

#include <memory>

#include "cryptohome/auth_factors/auth_factor.h"
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
  MountError AuthenticateAuthFactor(const Credentials& credential,
                                    bool is_ephemeral_user) override;

  // Transfer ownership of password verifier that can be used to verify
  // credentials during unlock.
  std::unique_ptr<CredentialVerifier> TakeCredentialVerifier() override;
  // Returns the key data with which this AuthFactor is authenticated with.
  const cryptohome::KeyData& GetKeyData() const override;

  // Return VaultKeyset of the authenticated user.
  const VaultKeyset* vault_keyset() const override {
    return vault_keyset_.get();
  };

  // Returns FileSystemKeyset of the authenticated user.
  const FileSystemKeyset GetFileSystemKeyset() const override;

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

#endif  // CRYPTOHOME_AUTH_FACTORS_PASSWORD_AUTH_FACTOR_H_
