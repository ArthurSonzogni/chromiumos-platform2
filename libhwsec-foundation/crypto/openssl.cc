// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/crypto/openssl.h"

#include <string>

#include <crypto/scoped_openssl_types.h>

namespace hwsec_foundation {

std::string RSAPublicKeyToString(const crypto::ScopedRSA& key) {
  return OpenSSLObjectToString<RSA, i2d_RSAPublicKey>(key.get());
}

std::string RSASubjectPublicKeyInfoToString(const crypto::ScopedRSA& key) {
  return OpenSSLObjectToString<RSA, i2d_RSA_PUBKEY>(key.get());
}

std::string ECCSubjectPublicKeyInfoToString(const crypto::ScopedEC_KEY& key) {
  return OpenSSLObjectToString<EC_KEY, i2d_EC_PUBKEY>(key.get());
}

std::string ECDSASignatureToString(const crypto::ScopedECDSA_SIG& sig) {
  return OpenSSLObjectToString<ECDSA_SIG, i2d_ECDSA_SIG>(sig.get());
}

}  // namespace hwsec_foundation
