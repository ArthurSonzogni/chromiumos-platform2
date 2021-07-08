// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cryptohome_rsa_key_loader.h"

#include "cryptohome/crypto/rsa.h"

namespace cryptohome {

namespace {

constexpr char kDefaultCryptohomeKeyFile[] = "/home/.shadow/cryptohome.key";

constexpr unsigned int kDefaultTpmRsaKeyBits = 2048;

}  // namespace

CryptohomeRsaKeyLoader::CryptohomeRsaKeyLoader(Tpm* tpm, Platform* platform)
    : CryptohomeKeyLoader(
          tpm, platform, base::FilePath(kDefaultCryptohomeKeyFile)) {}

bool CryptohomeRsaKeyLoader::CreateCryptohomeKey(
    brillo::SecureBlob* wrapped_key) {
  if (!GetTpm()->IsEnabled() || !GetTpm()->IsOwned()) {
    LOG(WARNING) << "Canceled creating cryptohome key - TPM is not ready.";
    return false;
  }
  brillo::SecureBlob n;
  brillo::SecureBlob p;
  if (!CreateRsaKey(kDefaultTpmRsaKeyBits, &n, &p)) {
    LOG(ERROR) << "Error creating RSA key";
    return false;
  }

  CHECK(wrapped_key);

  if (!GetTpm()->WrapRsaKey(n, p, wrapped_key)) {
    LOG(ERROR) << "Couldn't wrap cryptohome key";
    return false;
  }

  return true;
}

}  // namespace cryptohome
