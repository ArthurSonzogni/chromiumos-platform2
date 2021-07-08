// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cryptohome_ecc_key_loader.h"

namespace cryptohome {

namespace {

constexpr char kDefaultCryptohomeKeyFile[] = "/home/.shadow/cryptohome.ecc.key";

}  // namespace

CryptohomeEccKeyLoader::CryptohomeEccKeyLoader(Tpm* tpm, Platform* platform)
    : CryptohomeKeyLoader(
          tpm, platform, base::FilePath(kDefaultCryptohomeKeyFile)) {}

bool CryptohomeEccKeyLoader::CreateCryptohomeKey(
    brillo::SecureBlob* wrapped_key) {
  if (!GetTpm()->IsEnabled() || !GetTpm()->IsOwned()) {
    LOG(WARNING) << "Canceled creating ECC cryptohome key - TPM is not ready.";
    return false;
  }

  if (!GetTpm()->CreateWrappedEccKey(wrapped_key)) {
    LOG(ERROR) << "Couldn't create wrapped ECC cryptohome key";
    return false;
  }

  return true;
}

}  // namespace cryptohome
