// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCK_STATE_H_
#define CRYPTOHOME_AUTH_BLOCK_STATE_H_

#include <absl/types/variant.h>
#include <base/optional.h>
#include <brillo/secure_blob.h>

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
  base::Optional<brillo::SecureBlob> salt;
  // The number of rounds key derivation is called.
  base::Optional<uint32_t> password_rounds;
  // The VKK wrapped with the user's password by the tpm.
  base::Optional<brillo::SecureBlob> tpm_key;
  // A check if this is the same TPM that wrapped the credential.
  base::Optional<brillo::SecureBlob> tpm_public_key_hash;
  // The wrapped reset seed to reset LE credentials.
  base::Optional<brillo::SecureBlob> wrapped_reset_seed;
};

struct TpmBoundToPcrAuthBlockState {
  // Marks if the password is run through scrypt before going to the TPM.
  bool scrypt_derived = false;
  // The salt used to bind to the TPM.
  base::Optional<brillo::SecureBlob> salt;
  // The VKK wrapped with the user's password by the tpm.
  base::Optional<brillo::SecureBlob> tpm_key;
  // Same as tpm_key, but extends the PCR to only allow one user until reboot.
  base::Optional<brillo::SecureBlob> extended_tpm_key;
  // A check if this is the same TPM that wrapped the credential.
  base::Optional<brillo::SecureBlob> tpm_public_key_hash;
  // The wrapped reset seed to reset LE credentials.
  base::Optional<brillo::SecureBlob> wrapped_reset_seed;
};

struct PinWeaverAuthBlockState {
  // The label for the credential in the LE hash tree.
  base::Optional<uint64_t> le_label;
  // The salt used to first scrypt the user input.
  base::Optional<brillo::SecureBlob> salt;
  // The IV used to derive the chaps key.
  base::Optional<brillo::SecureBlob> chaps_iv;
  // The IV used to derive the file encryption key.
  base::Optional<brillo::SecureBlob> fek_iv;
};

// This is a unique AuthBlockState for backwards compatibility. libscrypt puts
// the metadata, such as IV and salt, into the header of the encrypted
// buffer. Thus this is the only auth block state to pass wrapped secrets. See
// the LibScryptCompatAuthBlock header for a full explanation.
struct LibScryptCompatAuthBlockState {
  // The wrapped filesystem keys.
  base::Optional<brillo::SecureBlob> wrapped_keyset;
  // The wrapped chaps keys.
  base::Optional<brillo::SecureBlob> wrapped_chaps_key;
  // The wrapped reset seed keys.
  base::Optional<brillo::SecureBlob> wrapped_reset_seed;
  // The random salt.
  // TODO(b/198394243): We should remove it because it's not actually used.
  base::Optional<brillo::SecureBlob> salt;
};

struct ChallengeCredentialAuthBlockState {
  struct LibScryptCompatAuthBlockState scrypt_state;
};

struct DoubleWrappedCompatAuthBlockState {
  struct LibScryptCompatAuthBlockState scrypt_state;
  struct TpmNotBoundToPcrAuthBlockState tpm_state;
};

struct CryptohomeRecoveryAuthBlockState {
  // Contains encrypted mediator share and data required for decryption.
  struct EncryptedMediatorShare {
    // The integrity tag of the data generated during encryption of the
    // mediator share.
    base::Optional<brillo::SecureBlob> tag;
    // The initialization vector generated during encryption of the mediator
    // share.
    base::Optional<brillo::SecureBlob> iv;
    // Ephemeral key created during encryption of the mediator share.
    base::Optional<brillo::SecureBlob> ephemeral_pub_key;
    // Encrypted mediator share.
    base::Optional<brillo::SecureBlob> encrypted_data;
  };
  // Secret share of the mediator encrypted to the mediator public key.
  base::Optional<EncryptedMediatorShare> encrypted_mediator_share;
  // HSM Payload is created at onboarding and contains all the data that are
  // persisted on a chromebook and will be eventually used for recovery,
  // serialized to CBOR.
  base::Optional<brillo::SecureBlob> hsm_payload;
  // The salt used to first scrypt the user input.
  base::Optional<brillo::SecureBlob> salt;
  // Secret share of the destination (plaintext).
  // TODO(b/184924489): store encrypted destination share.
  base::Optional<brillo::SecureBlob> plaintext_destination_share;
  // Channel keys that will be used for secure communication during recovery.
  // TODO(b/196192089): store encrypted keys.
  base::Optional<brillo::SecureBlob> channel_pub_key;
  base::Optional<brillo::SecureBlob> channel_priv_key;
};

struct AuthBlockState {
  absl::variant<TpmNotBoundToPcrAuthBlockState,
                TpmBoundToPcrAuthBlockState,
                PinWeaverAuthBlockState,
                LibScryptCompatAuthBlockState,
                ChallengeCredentialAuthBlockState,
                DoubleWrappedCompatAuthBlockState,
                CryptohomeRecoveryAuthBlockState>
      state;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCK_STATE_H_
