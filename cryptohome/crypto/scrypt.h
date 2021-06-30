// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTO_SCRYPT_H_
#define CRYPTOHOME_CRYPTO_SCRYPT_H_

#include <string>
#include <vector>

#include <openssl/bn.h>
#include <openssl/evp.h>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <brillo/secure_blob.h>

#include "cryptohome/attestation.pb.h"
#include "cryptohome/crypto_error.h"

namespace cryptohome {

extern const unsigned int kDefaultPasswordRounds;
extern const unsigned int kDefaultLegacyPasswordRounds;
extern const unsigned int kDefaultPassBlobSize;
extern const unsigned int kScryptMetadataSize;
extern const unsigned int kScryptMaxMem;
extern const double kScryptMaxEncryptTime;
extern const int kTpmDecryptMaxRetries;

// A struct wrapping the scrypt parameters, with the default production
// parameters set.
struct ScryptParameters {
  // N is the work factor. Scrypt stores N sequential hash results in RAM,
  // randomizes their order, and XORs them.
  int n_factor = 16384;
  // The r factor iterates the hash function 2r times, so that memory and CPU
  // consumption grow with r.
  uint32_t r_factor = 8;
  // P is the parallelization factor.
  uint32_t p_factor = 1;
};

extern const ScryptParameters kDefaultScryptParams;
extern const ScryptParameters kTestScryptParams;

// Derives secrets and other values from User Passkey.
//
// Parameters
//   passkey - The User Passkey, from which to derive the secrets.
//   salt - The salt used when deriving the secrets.
//   gen_secrets (IN-OUT) - Vector containing resulting secrets.
//                          The caller allocates each blob in |gen_secrets|
//                          to the appropriate (non-empty) size.
//
bool DeriveSecretsScrypt(const brillo::SecureBlob& passkey,
                         const brillo::SecureBlob& salt,
                         std::vector<brillo::SecureBlob*> gen_secrets);

// |passkey| - The User Passkey, from which to derive the secrets.
// |salt| - The salt used when deriving the secrets.
// |work_factor| - The work factor passed to scrypt.
// |block_size| - The block size passed to scrypt.
// |parallel_factor| - The parallel factor passed to scrypt.
// |result| - The blob, allocated by the caller to the correct size,
//            containing the result secret.
//
//  Returns true on success.
bool Scrypt(const brillo::SecureBlob& passkey,
            const brillo::SecureBlob& salt,
            int work_factor,
            int block_size,
            int parallel_factor,
            brillo::SecureBlob* result);

// Encrypt a provided blob using libscrypt, which sets up a header, derives
// the keys, encrypts, and HMACs.
//
// The parameters are as follows:
// - blob: Data blob to be encrypted.
// - key_source: User passphrase key used for encryption.
// - max_encrypt_time: A max encryption time which can specified.
// - wrapped_blob: Pointer to blob where encrypted data is stored.
//
// Returns true on success, and false on failure.
bool DeprecatedEncryptScryptBlob(const brillo::SecureBlob& blob,
                                 const brillo::SecureBlob& key_source,
                                 brillo::SecureBlob* wrapped_blob);

// Companion decryption function for DeprecatedEncryptScryptBlob().
// This decrypts the data blobs which were encrypted using
// DeprecatedEncryptScryptBlob().
//
// Returns true on success. On failure, false is returned, and
// |error| is set with the appropriate error code.
bool DeprecatedDecryptScryptBlob(const brillo::SecureBlob& wrapped_blob,
                                 const brillo::SecureBlob& key,
                                 brillo::SecureBlob* blob,
                                 CryptoError* error);

// This verifies that the default scrypt params are used in production.
void AssertProductionScryptParams();

// This updates the global scrypt testing parameters.
void SetScryptTestingParams();

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTO_SCRYPT_H_
