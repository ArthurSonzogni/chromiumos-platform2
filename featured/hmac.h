// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEATURED_HMAC_H_
#define FEATURED_HMAC_H_

#include <optional>
#include <string>
#include <vector>

#include <base/strings/string_piece.h>
#include <openssl/hmac.h>

namespace featured {

// HMAC is a wrapper around OpenSSL's functionality for generating
// and verifying HMACs of arbitrary data, as well as generating keys. It
// simplifies the APIs and provides a more ergonomic approach.
//
// Similar to libchrome's //crypto/hmac, but at ToT that uses BoringSSL, which
// is not supported on CrOS. So, we use OpenSSL instead.
class HMAC {
 public:
  enum HashAlgorithm {
    SHA256,  // we deliberately do not support SHA1.
  };

  // Create instance of class.
  // (Note that Init will be required to make the class usable.)
  explicit HMAC(HashAlgorithm alg);

  ~HMAC();

  HMAC(const HMAC&) = delete;
  HMAC& operator=(const HMAC&) = delete;

  // Attempt to initialize the HMAC structure, using a randomly generated key.
  // May be called repeatedly, but will generate a new key each time.
  // Returns true if generating the key succeeded, or false otherwise. If key
  // generation fails, the class will not be usable.
  [[nodiscard]] bool Init();

  // Similar, but uses a specified key. User is responsible for ensuring the key
  // is securely generated. (e.g., from a prior run of HMAC.)
  [[nodiscard]] bool Init(base::StringPiece key);

  // Get the raw key.
  // NOTE: You must carefully handle this data; it is exceedingly sensitive.
  // Do not log it or write it to disk in plaintext, and use `OPENSSL_cleanse`
  // to clear the memory out once you are done with the data, as soon as
  // possible.
  std::string GetKey() const { return key_; }

  // Attempt to HMAC the given data with |key_|.
  std::optional<std::string> Sign(base::StringPiece data) const;

  // Determine whether |hmac| is a valid HMAC of |data| with the |key_|.
  // DO NOT attempt to implement this manually; comparisons between different
  // signatures are sensitive to potential timing attacks.
  bool Verify(base::StringPiece data, base::StringPiece hmac) const;

 private:
  // Cleanse all sensitive data.
  void ZeroData();

  // The symmetric key
  std::string key_;
  HMAC_CTX* ctx_ = nullptr;
  HashAlgorithm hash_alg_;
};

}  // namespace featured

#endif  // FEATURED_HMAC_H_
