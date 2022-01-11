// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTO_AES_H_
#define CRYPTOHOME_CRYPTO_AES_H_

#include <optional>

#include <brillo/secure_blob.h>

namespace cryptohome {

extern const unsigned int kAesBlockSize;
extern const unsigned int kAesGcmTagSize;
extern const unsigned int kAesGcmIVSize;
extern const unsigned int kAesGcm256KeySize;
extern const unsigned int kDefaultAesKeySize;

enum class PaddingScheme {
  kPaddingNone = 0,
  // Also called PKCS padding.
  // See http://tools.ietf.org/html/rfc5652#section-6.3.
  kPaddingStandard = 1,
  kPaddingCryptohomeDefaultDeprecated = 2,
};

enum class BlockMode {
  kEcb = 1,
  kCbc = 2,
  kCtr = 3,
};

// Returns the block size of the AES-256 cipher.
size_t GetAesBlockSize();

// Derives a key and IV from the password.
//
// Parameters
//   passkey - The data to derive the key from.
//   salt - Used as a salt in the derivation. Must have `PKCS5_SALT_LEN` size.
//   rounds - The iteration count to use.
//            Increasing the `rounds` parameter slows down the algorithm which
//            makes it harder for an attacker to perform a brute force attack
//            using a large number of candidate passwords.
//    key - On success, the derived key.
//    iv - On success, the derived iv.
bool PasskeyToAesKey(const brillo::SecureBlob& passkey,
                     const brillo::SecureBlob& salt,
                     unsigned int rounds,
                     brillo::SecureBlob* key,
                     brillo::SecureBlob* iv);

// AES encrypts the plain text data using the specified key and IV.  This
// method uses custom padding and is not inter-operable with other crypto
// systems.  The encrypted data can be decrypted with AesDecrypt.
//
// Parameters
//   plaintext - The plain text data to encrypt
//   key - The AES key to use
//   iv - The initialization vector to use
//   ciphertext - On success, the encrypted data
bool AesEncryptDeprecated(const brillo::SecureBlob& plaintext,
                          const brillo::SecureBlob& key,
                          const brillo::SecureBlob& iv,
                          brillo::SecureBlob* ciphertext);

// Decrypts data encrypted with AesEncrypt.
//
// Parameters
//   wrapped - The blob containing the encrypted data
//   key - The AES key to use in decryption
//   iv - The initialization vector to use
//   plaintext - The unwrapped (decrypted) data
bool AesDecryptDeprecated(const brillo::SecureBlob& ciphertext,
                          const brillo::SecureBlob& key,
                          const brillo::SecureBlob& iv,
                          brillo::SecureBlob* plaintext);

// AES-GCM decrypts the |ciphertext| using the |key| and |iv|. |key| must be
// 256-bits and |iv| must be 96-bits.
//
// Parameters:
//   ciphertext - The encrypted data.
//   ad - (optional) additional authenticated data.
//   tag - The integrity check of the data.
//   key - The key to decrypt with.
//   iv - The IV to decrypt with.
//   plaintext - On success, the decrypted data.
bool AesGcmDecrypt(const brillo::SecureBlob& ciphertext,
                   const std::optional<brillo::SecureBlob>& ad,
                   const brillo::SecureBlob& tag,
                   const brillo::SecureBlob& key,
                   const brillo::SecureBlob& iv,
                   brillo::SecureBlob* plaintext);

// AES-GCM encrypts the |plaintext| using the |key|. A random initialization
// vector is created and retuned in |iv|. The encrypted data can be decrypted
// with AesGcmDecrypt. |key| must be 256-bits.
//
// Parameters:
//   plaintext - The plain text data to encrypt.
//   ad - (optional) additional authenticated data
//   key - The AES key to use.
//   iv - The initialization vector generated randomly.
//   tag - On success, the integrity tag of the data.
//   ciphertext - On success, the encrypted data.
bool AesGcmEncrypt(const brillo::SecureBlob& plaintext,
                   const std::optional<brillo::SecureBlob>& ad,
                   const brillo::SecureBlob& key,
                   brillo::SecureBlob* iv,
                   brillo::SecureBlob* tag,
                   brillo::SecureBlob* ciphertext);

// Same as AesDecrypt, but allows using either CBC or ECB
bool AesDecryptSpecifyBlockMode(const brillo::SecureBlob& ciphertext,
                                unsigned int start,
                                unsigned int count,
                                const brillo::SecureBlob& key,
                                const brillo::SecureBlob& iv,
                                PaddingScheme padding,
                                BlockMode mode,
                                brillo::SecureBlob* plaintext);

// Same as AesEncrypt, but allows using either CBC or ECB
bool AesEncryptSpecifyBlockMode(const brillo::SecureBlob& plaintext,
                                unsigned int start,
                                unsigned int count,
                                const brillo::SecureBlob& key,
                                const brillo::SecureBlob& iv,
                                PaddingScheme padding,
                                BlockMode mode,
                                brillo::SecureBlob* ciphertext);
}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTO_AES_H_
