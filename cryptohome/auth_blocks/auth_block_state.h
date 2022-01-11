// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_STATE_H_
#define CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_STATE_H_

#include <optional>
#include <variant>

#include <brillo/secure_blob.h>

#include "cryptohome/auth_block_state_generated.h"
#include "cryptohome/signature_sealing/structures.h"

namespace cryptohome {
// TODO(b/199531643): Check the impact of using empty blobs stored in every
// AuthBlockState.

// Fields in AuthBlockState are all marked optional because they can be read
// from objects stored on disk, such as the SerializedVaultKeyset. As a result
// cryptohome cannot assume all fields are always populated. However, the
// fields should always be defined or the auth block cannot operate.

struct TpmNotBoundToPcrAuthBlockState {
  // Marks if the password is run through scrypt before going to the TPM.
  bool scrypt_derived = false;
  // The salt used to bind to the TPM.
  // Must be set.
  std::optional<brillo::SecureBlob> salt;
  // Optional, the number of rounds key derivation is called.
  // This is only used for legacy non-scrypt key derivation.
  std::optional<uint32_t> password_rounds;
  // The VKK wrapped with the user's password by the tpm.
  // Must be set.
  std::optional<brillo::SecureBlob> tpm_key;
  // Optional, served as a TPM identity, useful when checking if the TPM is
  // the same one sealed the tpm_key.
  std::optional<brillo::SecureBlob> tpm_public_key_hash;
};

struct TpmBoundToPcrAuthBlockState {
  // Marks if the password is run through scrypt before going to the TPM.
  bool scrypt_derived = false;
  // The salt used to bind to the TPM.
  std::optional<brillo::SecureBlob> salt;
  // The VKK wrapped with the user's password by the tpm.
  std::optional<brillo::SecureBlob> tpm_key;
  // Same as tpm_key, but extends the PCR to only allow one user until reboot.
  std::optional<brillo::SecureBlob> extended_tpm_key;
  // Optional, served as a TPM identity, useful when checking if the TPM is
  // the same one sealed the tpm_key.
  std::optional<brillo::SecureBlob> tpm_public_key_hash;
};

struct PinWeaverAuthBlockState {
  // The label for the credential in the LE hash tree.
  std::optional<uint64_t> le_label;
  // The salt used to first scrypt the user input.
  std::optional<brillo::SecureBlob> salt;
  // The IV used to derive the chaps key.
  std::optional<brillo::SecureBlob> chaps_iv;
  // The IV used to derive the file encryption key.
  // TODO(b/204202689): rename fek_iv to vkk_iv.
  std::optional<brillo::SecureBlob> fek_iv;
};

// This is a unique AuthBlockState for backwards compatibility. libscrypt puts
// the metadata, such as IV and salt, into the header of the encrypted
// buffer. Thus this is the only auth block state to pass wrapped secrets. See
// the LibScryptCompatAuthBlock header for a full explanation.
struct LibScryptCompatAuthBlockState {
  // The wrapped filesystem keys.
  // This is for in memory data holding only and will not be serialized.
  std::optional<brillo::SecureBlob> wrapped_keyset;
  // The wrapped chaps keys.
  // This is for in memory data holding only and will not be serialized.
  std::optional<brillo::SecureBlob> wrapped_chaps_key;
  // The wrapped reset seed keys.
  // This is for in memory data holding only and will not be serialized.
  std::optional<brillo::SecureBlob> wrapped_reset_seed;
  // The random salt.
  // TODO(b/198394243): We should remove it because it's not actually used.
  std::optional<brillo::SecureBlob> salt;
};

struct ChallengeCredentialAuthBlockState {
  struct LibScryptCompatAuthBlockState scrypt_state;
  std::optional<structure::SignatureChallengeInfo> keyset_challenge_info;
};

struct DoubleWrappedCompatAuthBlockState {
  struct LibScryptCompatAuthBlockState scrypt_state;
  struct TpmNotBoundToPcrAuthBlockState tpm_state;
};

struct CryptohomeRecoveryAuthBlockState {
  // HSM Payload is created at onboarding and contains all the data that are
  // persisted on a chromebook and will be eventually used for recovery,
  // serialized to CBOR.
  std::optional<brillo::SecureBlob> hsm_payload;
  // The salt used to first scrypt the user input.
  std::optional<brillo::SecureBlob> salt;
  // Secret share of the destination (plaintext).
  // TODO(b/184924489): store encrypted destination share.
  std::optional<brillo::SecureBlob> plaintext_destination_share;
  // Channel keys that will be used for secure communication during recovery.
  // TODO(b/196192089): store encrypted keys.
  std::optional<brillo::SecureBlob> channel_pub_key;
  std::optional<brillo::SecureBlob> channel_priv_key;
};

struct TpmEccAuthBlockState {
  // The salt used to derive the user input with scrypt.
  std::optional<brillo::SecureBlob> salt;
  // The IV to decrypt EVK.
  std::optional<brillo::SecureBlob> vkk_iv;
  // The number of rounds the auth value generating process is called.
  std::optional<uint32_t> auth_value_rounds;
  // HVKKM: Hardware Vault Keyset Key Material.
  // SVKKM: Software Vault Keyset Key Material.
  // We would use HVKKM and SVKKM to derive the VKK.
  // The HVKKM are encrypted with the user's password, TPM, and binds to empty
  // current user state.
  std::optional<brillo::SecureBlob> sealed_hvkkm;
  // Same as |sealed_hvkkm|, but extends the current user state to the specific
  // user.
  std::optional<brillo::SecureBlob> extended_sealed_hvkkm;
  // A check if this is the same TPM that wrapped the credential.
  std::optional<brillo::SecureBlob> tpm_public_key_hash;
  // The wrapped reset seed to reset LE credentials.
  std::optional<brillo::SecureBlob> wrapped_reset_seed;
};

struct AuthBlockState {
  // Returns an Flatbuffer offset which can be added to other Flatbuffers
  // tables. Returns a zero offset for errors since AuthBlockState
  // shall never be an empty table. Zero offset can be checked by IsNull().
  flatbuffers::Offset<SerializedAuthBlockState> SerializeToOffset(
      flatbuffers::FlatBufferBuilder* builder) const;

  // Returns an AuthBlockState Flatbuffer serialized to a SecureBlob.
  std::optional<brillo::SecureBlob> Serialize() const;

  std::variant<std::monostate,
               TpmNotBoundToPcrAuthBlockState,
               TpmBoundToPcrAuthBlockState,
               PinWeaverAuthBlockState,
               LibScryptCompatAuthBlockState,
               ChallengeCredentialAuthBlockState,
               DoubleWrappedCompatAuthBlockState,
               CryptohomeRecoveryAuthBlockState,
               TpmEccAuthBlockState>
      state;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_AUTH_BLOCK_STATE_H_
