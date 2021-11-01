// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCK_H_
#define CRYPTOHOME_AUTH_BLOCK_H_

#include "cryptohome/auth_block_state.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/vault_keyset.h"

#include <base/optional.h>

namespace cryptohome {

// Defined in cryptohome_metrics.h
enum DerivationType : int;

// This is a pure virtual interface designed to be implemented by the different
// authentication methods - U2F, PinWeaver, TPM backed passwords, etc. - so that
// they take some arbitrary user input and give out a key.
class SyncAuthBlock {
 public:
  virtual ~SyncAuthBlock() = default;

  // This is implemented by concrete auth methods to create a fresh key from
  // user input. The key will then be used to wrap the keyset.
  // On success, it returns a constructed object, such as a
  // SerializedVaultKeyset, in the optional object, or base::nullopt on failure.
  virtual base::Optional<AuthBlockState> Create(const AuthInput& user_input,
                                                KeyBlobs* key_blobs,
                                                CryptoError* error) = 0;

  // This is implemented by concrete auth methods to map the user secret
  // input into a key. This method should successfully authenticate the user.
  virtual bool Derive(const AuthInput& auth_input,
                      const AuthBlockState& state,
                      KeyBlobs* key_blobs,
                      CryptoError* error) = 0;

  DerivationType derivation_type() const { return derivation_type_; }

 protected:
  // This is a virtual interface that should not be directly constructed.
  explicit SyncAuthBlock(DerivationType derivation_type)
      : derivation_type_(derivation_type) {}

 private:
  // For UMA - keeps track of the encryption type used in Derive().
  const DerivationType derivation_type_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCK_H_
