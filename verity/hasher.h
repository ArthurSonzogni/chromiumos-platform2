// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VERITY_HASHER_H_
#define VERITY_HASHER_H_

#include <cstdint>
#include <memory>

#include <openssl/evp.h>

namespace verity {

class Hasher {
 public:
  Hasher() = default;
  virtual ~Hasher() = default;
  Hasher(const Hasher&) = delete;
  Hasher& operator=(const Hasher&) = delete;

  // `DigestSize` returns the size of the hash function's digest.
  virtual ssize_t DigestSize() const = 0;
  // `Init` initializes the hasher for a new calculation, clears previous state.
  virtual bool Init() = 0;
  // `Update` adds input bytes for hashing.
  virtual bool Update(const uint8_t* buf, size_t buflen) = 0;
  // `Final` finalizes digest computation and copies digest into `out` param.
  virtual bool Final(uint8_t* out) = 0;
};

class OpenSSLHasher : public Hasher {
 public:
  explicit OpenSSLHasher(const char* alg_name);
  ~OpenSSLHasher() override = default;

  ssize_t DigestSize() const override;
  bool Init() override;
  bool Update(const uint8_t* buf, size_t buflen) override;
  bool Final(uint8_t* out) override;

 private:
  std::unique_ptr<EVP_MD_CTX, void (*)(EVP_MD_CTX*)> digest_ctx_;
  const EVP_MD* digest_alg_;
};

}  // namespace verity

#endif  // VERITY_HASHER_H_
