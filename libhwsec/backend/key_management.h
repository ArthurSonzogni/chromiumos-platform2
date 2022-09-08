// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_KEY_MANAGEMENT_H_
#define LIBHWSEC_BACKEND_KEY_MANAGEMENT_H_

#include <cstdint>

#include <absl/container/flat_hash_set.h>
#include <brillo/secure_blob.h>

#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"
#include "libhwsec/structures/operation_policy.h"

namespace hwsec {

class BackendTpm2;

// KeyManagement provide the functions to manager key.
class KeyManagement {
 public:
  enum class PersistentKeyType {
    kStorageRootKey,
  };
  struct CreateKeyOptions {
    bool allow_software_gen = false;
    bool allow_decrypt = false;
    bool allow_sign = false;
  };
  struct CreateKeyResult {
    ScopedKey key;
    brillo::Blob key_blob;
  };

  // Gets the supported algorithm.
  virtual StatusOr<absl::flat_hash_set<KeyAlgoType>> GetSupportedAlgo() = 0;

  // Creates a key with |key_algo| algorithm, |policy| and optional |options|.
  virtual StatusOr<CreateKeyResult> CreateKey(
      const OperationPolicySetting& policy,
      KeyAlgoType key_algo,
      CreateKeyOptions options) = 0;

  // Loads a key from |key_blob| with |policy|.
  virtual StatusOr<ScopedKey> LoadKey(const OperationPolicy& policy,
                                      const brillo::Blob& key_blob) = 0;

  // Creates an auto-reload key with |key_algo| algorithm, |policy| and
  // optional |options|.
  virtual StatusOr<CreateKeyResult> CreateAutoReloadKey(
      const OperationPolicySetting& policy,
      KeyAlgoType key_algo,
      CreateKeyOptions options) = 0;

  // Loads an auto-reload key from |key_blob| with |policy|.
  virtual StatusOr<ScopedKey> LoadAutoReloadKey(
      const OperationPolicy& policy, const brillo::Blob& key_blob) = 0;

  // Loads the persistent key with specific |key_type|.
  virtual StatusOr<ScopedKey> GetPersistentKey(PersistentKeyType key_type) = 0;

  // Loads the hash of public part of the |key|.
  virtual StatusOr<brillo::Blob> GetPubkeyHash(Key key) = 0;

  // Flushes the |key| to reclaim the resource.
  virtual Status Flush(Key key) = 0;

  // Reloads the |key| if possible.
  virtual Status ReloadIfPossible(Key key) = 0;

  // Loads the key with raw |key_handle|.
  // TODO(174816474): deprecated legacy APIs.
  virtual StatusOr<ScopedKey> SideLoadKey(uint32_t key_handle) = 0;

  // Loads the raw |key_handle| from key.
  // TODO(174816474): deprecated legacy APIs.
  virtual StatusOr<uint32_t> GetKeyHandle(Key key) = 0;

 protected:
  KeyManagement() = default;
  ~KeyManagement() = default;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_KEY_MANAGEMENT_H_
