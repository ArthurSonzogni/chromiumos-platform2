// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTOHOME_KEY_LOADER_H_
#define CRYPTOHOME_CRYPTOHOME_KEY_LOADER_H_

#include <memory>

#include <base/files/file_path.h>
#include <brillo/secure_blob.h>

#include "cryptohome/platform.h"
#include "cryptohome/tpm.h"

namespace cryptohome {

class CryptohomeKeyLoader {
 public:
  CryptohomeKeyLoader(Tpm* tpm, Platform* platform, const base::FilePath& path);
  CryptohomeKeyLoader(const CryptohomeKeyLoader&) = delete;
  CryptohomeKeyLoader& operator=(const CryptohomeKeyLoader&) = delete;

  virtual ~CryptohomeKeyLoader() = default;

  virtual bool HasCryptohomeKey();

  virtual TpmKeyHandle GetCryptohomeKey();

  virtual bool ReloadCryptohomeKey();

  virtual void Init();

 protected:
  Tpm* GetTpm() { return tpm_; }

 private:
  virtual bool CreateCryptohomeKey(brillo::SecureBlob* wrapped_key) = 0;
  bool SaveCryptohomeKey(const brillo::SecureBlob& wrapped_key);

  hwsec::StatusChain<hwsec::TPMErrorBase> LoadCryptohomeKey(
      ScopedKeyHandle* key_handle);

  bool LoadOrCreateCryptohomeKey(ScopedKeyHandle* key_handle);

  Tpm* tpm_ = nullptr;
  Platform* platform_ = nullptr;
  const base::FilePath cryptohome_key_path_;
  ScopedKeyHandle cryptohome_key_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTOHOME_KEY_LOADER_H_
