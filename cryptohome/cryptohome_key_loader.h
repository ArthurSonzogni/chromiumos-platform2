// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTOHOME_KEY_LOADER_H_
#define CRYPTOHOME_CRYPTOHOME_KEY_LOADER_H_

#include <memory>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <brillo/secure_blob.h>

#include "cryptohome/tpm.h"

namespace cryptohome {

class Platform;

class CryptohomeKeyLoader {
 public:
  CryptohomeKeyLoader(Tpm* tpm, Platform* platform);
  CryptohomeKeyLoader(const CryptohomeKeyLoader&) = delete;
  CryptohomeKeyLoader& operator=(const CryptohomeKeyLoader&) = delete;

  virtual ~CryptohomeKeyLoader();

  virtual bool HasCryptohomeKey();

  virtual TpmKeyHandle GetCryptohomeKey();

  virtual bool ReloadCryptohomeKey();

  virtual void Init();

 private:
  bool SaveCryptohomeKey(const brillo::SecureBlob& wrapped_key);

  Tpm::TpmRetryAction LoadCryptohomeKey(ScopedKeyHandle* key_handle);

  bool CreateCryptohomeKey();

  bool LoadOrCreateCryptohomeKey(ScopedKeyHandle* key_handle);

  Tpm* tpm_ = nullptr;
  Platform* platform_ = nullptr;
  ScopedKeyHandle cryptohome_key_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTOHOME_KEY_LOADER_H_
