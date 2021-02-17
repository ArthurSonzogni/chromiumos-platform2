// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// OpenSSL util. It should be used only by OpenSSL C++ wrappers.

#ifndef CRYPTOHOME_CRYPTO_OPENSSL_UTIL_H_
#define CRYPTOHOME_CRYPTO_OPENSSL_UTIL_H_

#include <string>

#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <openssl/bn.h>

namespace cryptohome {

// TODO(b:182154354): Move to Chrome crypto library.
using ScopedBN_CTX = crypto::ScopedOpenSSL<BN_CTX, BN_CTX_free>;

// Creates context for big number operations. Returns nullptr if error occurred.
ScopedBN_CTX CreateBigNumContext();

// Creates big number with undefined value. Returns nullptr if error occurred.
crypto::ScopedBIGNUM CreateBigNum();

// Creates BIGNUM and set it to a given value. Returns nullptr if error
// occurred. This is useful for testing, otherwise shouldn't be used.
crypto::ScopedBIGNUM BigNumFromValue(BN_ULONG value);

// Converts SecureBlob to BIGNUM. Returns nullptr if error occurred.
// Empty SecureBlob is interpreted as zero.
// The input SecureBlob is expected to be in big-endian encoding.
crypto::ScopedBIGNUM SecureBlobToBigNum(const brillo::SecureBlob& blob);

// Converts BIGNUM to SecureBlob. Returns false if error occurred, otherwise
// stores resulting blob in `result`. Stores empty SecureBlob if `bn` number
// was zero.
// The resulting SecureBlob is encoded in big-endian form.
bool BigNumToSecureBlob(const BIGNUM& bn, brillo::SecureBlob* result);

// Returns all errors in OpenSSL error queue delimited with a semicolon
// starting from the earliest. Returns empty string if there are no errors in
// the queue. Clears the queue.
std::string GetOpenSSLErrors();

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTO_OPENSSL_UTIL_H_
