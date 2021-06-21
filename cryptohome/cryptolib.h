// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTOLIB_H_
#define CRYPTOHOME_CRYPTOLIB_H_

#include <string>
#include <vector>

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <brillo/secure_blob.h>

#include "cryptohome/attestation.pb.h"
#include "cryptohome/crypto_error.h"

namespace cryptohome {

extern const unsigned int kDefaultPasswordRounds;
extern const unsigned int kWellKnownExponent;
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

class CryptoLib {
 public:
  CryptoLib();
  ~CryptoLib();

  static bool CreateRsaKey(size_t bits,
                           brillo::SecureBlob* n,
                           brillo::SecureBlob* p);

  // Fills out all fields related to the RSA private key information, given the
  // public key information provided via |rsa| and the secret prime via
  // |secret_prime|.
  static bool FillRsaPrivateKeyFromSecretPrime(
      const brillo::SecureBlob& secret_prime, RSA* rsa);

  // Obscure an RSA message by encrypting part of it.
  // The TPM could _in theory_ produce an RSA message (as a response from Bind)
  // that contains a header of a known format. If it did, and we encrypted the
  // whole message with a passphrase-derived AES key, then one could test
  // passphrase correctness by trial-decrypting the header. Instead, encrypt
  // only part of the message, and hope the part we encrypt is part of the RSA
  // message.
  //
  // In practice, this never makes any difference, because no TPM does that; the
  // result is always a bare PKCS1.5-padded RSA-encrypted message, which is
  // (as far as the author knows, although no proof is known) indistinguishable
  // from random data, and hence the attack this would protect against is
  // infeasible.
  static bool ObscureRSAMessage(const brillo::SecureBlob& plaintext,
                                const brillo::SecureBlob& key,
                                brillo::SecureBlob* ciphertext);
  static bool UnobscureRSAMessage(const brillo::SecureBlob& ciphertext,
                                  const brillo::SecureBlob& key,
                                  brillo::SecureBlob* plaintext);

  // Encrypts data using the RSA OAEP scheme with the SHA-1 hash function, the
  // MGF1 mask function, and an empty label parameter.
  static bool RsaOaepEncrypt(const brillo::SecureBlob& plaintext,
                             RSA* key,
                             brillo::Blob* ciphertext);
  // Decrypts the data encrypted with RSA OAEP with the SHA-1 hash function, the
  // MGF1 mask function, and the label parameter equal to |oaep_label|.
  static bool RsaOaepDecrypt(const brillo::SecureBlob& ciphertext,
                             const brillo::SecureBlob& oaep_label,
                             RSA* key,
                             brillo::SecureBlob* plaintext);

  // Encrypts data using the TPM_ES_RSAESOAEP_SHA1_MGF1 scheme.
  //
  // Parameters
  //   key - The RSA public key.
  //   input - The data to be encrypted.
  //   output - The encrypted data.
  static bool TpmCompatibleOAEPEncrypt(RSA* key,
                                       const brillo::SecureBlob& input,
                                       brillo::SecureBlob* output);

  // Checks an RSA key modulus for the ROCA fingerprint (i.e. whether the RSA
  // modulus has a discrete logarithm modulus small primes). See research paper
  // for details: https://crocs.fi.muni.cz/public/papers/rsa_ccs17
  static bool TestRocaVulnerable(const BIGNUM* rsa_modulus);

  // Derives secrets and other values from User Passkey.
  //
  // Parameters
  //   passkey - The User Passkey, from which to derive the secrets.
  //   salt - The salt used when deriving the secrets.
  //   gen_secrets (IN-OUT) - Vector containing resulting secrets.
  //                          The caller allocates each blob in |gen_secrets|
  //                          to the appropriate (non-empty) size.
  //
  static bool DeriveSecretsScrypt(const brillo::SecureBlob& passkey,
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
  static bool Scrypt(const brillo::SecureBlob& passkey,
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
  static bool DeprecatedEncryptScryptBlob(const brillo::SecureBlob& blob,
                                          const brillo::SecureBlob& key_source,
                                          brillo::SecureBlob* wrapped_blob);

  // Companion decryption function for DeprecatedEncryptScryptBlob().
  // This decrypts the data blobs which were encrypted using
  // DeprecatedEncryptScryptBlob().
  //
  // Returns true on success. On failure, false is returned, and
  // |error| is set with the appropriate error code.
  static bool DeprecatedDecryptScryptBlob(
      const brillo::SecureBlob& wrapped_blob,
      const brillo::SecureBlob& key,
      brillo::SecureBlob* blob,
      CryptoError* error);

  // This verifies that the default scrypt params are used in production.
  static void AssertProductionScryptParams();

  // This updates the global static scrypt testing parameters.
  static void SetScryptTestingParams();

  // Global static override-able for testing.
  static ScryptParameters gScryptParams;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTOLIB_H_
