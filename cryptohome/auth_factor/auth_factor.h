// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_H_
#define CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_H_

#include <memory>
#include <string>

#include <base/optional.h>
#include <cryptohome/proto_bindings/rpc.pb.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/credential_verifier.h"
#include "cryptohome/credentials.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/storage/file_system_keyset.h"

namespace cryptohome {

// This is an interface designed to be implemented by the different
// authentication factors - password, pin, security keys, etc - so that
// they take handle multiple factors of the same type and know what to do with
// it.
class AuthFactor {
 public:
  explicit AuthFactor(KeysetManagement* keyset_management);

  // Only for testing currently.
  AuthFactor(AuthFactorType type,
             const std::string& label,
             const AuthFactorMetadata& metadata,
             const AuthBlockState& auth_block_state);

  ~AuthFactor() = default;

  // AuthenticateAuthFactor validates the key should it exist on disk for the
  // user.
  MountError AuthenticateAuthFactor(const Credentials& credential,
                                    bool is_ephemeral_user);

  // Transfer ownership of password verifier that can be used to verify
  // credentials during unlock.
  std::unique_ptr<CredentialVerifier> TakeCredentialVerifier();

  const base::Optional<AuthFactorType>& type() const { return type_; }
  const base::Optional<std::string>& label() const { return label_; }
  const base::Optional<AuthFactorMetadata>& metadata() const {
    return metadata_;
  }
  const base::Optional<AuthBlockState>& auth_block_state() const {
    return auth_block_state_;
  }

  // -------------------------------------------------------------------------
  // Temporary functions below as we transition from AuthSession to AuthFactor
  // -------------------------------------------------------------------------
  // Returns the key data with which this AuthFactor is authenticated with.
  const cryptohome::KeyData& GetKeyData() const;

  // Return VaultKeyset of the authenticated user.
  const VaultKeyset* vault_keyset() const { return vault_keyset_.get(); }

  // Returns FileSystemKeyset of the authenticated user.
  const FileSystemKeyset GetFileSystemKeyset() const;

 private:
  // The creator of the AuthFactor object is responsible for the
  // life of KeysetManagement object.
  KeysetManagement* keyset_management_ = nullptr;
  // This is used by User Session to verify users credentials at unlock.
  std::unique_ptr<CredentialVerifier> credential_verifier_;
  // Used to decrypt/ encrypt & store credentials.
  std::unique_ptr<VaultKeyset> vault_keyset_;
  // Used to store key meta data.
  cryptohome::KeyData key_data_;
  // The auth factor public information. TODO(b:208351356): Make these
  // non-optional by implementing vault keyset conversion into these fields.
  const base::Optional<AuthFactorType> type_;
  const base::Optional<std::string> label_;
  const base::Optional<AuthFactorMetadata> metadata_;
  // Contains the data that the auth factor needs for deriving the secret.
  base::Optional<AuthBlockState> auth_block_state_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_H_
