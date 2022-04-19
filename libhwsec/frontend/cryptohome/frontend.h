// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_CRYPTOHOME_FRONTEND_H_
#define LIBHWSEC_FRONTEND_CRYPTOHOME_FRONTEND_H_

#include <string>

#include <absl/container/flat_hash_set.h>
#include <brillo/secure_blob.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/frontend/frontend.h"
#include "libhwsec/hwsec_export.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"
#include "libhwsec/structures/operation_policy.h"

namespace hwsec {

class HWSEC_EXPORT CryptohomeFrontend : public Frontend {
 public:
  using CreateKeyResult = Backend::KeyManagerment::CreateKeyResult;
  ~CryptohomeFrontend() override = default;

  // Is the security module enabled or not.
  virtual StatusOr<bool> IsEnabled() = 0;

  // Is the security module ready to use or not.
  virtual StatusOr<bool> IsReady() = 0;

  // Gets the supported algorithm.
  virtual StatusOr<absl::flat_hash_set<KeyAlgoType>> GetSupportedAlgo() = 0;

  // Creates the cryptohome key with specific |key_algo| algorithm.
  virtual StatusOr<CreateKeyResult> CreateCryptohomeKey(
      KeyAlgoType key_algo) = 0;

  // Loads key from |key_blob|.
  virtual StatusOr<ScopedKey> LoadKey(const brillo::Blob& key_blob) = 0;

  // Loads the hash of public part of the |key|.
  virtual StatusOr<brillo::Blob> GetPubkeyHash(Key key) = 0;

  // Loads the key with raw |key_handle|.
  // TODO(174816474): deprecated legacy APIs.
  virtual StatusOr<ScopedKey> SideLoadKey(uint32_t key_handle) = 0;

  // Loads the raw |key_handle| from key.
  // TODO(174816474): deprecated legacy APIs.
  virtual StatusOr<uint32_t> GetKeyHandle(Key key) = 0;

  // Sets the |current_user| config.
  virtual Status SetCurrentUser(const std::string& current_user) = 0;

  // Seals the |unsealed_data| with |auth_value| and binds to |current_user|.
  // If the |current_user| is std::nullopt, it would bind to the prior login
  // state.
  virtual StatusOr<brillo::Blob> SealWithCurrentUser(
      const std::optional<std::string>& current_user,
      const brillo::SecureBlob& auth_value,
      const brillo::SecureBlob& unsealed_data) = 0;

  // Preloads the |sealed_data|.
  virtual StatusOr<std::optional<ScopedKey>> PreloadSealedData(
      const brillo::Blob& sealed_data) = 0;

  // Unseals the |sealed_data| with |auth_value| and optional |preload_data|.
  virtual StatusOr<brillo::SecureBlob> UnsealWithCurrentUser(
      std::optional<Key> preload_data,
      const brillo::SecureBlob& auth_value,
      const brillo::Blob& sealed_data) = 0;

  // Encrypts the |plaintext| with |key|.
  virtual StatusOr<brillo::Blob> Encrypt(
      Key key, const brillo::SecureBlob& plaintext) = 0;

  // Decrypts the |ciphertext| with |key|.
  virtual StatusOr<brillo::SecureBlob> Decrypt(
      Key key, const brillo::Blob& ciphertext) = 0;

  // Derives the auth value from |pass_blob| with key.
  virtual StatusOr<brillo::SecureBlob> GetAuthValue(
      Key key, const brillo::SecureBlob& pass_blob) = 0;

  // Generates random blob with |size|.
  virtual StatusOr<brillo::Blob> GetRandomBlob(size_t size) = 0;

  // Generates random secure blob with |size|.
  virtual StatusOr<brillo::SecureBlob> GetRandomSecureBlob(size_t size) = 0;

  // Gets the manufacturer.
  virtual StatusOr<uint32_t> GetManufacturer() = 0;

  // Is the PinWeaver enabled or not.
  virtual StatusOr<bool> IsPinWeaverEnabled() = 0;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_CRYPTOHOME_FRONTEND_H_
