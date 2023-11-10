// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_CRYPTO_SECURE_BOX_H_
#define LIBHWSEC_FOUNDATION_CRYPTO_SECURE_BOX_H_

#include <optional>

#include <brillo/secure_blob.h>

#include "libhwsec-foundation/hwsec-foundation_export.h"

namespace hwsec_foundation {
// A C++ implementation for go/securebox2.
namespace secure_box {
struct KeyPair {
  // Uncompressed format of an EC P-256 public key: 1 byte of header (always set
  // to 4) + 32 bytes of big-endian encoding of the X coordinate of the public
  // key point + 32 bytes of that of the Y coordinate.
  brillo::Blob public_key;
  // 32 bytes of big-endian encoding of the private key scalar + 65 bytes of
  // |public_key|.
  // The public key is concatenated such that when the server side decrypts the
  // encrypted encoded private key, it contains the whole key pair.
  brillo::SecureBlob private_key;
};

// Derive a SecureBox P-256 EC key pair from the given seed using the FIPS
// 186-5 "ECDSA Key Pair Generation by Extra Random Bits" method. The
// recommendation for minimum entropy of the seed is 352 bits for the P-256
// curve.
std::optional<KeyPair> HWSEC_FOUNDATION_EXPORT
DeriveKeyPairFromSeed(const brillo::SecureBlob& seed);

// Encrypting and authenticating |payload| with |their_public_key| and
// |shared_key|, with |header| authenticated together with |payload| but not
// encrypted. Returns the encrypted and authenticated blob on success.
// |their_public_key|: The P-256 public key of the recipient. It must be a
// blob of size 0 or 65 bytes. If it's an empty string, only
// symmetric encryption is used.
// |shared_secret|: A SecureBlob of arbitrary size that contains a shared
// secret between the sender and the recipient. It can be of size zero.
// |header|: A Blob of arbitrary size that will be authenticated
// together with |payload|, but not encrypted. It can be of size zero.
// |payload|: A SecureBlob that needs to be encrypted and authenticated. It
// can be of size zero.
std::optional<brillo::Blob> HWSEC_FOUNDATION_EXPORT
Encrypt(const brillo::Blob& their_public_key,
        const brillo::SecureBlob& shared_secret,
        const brillo::Blob& header,
        const brillo::SecureBlob& payload);

}  // namespace secure_box
}  // namespace hwsec_foundation

#endif  // LIBHWSEC_FOUNDATION_CRYPTO_SECURE_BOX_H_
