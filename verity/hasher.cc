// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "verity/hasher.h"

#include <cstdint>
#include <memory>
#include <utility>

#include <openssl/evp.h>

namespace verity {

OpenSSLHasher::OpenSSLHasher(const char* alg_name)
    : Hasher(),
      digest_ctx_(EVP_MD_CTX_new(), EVP_MD_CTX_free),
      digest_alg_(EVP_get_digestbyname(alg_name)) {}

ssize_t OpenSSLHasher::DigestSize() const {
  if (!digest_alg_)
    return -1;
  return EVP_MD_size(digest_alg_);
}

bool OpenSSLHasher::Init() {
  if (!digest_alg_ || !digest_ctx_) {
    return false;
  }

  EVP_DigestInit(digest_ctx_.get(), digest_alg_);
  return true;
}

bool OpenSSLHasher::Update(const uint8_t* buf, size_t buflen) {
  return EVP_DigestUpdate(digest_ctx_.get(), buf, buflen) == 1;
}

bool OpenSSLHasher::Final(uint8_t* out) {
  return EVP_DigestFinal_ex(digest_ctx_.get(), out, NULL) == 1;
}

}  // namespace verity
