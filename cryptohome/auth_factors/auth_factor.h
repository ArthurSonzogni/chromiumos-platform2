// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTORS_AUTH_FACTOR_H_
#define CRYPTOHOME_AUTH_FACTORS_AUTH_FACTOR_H_

#include <memory>
#include <string>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/credential_verifier.h"
#include "cryptohome/credentials.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/rpc.pb.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/UserDataAuth.pb.h"

namespace cryptohome {

// This is a pure virtual interface designed to be implemented by the different
// authentication factors - password, pin, security keys, etc - so that
// they take handle multiple factors of the same type and know what to do with
// it.
class AuthFactor {
 public:
  AuthFactor() = default;
  virtual ~AuthFactor() = default;
  // AuthenticateAuthFactor validates the key should it exist on disk for the
  // user.
  virtual MountError AuthenticateAuthFactor(const Credentials& credential,
                                            bool is_ephemeral_user) = 0;

  // Transfer ownership of password verifier that can be used to verify
  // credentials during unlock.
  virtual std::unique_ptr<CredentialVerifier> TakeCredentialVerifier() = 0;
  // -------------------------------------------------------------------------
  // Temporary functions below as we transition from AuthSession to AuthFactor
  // -------------------------------------------------------------------------
  // Returns the key data with which this AuthFactor is authenticated with.
  virtual const cryptohome::KeyData& GetKeyData() const = 0;

  // Return VaultKeyset of the authenticated user.
  virtual const VaultKeyset* vault_keyset() const = 0;

  // Returns FileSystemKeyset of the authenticated user.
  virtual const FileSystemKeyset GetFileSystemKeyset() const = 0;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTORS_AUTH_FACTOR_H_
