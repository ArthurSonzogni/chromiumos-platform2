// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTOHOME_RSA_KEY_LOADER_H_
#define CRYPTOHOME_CRYPTOHOME_RSA_KEY_LOADER_H_

#include "cryptohome/cryptohome_key_loader.h"

namespace cryptohome {

class CryptohomeRsaKeyLoader : public CryptohomeKeyLoader {
 public:
  CryptohomeRsaKeyLoader(Tpm* tpm, Platform* platform);
  CryptohomeRsaKeyLoader(const CryptohomeRsaKeyLoader&) = delete;
  CryptohomeRsaKeyLoader& operator=(const CryptohomeRsaKeyLoader&) = delete;
  virtual ~CryptohomeRsaKeyLoader() = default;

 private:
  bool CreateCryptohomeKey(brillo::SecureBlob* wrapped_key) override;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTOHOME_RSA_KEY_LOADER_H_
