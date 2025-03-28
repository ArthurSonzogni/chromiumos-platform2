// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_KEY_OBJECTS_H_
#define CRYPTOHOME_KEY_OBJECTS_H_

#include <optional>
#include <string>
#include <vector>

#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/recoverable_key_store.pb.h>

#include "cryptohome/cryptorecovery/cryptorecovery.pb.h"
#include "cryptohome/cryptorecovery/recovery_crypto_util.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/flatbuffer_schemas/structures.h"
#include "cryptohome/recoverable_key_store/type.h"
#include "cryptohome/username.h"

namespace cryptohome {

struct CryptohomeRecoveryPrepareInput {
  // The recovery request metadata.
  cryptorecovery::RequestMetadata request_metadata;
  // The epoch response from the recovery mediator service.
  brillo::Blob epoch_response;
  // The auth block state for the specified factor.
  CryptohomeRecoveryAuthBlockState auth_block_state;
};

struct PrepareInput {
  // The obfuscated username.
  ObfuscatedUsername username;
  // If this is preparing for a recovery operation, this should contain the
  // necessary inputs to produce the recovery request.
  std::optional<CryptohomeRecoveryPrepareInput>
      cryptohome_recovery_prepare_input;
  // A generated reset secret to unlock a rate limited credential.
  std::optional<brillo::SecureBlob> reset_secret;
  // The PinWeaver leaf label of the rate-limiter.
  std::optional<uint64_t> rate_limiter_label;
  // Supported signature algorithms, in the order of preference
  // (starting from the most preferred).
  std::vector<SerializedChallengeSignatureAlgorithm>
      challenge_signature_algorithms;
};

struct CryptohomeRecoveryPrepareOutput {
  // The prepared recovery RPC request, to be sent to the recovery service.
  // Produced when preparing for recovery.
  cryptorecovery::CryptoRecoveryRpcRequest recovery_rpc_request;
  // The ephemeral public key associated with the request.
  brillo::Blob ephemeral_pub_key;
};

struct PrepareOutput {
  // Output of preparing for a cryptohome recovery flow.
  std::optional<CryptohomeRecoveryPrepareOutput>
      cryptohome_recovery_prepare_output;
};

// Data required for Cryptohome Recovery flow.
// - For creation of the recovery key, `mediator_pub_key` and
// `ensure_fresh_recovery_id` fields should be set.
// - For derivation of the recovery key, `epoch_pub_key`,
//   `ephemeral_pub_key`, `recovery_response`, `ledger_name`, `ledger_key_hash`,
//   `ledger_public_key` fields should be set.
struct CryptohomeRecoveryAuthInput {
  // Public key of the mediator for Cryptohome recovery flow.
  std::optional<brillo::Blob> mediator_pub_key;
  // Whether the recovery id should be rotated.
  bool ensure_fresh_recovery_id = true;

  // GaiaId of the owner of cryptohome to be recovered.
  std::string user_gaia_id;
  // Unique identifier generated on cryptohome creation.
  std::string device_user_id;
  // Serialized cryptorecovery::CryptoRecoveryEpochResponse.
  // An epoch response received from Recovery Mediator service containing epoch
  // beacon value for Cryptohome recovery flow.
  std::optional<brillo::Blob> epoch_response;
  // Ephemeral public key for Cryptohome recovery flow.
  std::optional<brillo::Blob> ephemeral_pub_key;
  // Serialized cryptorecovery::CryptoRecoveryRpcResponse.
  // A response received from Recovery Mediator service and used by Cryptohome
  // recovery flow to derive the wrapping keys.
  std::optional<brillo::Blob> recovery_response;

  // The ledger info from the chrome side and used by Cryptohome
  // recovery flow to determine which ledger is used:
  // Ledger's name.
  std::string ledger_name;
  // Ledger's public key hash.
  uint32_t ledger_key_hash;
  // Ledger's public key.
  std::optional<brillo::Blob> ledger_public_key;
};

// Data required for Challenge Credential flow.
struct ChallengeCredentialAuthInput {
  // Supported signature algorithms, in the order of preference
  // (starting from the most preferred). Absence of this field
  // denotes that the key cannot be used for signing.
  std::vector<SerializedChallengeSignatureAlgorithm>
      challenge_signature_algorithms;
  // Dbus service name used when generating a KeyChallengeService,
  // also used to create the ChallengeCredential AuthBlock.
  std::string dbus_service_name;
};

struct FingerprintAuthInput {
  // The secret from the biometrics auth stack bound to this AuthFactor.
  std::optional<brillo::SecureBlob> auth_secret;
  // Data required for legacy fingerprint migration flow.
  std::optional<std::string> legacy_record_id;
};

struct AuthInput {
  // The user input, such as password.
  std::optional<brillo::SecureBlob> user_input;
  // Whether or not the PCR is extended, this is usually false.
  std::optional<bool> locked_to_single_user;
  // The username accosiated with the running AuthSession.
  Username username;
  // The obfuscated username.
  std::optional<ObfuscatedUsername> obfuscated_username;
  // A generated reset secret to unlock a rate limited credential. This will be
  // used for USS.
  std::optional<brillo::SecureBlob> reset_secret;
  // reset_seed used to generate a reset secret.
  // This will be removed after full migration to USS.
  std::optional<brillo::SecureBlob> reset_seed;
  // reset_salt used to generate a reset secret.
  // This will be removed after full migration to USS.
  std::optional<brillo::Blob> reset_salt;
  // The PinWeaver leaf label of the rate-limiter.
  std::optional<uint64_t> rate_limiter_label;
  // security_domain_keys used to generate recoverable key stores.
  std::optional<SecurityDomainKeys> security_domain_keys;
  // Data required for Cryptohome Recovery flow.
  std::optional<CryptohomeRecoveryAuthInput> cryptohome_recovery_auth_input;
  // Data required for Challenge Credential flow.
  std::optional<ChallengeCredentialAuthInput> challenge_credential_auth_input;
  // Data required for Fingerprint flow.
  std::optional<FingerprintAuthInput> fingerprint_auth_input;
};

// This struct is populated by the various authentication methods, with the
// secrets derived from the user input.
struct KeyBlobs {
  // Derives a secret used for wrapping the UserSecretStash main key. This
  // secret is not returned by auth blocks directly, but rather calculated as a
  // KDF of their output, allowing for adding new derived keys in the future.
  CryptohomeStatusOr<brillo::SecureBlob> DeriveUssCredentialSecret() const;

  // The file encryption key. This value is also used for deriving various
  // values and not only for vault keysets.
  std::optional<brillo::SecureBlob> vkk_key;
  // The Scrypt chaps key. Used for ScryptAuthBlock for storing the chaps key.
  std::optional<brillo::SecureBlob> scrypt_chaps_key;
  // The Scrypt reset seed key. Used for ScryptAuthBlock for storing the reset
  // seed key.
  std::optional<brillo::SecureBlob> scrypt_reset_seed_key;

  // The file encryption IV.
  std::optional<brillo::Blob> vkk_iv;
  // The IV to use with the chaps key.
  std::optional<brillo::Blob> chaps_iv;
  // The reset secret used for LE credentials.
  std::optional<brillo::SecureBlob> reset_secret;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_KEY_OBJECTS_H_
