// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "featured/hmac.h"

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/rand.h>

#include <absl/cleanup/cleanup.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>

namespace featured {

// Ints because RAND_priv_bytes takes an int.
constexpr int kKeyBits = 256;  // 32 bytes
constexpr int kKeyBytes = kKeyBits / 8;

namespace {

// Log all errors.
void LogOpenSSLError(const std::string& context) {
  LOG(ERROR) << "Failure: " << context << ". OpenSSL context, if any, follows.";
  ERR_print_errors_cb(
      [](const char* str, size_t len, void* context) -> int {
        std::string s(str, len);
        LOG(ERROR) << reinterpret_cast<const char*>(context) << ": " << s;
        return 1;  // report success.
      },
      const_cast<void*>(reinterpret_cast<const void*>(context.data())));
}

}  // namespace

HMAC::HMAC(HMAC::HashAlgorithm alg) : hash_alg_(alg) {}

HMAC::~HMAC() {
  ZeroData();
}

// ZeroData fills memory with null bytes to clear confidential data (e.g. keys)
void HMAC::ZeroData() {
  if (!key_.empty()) {
    OPENSSL_cleanse(key_.data(), key_.size());
  }
  HMAC_CTX_free(ctx_);
  ctx_ = nullptr;
}

bool HMAC::Init() {
  return Init("");
}

bool HMAC::Init(base::StringPiece key) {
  // Perhaps we'll support more later, but for now SHA256 suffices.
  DCHECK_EQ(hash_alg_, SHA256);

  // Make sure we're starting from a clean slate, and wipe out any pre-existing
  // key data.
  ZeroData();
  key_.clear();
  ctx_ = HMAC_CTX_new();
  if (ctx_ == nullptr) {
    LogOpenSSLError("Init: HMAC_CTX_new");
    return false;
  }

  if (!key.empty()) {
    key_.assign(key.begin(), key.end());
  } else {
    // Allocate sufficient space.
    key_.resize(kKeyBytes);

    // Generate the key.
    int rv = RAND_priv_bytes(reinterpret_cast<unsigned char*>(key_.data()),
                             kKeyBytes);
    if (rv != 1) {
      LogOpenSSLError("Init - RandPrivBytes");
      ZeroData();
      // ctx_ will be nullptr so the class will be unusable.
      return false;
    }
  }

  int rv = HMAC_Init_ex(ctx_, key_.data(), key_.size(), EVP_sha256(), nullptr);
  if (rv != 1) {
    LogOpenSSLError("Init - HMAC_Init_ex");
    ZeroData();
    return false;
  }

  return true;
}

std::optional<std::string> HMAC::Sign(base::StringPiece data) const {
  DCHECK_NE(ctx_, nullptr) << "Class not initialized";
  if (ctx_ == nullptr) {
    // Safeguard in case code using empty key sneaks through to prod -- better
    // to fail than allow signatures using no key.
    // We don't CHECK in case a subtle bug occurs causing an untested path to be
    // uncovered in prod.
    return std::nullopt;
  }

  // Reuse same key, but otherwise use a fresh ctx.
  // (Otherwise, a second Sign would reuse data from the first Sign, or a Verify
  // after a Sign would reuse data from the first Sign.)
  HMAC_CTX* ctx_for_sign = HMAC_CTX_new();
  if (ctx_for_sign == nullptr) {
    LogOpenSSLError("Sign - HMAC_CTX_new");
    return std::nullopt;
  }
  // Ensure ctx_for_sign is freed.
  absl::Cleanup free_ctx = [ctx_for_sign]() { HMAC_CTX_free(ctx_for_sign); };

  int rv = HMAC_CTX_copy(ctx_for_sign, ctx_);
  if (rv != 1) {
    LogOpenSSLError("Sign - HMAC_CTX_copy");
    return std::nullopt;
  }

  rv = HMAC_Update(ctx_for_sign,
                   reinterpret_cast<const unsigned char*>(data.data()),
                   data.size());
  if (rv != 1) {
    LogOpenSSLError("Sign - HMAC_Update");
    return std::nullopt;
  }

  // Allocate sufficient space.
  uint8_t digest_data[EVP_MAX_MD_SIZE] = {0};

  unsigned int output_size = 0;
  rv = HMAC_Final(ctx_for_sign, digest_data, &output_size);
  if (rv != 1) {
    LogOpenSSLError("Sign: HMAC_final");
    return std::nullopt;
  }

  return std::string(reinterpret_cast<char*>(digest_data), output_size);
}

bool HMAC::Verify(base::StringPiece data, base::StringPiece hmac) const {
  std::optional<std::string> actual = Sign(data);
  if (!actual.has_value()) {
    // Fail closed.
    return false;
  }
  if (actual->size() != hmac.size()) {
    return false;
  }
  return CRYPTO_memcmp(actual->c_str(), hmac.data(), actual->size()) == 0;
}

}  // namespace featured
