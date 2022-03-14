// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/crypto/hmac.h"

#include <openssl/hmac.h>

namespace hwsec_foundation {
namespace {

template <class T>
brillo::SecureBlob HmacSha512Helper(const brillo::SecureBlob& key,
                                    const T& data) {
  const int kSha512OutputSize = 64;
  unsigned char mac[kSha512OutputSize];
  HMAC(EVP_sha512(), key.data(), key.size(), data.data(), data.size(), mac,
       NULL);
  return brillo::SecureBlob(std::begin(mac), std::end(mac));
}

template <class T>
brillo::SecureBlob HmacSha256Helper(const brillo::SecureBlob& key,
                                    const T& data) {
  const int kSha256OutputSize = 32;
  unsigned char mac[kSha256OutputSize];
  HMAC(EVP_sha256(), key.data(), key.size(), data.data(), data.size(), mac,
       NULL);
  return brillo::SecureBlob(std::begin(mac), std::end(mac));
}

}  // namespace

brillo::SecureBlob HmacSha512(const brillo::SecureBlob& key,
                              const brillo::Blob& data) {
  return HmacSha512Helper(key, data);
}

brillo::SecureBlob HmacSha512(const brillo::SecureBlob& key,
                              const brillo::SecureBlob& data) {
  return HmacSha512Helper(key, data);
}

brillo::SecureBlob HmacSha256(const brillo::SecureBlob& key,
                              const brillo::Blob& data) {
  return HmacSha256Helper(key, data);
}

brillo::SecureBlob HmacSha256(const brillo::SecureBlob& key,
                              const brillo::SecureBlob& data) {
  return HmacSha256Helper(key, data);
}

}  // namespace hwsec_foundation
