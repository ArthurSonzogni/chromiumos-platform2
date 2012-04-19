// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHAPS_TPM_UTILITY_H
#define CHAPS_TPM_UTILITY_H

#include <string>

namespace chaps {

// TPMUtility is a high-level interface to TPM services. In practice, only a
// single instance of this class is necessary to provide TPM services across
// multiple logical tokens and sessions.
class TPMUtility {
 public:
  virtual ~TPMUtility() {}
  // Performs initialization tasks including the loading of the storage root key
  // (SRK). This may be called multiple times.
  // Returns true on success.
  virtual bool Init() = 0;

  // Authenticates a user by decrypting the user's master key with the user's
  // authorization key.
  //   auth_data - The user's authorization data (which is derived from the
  //               the user's password).
  //   auth_key_blob - The authorization key blob as provided by the TPM when
  //                   the key was generated.
  //   encrypted_master_key - The master key encrypted with the authorization
  //                          key.
  //   master_key - Will be populated with the decrypted master key.
  // Returns true on success.
  virtual bool Authenticate(int slot_id,
                            const std::string& auth_data,
                            const std::string& auth_key_blob,
                            const std::string& encrypted_master_key,
                            std::string* master_key) = 0;

  // Changes authorization data for a user's authorization key. Returns true on
  // success.
  virtual bool ChangeAuthData(int slot_id,
                              const std::string& old_auth_data,
                              const std::string& new_auth_data,
                              const std::string& old_auth_key_blob,
                              std::string* new_auth_key_blob) = 0;

  // Provides hardware-generated random data. Returns true on success.
  virtual bool GenerateRandom(int num_bytes, std::string* random_data) = 0;

  // Adds entropy to the hardware random number generator. This is like seeding
  // the generator except the provided entropy is mixed with existing state and
  // the resulting random numbers generated are not deterministic. Returns true
  // on success.
  virtual bool StirRandom(const std::string& entropy_data) = 0;

  // Generates an RSA key pair in the TPM and wraps it with the SRK. The key
  // type will be set to TSS_KEY_TYPE_LEGACY.
  //   slot - The slot associated with this key.
  //   modulus_bits - The size of the key to be generated (usually 2048).
  //   public_exponent - The RSA public exponent (usually {1, 0, 1} which is
  //                     65537).
  //   auth_data - Authorization data which will be associated with the new key.
  //   key_blob - Will be populated with the wrapped key blob as provided by the
  //              TPM. This should be saved so the key can be loaded again
  //              in the future.
  //   key_handle - A handle to the new key. This will be valid until keys are
  //                unloaded for the given slot.
  // Returns true on success.
  virtual bool GenerateKey(int slot,
                           int modulus_bits,
                           const std::string& public_exponent,
                           const std::string& auth_data,
                           std::string* key_blob,
                           int* key_handle) = 0;

  // Retrieves the public components of an RSA key pair. Returns true on
  // success.
  virtual bool GetPublicKey(int key_handle,
                            std::string* public_exponent,
                            std::string* modulus) = 0;

  // Wraps an RSA key pair with the SRK. The key type will be set to
  // TSS_KEY_TYPE_LEGACY.
  //   slot - The slot associated with this key.
  //   public_exponent - The RSA public exponent (e).
  //   modulus - The RSA modulus (n).
  //   prime_factor - One of the prime factors of the modulus (p or q).
  //   auth_data - Authorization data which will be associated with the new key.
  //   key_blob - Will be populated with the wrapped key blob as provided by the
  //              TPM. This should be saved so the key can be loaded again
  //              in the future.
  //   key_handle - A handle to the new key. This will be valid until keys are
  //                unloaded for the given slot.
  // Returns true on success.
  virtual bool WrapKey(int slot,
                       const std::string& public_exponent,
                       const std::string& modulus,
                       const std::string& prime_factor,
                       const std::string& auth_data,
                       std::string* key_blob,
                       int* key_handle) = 0;

  // Loads a key by blob into the TPM.
  //   slot - The slot associated with this key.
  //   key_blob - The key blob as provided by GenerateKey or WrapKey.
  //   auth_data - Authorization data for the key.
  //   key_handle - A handle to the loaded key. This will be valid until keys
  //                are unloaded for the given slot.
  // Returns true on success.
  virtual bool LoadKey(int slot,
                       const std::string& key_blob,
                       const std::string& auth_data,
                       int* key_handle) = 0;

  // Loads a key by blob into the TPM that has a parent key that is not the SRK.
  //   slot - The slot associated with this key.
  //   key_blob - The key blob as provided by GenerateKey or WrapKey.
  //   auth_data - Authorization data for the key.
  //   parent_key_handle - The key handle of the parent key.
  //   key_handle - A handle to the loaded key. This will be valid until keys
  //                are unloaded for the given slot.
  // Returns true on success.
  virtual bool LoadKeyWithParent(int slot,
                                 const std::string& key_blob,
                                 const std::string& auth_data,
                                 int parent_key_handle,
                                 int* key_handle) = 0;

  // Unloads all keys loaded for a particular slot. All key handles for the
  // given slot will not be valid after this method returns.
  virtual void UnloadKeysForSlot(int slot) = 0;

  // Performs a 'bind' operation using the TSS_ES_RSAESPKCSV15 scheme. This
  // effectively performs PKCS #1 v1.5 RSA encryption (using PKCS #1 'type 2'
  // padding).
  //   key_handle - The key handle, as provided by LoadKey, WrapKey, or
  //                GenerateKey.
  //   input - Data to be encrypted. The length of this data must not exceed
  //           'N - 11' where N is the length in bytes of the RSA key modulus.
  //   output - The encrypted data. The length will always match the length of
  //            the RSA key modulus.
  // Returns true on success.
  virtual bool Bind(int key_handle,
                    const std::string& input,
                    std::string* output) = 0;

  // Performs a 'unbind' operation using the TSS_ES_RSAESPKCSV15 scheme. This
  // effectively performs PKCS #1 v1.5 RSA decryption (using PKCS #1 'type 2'
  // padding).
  //   key_handle - The key handle, as provided by LoadKey, WrapKey or
  //                GenerateKey.
  //   input - Data to be encrypted. The length of this data must not exceed
  //           'N - 11' where N is the length in bytes of the RSA key modulus.
  //   output - The encrypted data. The length will always match the length of
  //            the RSA key modulus.
  // Returns true on success.
  virtual bool Unbind(int key_handle,
                      const std::string& input,
                      std::string* output) = 0;

  // Generates a digital signature using the TSS_SS_RSASSAPKCS1V15_DER scheme.
  //   key_handle - The key handle, as provided by LoadKey, WrapKey or
  //                GenerateKey.
  //   input - Must be a DER encoding of the DigestInfo value (see
  //           PKCS #1 v.2.1: 9.2).
  //   signature - Receives the generated signature. The signature length will
  //               always match the length of the RSA key modulus.
  // Returns true on success.
  virtual bool Sign(int key_handle,
                    const std::string& input,
                    std::string* signature) = 0;

  // Verifies a digital signature using the TSS_SS_RSASSAPKCS1V15_DER scheme.
  //   key_handle - The key handle, as provided by LoadKey, WrapKey, or
  //                GenerateKey.
  //   input - Must be a DER encoding of the DigestInfo value (see
  //           PKCS #1 v.2.1: 9.2).
  //   signature - The digital signature to be verified.
  // Returns true if the signature is valid.
  virtual bool Verify(int key_handle,
                      const std::string& input,
                      const std::string& signature) = 0;
};

}  // namespace

#endif  // CHAPS_TPM_UTILITY_H
