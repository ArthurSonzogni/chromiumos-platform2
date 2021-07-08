// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTOHOME_ECC_KEY_LOADER_H_
#define CRYPTOHOME_CRYPTOHOME_ECC_KEY_LOADER_H_

#include "cryptohome/cryptohome_key_loader.h"

namespace cryptohome {

class CryptohomeEccKeyLoader : public CryptohomeKeyLoader {
 public:
  CryptohomeEccKeyLoader(Tpm* tpm, Platform* platform);
  CryptohomeEccKeyLoader(const CryptohomeEccKeyLoader&) = delete;
  CryptohomeEccKeyLoader& operator=(const CryptohomeEccKeyLoader&) = delete;
  virtual ~CryptohomeEccKeyLoader() = default;

 private:
  // Creates an ECC cryptohome key and store the wrapped key data into
  // |wrapped_key|, Return true when the operation success.
  bool CreateCryptohomeKey(brillo::SecureBlob* wrapped_key) override;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTOHOME_ECC_KEY_LOADER_H_
