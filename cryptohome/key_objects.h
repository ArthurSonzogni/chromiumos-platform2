// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_KEY_OBJECTS_H_
#define CRYPTOHOME_KEY_OBJECTS_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <brillo/secure_blob.h>

#include "cryptohome/cryptorecovery/cryptorecovery.pb.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {

// Data required for Cryptohome Recovery flow.
// For creation of the recovery key, `mediator_pub_key` field should be set.
// For derivation of the recovery key, `epoch_pub_key`, `ephemeral_pub_key`,
// `recovery_response` fields should be set.
struct CryptohomeRecoveryAuthInput {
  // Public key of the mediator for Cryptohome recovery flow.
  std::optional<brillo::SecureBlob> mediator_pub_key;
  // An epoch response received from Recovery Mediator service containing epoch
  // beacon value for Cryptohome recovery flow.
  std::optional<cryptorecovery::CryptoRecoveryEpochResponse> epoch_response;
  // Ephemeral public key for Cryptohome recovery flow.
  std::optional<brillo::SecureBlob> ephemeral_pub_key;
  // A response received from Recovery Mediator service and used by Cryptohome
  // recovery flow to derive the wrapping keys.
  std::optional<cryptorecovery::CryptoRecoveryRpcResponse> recovery_response;
};

// Data required for Challenge Credential flow.
struct ChallengeCredentialAuthInput {
  // DER-encoded blob of the X.509 Subject Public Key Info.
  brillo::Blob public_key_spki_der;
  // Supported signature algorithms, in the order of preference
  // (starting from the most preferred). Absence of this field
  // denotes that the key cannot be used for signing.
  std::vector<structure::ChallengeSignatureAlgorithm>
      challenge_signature_algorithms;
};

struct AuthInput {
  // The user input, such as password.
  std::optional<brillo::SecureBlob> user_input;
  // Whether or not the PCR is extended, this is usually false.
  std::optional<bool> locked_to_single_user;
  // The obfuscated username.
  std::optional<std::string> obfuscated_username;
  // A generated reset secret to unlock a rate limited credential.
  std::optional<brillo::SecureBlob> reset_secret;
  // Data required for Cryptohome Recovery flow.
  std::optional<CryptohomeRecoveryAuthInput> cryptohome_recovery_auth_input;
  // Data required for Challenge Credential flow.
  std::optional<ChallengeCredentialAuthInput> challenge_credential_auth_input;
};

// LibScrypt requires a salt to be passed from Create() into the encryption
// phase, so this struct has an optional salt.
class LibScryptCompatKeyObjects {
 public:
  // This class is never usable for encryption without a salt.
  explicit LibScryptCompatKeyObjects(brillo::SecureBlob derived_key)
      : derived_key_(derived_key), salt_(std::nullopt) {}

  LibScryptCompatKeyObjects(brillo::SecureBlob derived_key,
                            brillo::SecureBlob salt)
      : derived_key_(derived_key), salt_(salt) {}

  // Prohibit copy/move/assignment.
  LibScryptCompatKeyObjects(const LibScryptCompatKeyObjects&) = delete;
  LibScryptCompatKeyObjects(const LibScryptCompatKeyObjects&&) = delete;
  LibScryptCompatKeyObjects& operator=(const LibScryptCompatKeyObjects&) =
      delete;
  LibScryptCompatKeyObjects& operator=(const LibScryptCompatKeyObjects&&) =
      delete;

  // Access the derived key.
  brillo::SecureBlob derived_key();

  // Access the salt. The salt isn't used for decryption, so this only returns
  // the salt if the object is safe to used for encryption. Once accessed, the
  // salt is cleared and the class is no longer usable for encryption.
  brillo::SecureBlob ConsumeSalt();

 private:
  // The scrypt derived key which must always be present.
  const brillo::SecureBlob derived_key_;
  // The salt which only is passed out in the Create() flow.
  std::optional<brillo::SecureBlob> salt_;
};

// This struct is populated by the various authentication methods, with the
// secrets derived from the user input.
struct KeyBlobs {
  // Derives a secret used for wrapping the UserSecretStash main key. This
  // secret is not returned by auth blocks directly, but rather calculated as a
  // KDF of their output, allowing for adding new derived keys in the future.
  std::optional<brillo::SecureBlob> DeriveUssCredentialSecret() const;

  // The file encryption key. TODO(b/216474361): Rename to reflect this value is
  // used for deriving various values and not only for vault keysets, and add a
  // getter that returns the VKK.
  std::optional<brillo::SecureBlob> vkk_key;
  // The file encryption IV.
  std::optional<brillo::SecureBlob> vkk_iv;
  // The IV to use with the chaps key.
  std::optional<brillo::SecureBlob> chaps_iv;
  // The reset secret used for LE credentials.
  std::optional<brillo::SecureBlob> reset_secret;

  // The following fields are for libscrypt compatibility. They must be
  // unique_ptr's as the libscrypt keys cannot safely be re-used for multiple
  // encryption operations, so these are destroyed upon use.
  std::unique_ptr<LibScryptCompatKeyObjects> scrypt_key;
  // The key for scrypt wrapped chaps key.
  std::unique_ptr<LibScryptCompatKeyObjects> chaps_scrypt_key;
  // The scrypt wrapped reset seed.
  std::unique_ptr<LibScryptCompatKeyObjects> scrypt_wrapped_reset_seed_key;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_KEY_OBJECTS_H_
