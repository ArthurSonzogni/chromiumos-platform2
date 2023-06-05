// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_CRYPTO_OPENSSL_H_
#define LIBHWSEC_FOUNDATION_CRYPTO_OPENSSL_H_

#include <string>

#include <crypto/scoped_openssl_types.h>

#include "libhwsec-foundation/hwsec-foundation_export.h"

namespace hwsec_foundation {

// The wrapper of OpenSSL i2d series function. It takes a OpenSSL i2d function
// |openssl_func| and apply to |object|.
template <typename OpenSSLType, auto openssl_func>
std::string OpenSSLObjectToString(OpenSSLType* object) {
  if (object == nullptr) {
    return std::string();
  }

  unsigned char* openssl_buffer = nullptr;
  int size = openssl_func(object, &openssl_buffer);
  if (size < 0) {
    return std::string();
  }
  crypto::ScopedOpenSSLBytes scoped_buffer(openssl_buffer);

  return std::string(openssl_buffer, openssl_buffer + size);
}

// Convert RSA key (with public and/or private key set) key to the binary DER
// encoded RSAPublicKey format.
//
// Return empty string if |key| is null, or OpenSSL returned error.
HWSEC_FOUNDATION_EXPORT std::string RSAPublicKeyToString(
    const crypto::ScopedRSA& key);

// Convert RSA key (with public and/or private key set) key to the binary DER
// encoded SubjectPublicKeyInfo format.
//
// Return empty string if |key| is null, or OpenSSL returned error.
HWSEC_FOUNDATION_EXPORT std::string RSASubjectPublicKeyInfoToString(
    const crypto::ScopedRSA& key);

// Convert ECC key (with public and/or private key set) key to the binary DER
// encoded SubjectPublicKeyInfo format.
//
// Return empty string if |key| is null, or OpenSSL returned error.
HWSEC_FOUNDATION_EXPORT std::string ECCSubjectPublicKeyInfoToString(
    const crypto::ScopedEC_KEY& key);

// Convert ECDSA signature to the binary DER encoded signature
//
// Return empty string if |sig| is null, or OpenSSL returned error.
HWSEC_FOUNDATION_EXPORT std::string ECDSASignatureToString(
    const crypto::ScopedECDSA_SIG& sig);

}  // namespace hwsec_foundation

#endif  // LIBHWSEC_FOUNDATION_CRYPTO_OPENSSL_H_
