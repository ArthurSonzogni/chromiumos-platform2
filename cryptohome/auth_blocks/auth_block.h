// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_H_
#define CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_H_

#include <memory>

#include <base/callback.h>

#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {

// Defined in cryptohome_metrics.h
enum DerivationType : int;

// This is a pure virtual interface designed to be implemented by the different
// authentication methods - U2F, PinWeaver, TPM backed passwords, etc. - so that
// they take some arbitrary user input and give out a key.
class AuthBlock {
 public:
  virtual ~AuthBlock() = default;

  // If the operation succeeds, |key_blobs| will contain the constructed
  // KeyBlobs, AuthBlockState will be populated in |auth_block_state| and
  // |error| will be CryptoError::CE_NONE. On failure, error will be
  // populated, and should not rely on the value of key_blobs and
  // auth_block_state.
  using CreateCallback = base::OnceCallback<void(
      CryptoError error,
      std::unique_ptr<KeyBlobs> key_blobs,
      std::unique_ptr<AuthBlockState> auth_block_state)>;

  // This is implemented by concrete auth methods to create a fresh key from
  // user input.
  // This asynchronous API receives a callback to construct the KeyBlobs with
  // the released TPM secrets in an unblocking way. Once the callback is done,
  // on success, CryptoError will be CryptoError::CE_NONE, KeyBlobs and
  // AuthBlockState will be populated. On Failure, CryptoError is assigned the
  // related error value, the value of KeyBlobs and AuthBlockState are not valid
  // to use.
  virtual void Create(const AuthInput& user_input, CreateCallback callback) = 0;

  // If the operation succeeds, |key_blobs| will contain the constructed
  // KeyBlobs and |error| will be CryptoError::CE_NONE. On failure, error will
  // be populated, and should not rely on the value of key_blobs.
  using DeriveCallback = base::OnceCallback<void(
      CryptoError error, std::unique_ptr<KeyBlobs> key_blobs)>;

  // This is implemented by concrete auth methods to map the user secret
  // input/credentials into a key.
  // This asynchronous API receives a callback to construct the KeyBlobs with
  // the released TPM secrets in an unblocking way. Once the callback is done,
  // on success, CryptoError will be CryptoError::CE_NONE, KeyBlobs  will be
  // populated. On Failure, CryptoError is assigned the related error value, the
  // value of KeyBlobs are not valid to use.
  virtual void Derive(const AuthInput& auth_input,
                      const AuthBlockState& state,
                      DeriveCallback callback) = 0;

  DerivationType derivation_type() const { return derivation_type_; }

 protected:
  // This is a virtual interface that should not be directly constructed.
  explicit AuthBlock(DerivationType derivation_type)
      : derivation_type_(derivation_type) {}

 private:
  // For UMA - keeps track of the encryption type used in Derive().
  const DerivationType derivation_type_;
};

// This is a pure virtual interface designed to be implemented by the different
// authentication methods - U2F, PinWeaver, TPM backed passwords, etc. - so that
// they take some arbitrary user input and give out a key.
class SyncAuthBlock {
 public:
  virtual ~SyncAuthBlock() = default;

  // This is implemented by concrete auth methods to create a fresh key from
  // user input. The key will then be used to wrap the keyset.
  // On success, it returns CryptoError::CE_NONE, or the specific error on
  // failure.
  virtual CryptoError Create(const AuthInput& user_input,
                             AuthBlockState* auth_block_state,
                             KeyBlobs* key_blobs) = 0;

  // This is implemented by concrete auth methods to map the user secret
  // input into a key. This method should successfully authenticate the user.
  virtual CryptoError Derive(const AuthInput& auth_input,
                             const AuthBlockState& state,
                             KeyBlobs* key_blobs) = 0;

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

#endif  // CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_H_
