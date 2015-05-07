// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ATTESTATION_COMMON_CRYPTO_UTILITY_H_
#define ATTESTATION_COMMON_CRYPTO_UTILITY_H_

#include <string>

#include "attestation/common/common.pb.h"

namespace attestation {

// A class which provides helpers for cryptography-related tasks.
class CryptoUtility {
 public:
  virtual ~CryptoUtility() = default;

  // Generates |num_bytes| of |random_data|. Returns true on success.
  virtual bool GetRandom(size_t num_bytes, std::string* random_data) const = 0;

  // Creates a random |aes_key| and seals it to the TPM's PCR0, producing a
  // |sealed_key|. Returns true on success.
  virtual bool CreateSealedKey(std::string* aes_key,
                               std::string* sealed_key) = 0;

  // Encrypts the given |data| using the |aes_key|. The |sealed_key| will be
  // embedded in the |encrypted_data| to assist with decryption. It can be
  // extracted from the |encrypted_data| using UnsealKey(). Returns true on
  // success.
  virtual bool EncryptData(const std::string& data,
                           const std::string& aes_key,
                           const std::string& sealed_key,
                           std::string* encrypted_data) = 0;

  // Extracts and unseals the |aes_key| from the |sealed_key| embedded in
  // the given |encrypted_data|. The |sealed_key| is also provided as an output
  // so callers can make subsequent calls to EncryptData() with the same key.
  // Returns true on success.
  virtual bool UnsealKey(const std::string& encrypted_data,
                         std::string* aes_key,
                         std::string* sealed_key) = 0;

  // Decrypts |encrypted_data| using |aes_key|, producing the decrypted |data|.
  // Returns true on success.
  virtual bool DecryptData(const std::string& encrypted_data,
                           const std::string& aes_key,
                           std::string* data) = 0;

  // Convert |public_key| from PKCS #1 RSAPublicKey to X.509
  // SubjectPublicKeyInfo. On success returns true and provides the
  // |public_key_info|.
  virtual bool GetRSASubjectPublicKeyInfo(const std::string& public_key,
                                          std::string* public_key_info) = 0;

  // Convert |public_key_info| from X.509 SubjectPublicKeyInfo to PKCS #1
  // RSAPublicKey. On success returns true and provides the |public_key|.
  virtual bool GetRSAPublicKey(const std::string& public_key_info,
                               std::string* public_key) = 0;

  // Encrypts a |credential| in a format compatible with TPM attestation key
  // activation. The |ek_public_key_info| must be provided in X.509
  // SubjectPublicKeyInfo format and the |aik_public_key| must be provided in
  // TPM_PUBKEY format.
  virtual bool EncryptIdentityCredential(
      const std::string& credential,
      const std::string& ek_public_key_info,
      const std::string& aik_public_key,
      EncryptedIdentityCredential* encrypted) = 0;
};

}  // namespace attestation

#endif  // ATTESTATION_COMMON_CRYPTO_UTILITY_H_
