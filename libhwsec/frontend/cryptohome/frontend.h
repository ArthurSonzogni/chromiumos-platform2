// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_CRYPTOHOME_FRONTEND_H_
#define LIBHWSEC_FRONTEND_CRYPTOHOME_FRONTEND_H_

#include <string>
#include <vector>

#include <absl/container/flat_hash_set.h>
#include <brillo/secure_blob.h>

#include "libhwsec/backend/key_management.h"
#include "libhwsec/backend/signature_sealing.h"
#include "libhwsec/backend/storage.h"
#include "libhwsec/frontend/frontend.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"
#include "libhwsec/structures/operation_policy.h"

namespace hwsec {

class CryptohomeFrontend : public Frontend {
 public:
  using CreateKeyResult = KeyManagement::CreateKeyResult;
  using StorageState = Storage::ReadyState;
  using ChallengeID = SignatureSealing::ChallengeID;
  using ChallengeResult = SignatureSealing::ChallengeResult;
  using SignatureSealingAlgorithm = SignatureSealing::Algorithm;

  ~CryptohomeFrontend() override = default;

  // Is the security module enabled or not.
  virtual StatusOr<bool> IsEnabled() = 0;

  // Is the security module ready to use or not.
  virtual StatusOr<bool> IsReady() = 0;

  // Is DA counter can be mitigated or not.
  virtual StatusOr<bool> IsDAMitigationReady() = 0;

  // Is the SRK ROCA vulnerable or not.
  virtual StatusOr<bool> IsSrkRocaVulnerable() = 0;

  // Tries to mitigate the DA counter.
  virtual Status MitigateDACounter() = 0;

  // Gets the lookup key for Remote Server Unlock.
  virtual StatusOr<brillo::Blob> GetRsuDeviceId() = 0;

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

  // Is the current user had been set or not.
  virtual StatusOr<bool> IsCurrentUserSet() = 0;

  // Is the device supported sealing/unsealing or not.
  virtual StatusOr<bool> IsSealingSupported() = 0;

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

  // Gets the state of |space|.
  virtual StatusOr<StorageState> GetSpaceState(Space space) = 0;

  // Prepares the |space|.
  virtual Status PrepareSpace(Space space, uint32_t size) = 0;

  // Reads the data of |space|.
  virtual StatusOr<brillo::Blob> LoadSpace(Space space) = 0;

  // Writes the data to |space|.
  virtual Status StoreSpace(Space space, const brillo::Blob& blob) = 0;

  // Destroys the |space|.
  virtual Status DestroySpace(Space space) = 0;

  // Is the |space| write locked or not.
  virtual StatusOr<bool> IsSpaceWriteLocked(Space space) = 0;

  // Declares the TPM firmware is stable.
  virtual Status DeclareTpmFirmwareStable() = 0;

  // Seals the |unsealed_data| with |public_key_spki_der| and binds to
  // |current_user| or the prior login state.
  //
  // |key_algorithms| is the list of signature algorithms supported by the
  // key. Listed in the order of preference (starting from the most
  // preferred); however, the implementation is permitted to ignore this
  // order.
  virtual StatusOr<SignatureSealedData> SealWithSignatureAndCurrentUser(
      const std::string& current_user,
      const brillo::SecureBlob& unsealed_data,
      const brillo::Blob& public_key_spki_der,
      const std::vector<SignatureSealingAlgorithm>& key_algorithms) = 0;

  // Creates a challenge from the |sealed_data| and the current user state,
  // |public_key_spki_der|, |key_algorithms|.
  virtual StatusOr<ChallengeResult> ChallengeWithSignatureAndCurrentUser(
      const SignatureSealedData& sealed_data,
      const brillo::Blob& public_key_spki_der,
      const std::vector<SignatureSealingAlgorithm>& key_algorithms) = 0;

  // Unseals the sealed_data from previous |challenge| with the
  // |challenge_response|.
  virtual StatusOr<brillo::SecureBlob> UnsealWithChallenge(
      ChallengeID challenge, const brillo::Blob& challenge_response) = 0;

  // Gets the TPM family of GSC/TPM.
  // 0x312E3200 = TPM1.2
  // 0x322E3000 = TPM2.0
  virtual StatusOr<uint32_t> GetFamily() = 0;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_CRYPTOHOME_FRONTEND_H_
