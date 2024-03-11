// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "policy/tests/crypto_helpers.h"

#include <string_view>
#include <utility>

#include <base/check.h>
#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <openssl/crypto.h>

namespace policy {

KeyPair GenerateRsaKeyPair() {
  crypto::ScopedEVP_PKEY_CTX pkey_context(
      EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr));
  CHECK(pkey_context);
  CHECK(EVP_PKEY_keygen_init(pkey_context.get()));
  CHECK(EVP_PKEY_CTX_set_rsa_keygen_bits(pkey_context.get(), 2048));
  EVP_PKEY* pkey_raw = nullptr;
  CHECK(EVP_PKEY_keygen(pkey_context.get(), &pkey_raw));
  crypto::ScopedEVP_PKEY private_key(pkey_raw);
  // Obtain the DER-encoded Subject Public Key Info.
  const int key_spki_der_length = i2d_PUBKEY(private_key.get(), nullptr);
  CHECK_GT(key_spki_der_length, 0);
  brillo::Blob public_key(key_spki_der_length);
  uint8_t* public_key_buffer = public_key.data();
  CHECK(i2d_PUBKEY(private_key.get(), &public_key_buffer) == public_key.size());

  return KeyPair(std::move(private_key), public_key);
}

brillo::Blob SignData(std::string_view data,
                      EVP_PKEY& private_key,
                      const EVP_MD& digest_type) {
  crypto::ScopedEVP_MD_CTX ctx(EVP_MD_CTX_new());
  CHECK(ctx);

  CHECK(EVP_SignInit(ctx.get(), &digest_type));
  CHECK(EVP_SignUpdate(ctx.get(), data.data(), data.size()));

  brillo::Blob signature(EVP_PKEY_size(&private_key));
  unsigned int signature_size = 0;
  CHECK(EVP_SignFinal(ctx.get(), signature.data(), &signature_size,
                      &private_key));
  signature.resize(signature_size);

  return signature;
}

}  // namespace policy
