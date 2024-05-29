// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_KEYMINT_CONTEXT_OPENSSL_UTILS_H_
#define ARC_KEYMINT_CONTEXT_OPENSSL_UTILS_H_

#include "absl/types/span.h"

#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <keymaster/android_keymaster_utils.h>
#include <keymaster/km_openssl/openssl_err.h>
#include <keymaster/km_openssl/openssl_utils.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/ossl_typ.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/rand.h>

#include <optional>
#include <string>
#include <vector>

// Exposes OpenSSL functionality through an API that is relevant to the ARC
// KeyMint context.

namespace arc::keymint::context {

// Accessible from tests.
constexpr size_t kIvSize = 12;
constexpr size_t kTagSize = 16;

// Authenticated encryption of |input| using AES-GCM-256 with |key| and
// |auth_data|.
//
// A 12-byte IV is randomly generated at every call and appended to the
// encrypted output.
//
// Returns std::nullopt if there's an error in the OpenSSL operation.
std::optional<brillo::Blob> Aes256GcmEncrypt(const brillo::SecureBlob& key,
                                             const brillo::Blob& auth_data,
                                             const brillo::SecureBlob& input);

// Authenticated decryption of |input| using AES-GCM-256 with |key| and
// |auth_data|.
//
// Assumes the 12-byte IV used during encryption is appended to |input|.
//
// Returns std::nullopt if there's an error in the OpenSSL operation.
std::optional<brillo::SecureBlob> Aes256GcmDecrypt(
    const brillo::SecureBlob& key,
    const brillo::Blob& auth_data,
    const brillo::Blob& input);

// Helper function to extract ECDSA Affine Coordinates from Certificate
// provided by ChromeOS' libarc-attestation.
keymaster_error_t GetEcdsa256KeyFromCertBlob(brillo::Blob& certData,
                                             absl::Span<uint8_t> x_coord,
                                             absl::Span<uint8_t> y_coord);

// This function should never be used with |test_mode| as false.
// |seed| - input to the function based on which key would be generated.
// |private_key| - writes the private key in byte format in this variable.
// |private_key_pem| - writes the private key in PEM format in this string.
// |x_coord| and |y_coord| are the affine coordinates of the public key.
keymaster_error_t GenerateEcdsa256KeyFromSeed(bool test_mode,
                                              absl::Span<uint8_t> seed,
                                              absl::Span<uint8_t> private_key,
                                              std::string& private_key_pem,
                                              absl::Span<uint8_t> x_coord,
                                              absl::Span<uint8_t> y_coord);
}  // namespace arc::keymint::context

#endif  // ARC_KEYMINT_CONTEXT_OPENSSL_UTILS_H_
