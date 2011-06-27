// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CRYPTO_INTERFACE_
#define SHILL_CRYPTO_INTERFACE_

#include <string>

namespace shill {

// An interface to an encryption/decryption module.
class CryptoInterface {
 public:
  virtual ~CryptoInterface() {}

  // Returns a unique identifier for this crypto module.
  virtual std::string GetID() = 0;

  // Encrypts |plaintext| into |ciphertext|. Returns true on success.
  virtual bool Encrypt(const std::string &plaintext,
                       std::string *ciphertext) = 0;

  // Decrypts |ciphertext| into |plaintext|. Returns true on success.
  virtual bool Decrypt(const std::string &ciphertext,
                       std::string *plaintext) = 0;
};

}  // namespace shill

#endif  // SHILL_CRYPTO_INTERFACE_
