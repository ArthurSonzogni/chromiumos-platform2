// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_POLICY_TESTS_CRYPTO_HELPERS_H_
#define LIBBRILLO_POLICY_TESTS_CRYPTO_HELPERS_H_

#include <string_view>

#include <brillo/brillo_export.h>
#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <openssl/crypto.h>

namespace policy {

// Struct to store a private/public key pair.
struct KeyPair {
  crypto::ScopedEVP_PKEY private_key;
  brillo::Blob public_key;
};

// Generates a pair of EVP RSA keys.
BRILLO_EXPORT KeyPair GenerateRsaKeyPair();

// Signs |data| with |private key|. Returns the signature.
BRILLO_EXPORT brillo::Blob SignData(std::string_view data,
                                    EVP_PKEY& private_key);
}  // namespace policy

#endif  // LIBBRILLO_POLICY_TESTS_CRYPTO_HELPERS_H_
