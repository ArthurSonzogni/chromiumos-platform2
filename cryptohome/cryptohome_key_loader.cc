// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cryptohome_key_loader.h"

#include "cryptohome/crypto/rsa.h"
#include "cryptohome/platform.h"

using base::FilePath;
using brillo::SecureBlob;

namespace cryptohome {

namespace {

constexpr char kDefaultCryptohomeKeyFile[] = "/home/.shadow/cryptohome.key";

constexpr unsigned int kDefaultTpmRsaKeyBits = 2048;

}  // namespace

CryptohomeKeyLoader::CryptohomeKeyLoader(Tpm* tpm, Platform* platform)
    : tpm_(tpm), platform_(platform) {}

CryptohomeKeyLoader::~CryptohomeKeyLoader() {}

bool CryptohomeKeyLoader::CreateCryptohomeKey() {
  if (!tpm_->IsEnabled() || !tpm_->IsOwned()) {
    LOG(WARNING) << "Canceled creating cryptohome key - TPM is not ready.";
    return false;
  }
  SecureBlob n;
  SecureBlob p;
  if (!CreateRsaKey(kDefaultTpmRsaKeyBits, &n, &p)) {
    LOG(ERROR) << "Error creating RSA key";
    return false;
  }
  SecureBlob wrapped_key;
  if (!tpm_->WrapRsaKey(n, p, &wrapped_key)) {
    LOG(ERROR) << "Couldn't wrap cryptohome key";
    return false;
  }

  if (!SaveCryptohomeKey(wrapped_key)) {
    LOG(ERROR) << "Couldn't save cryptohome key";
    return false;
  }

  LOG(INFO) << "Created new cryptohome key.";
  return true;
}

bool CryptohomeKeyLoader::SaveCryptohomeKey(const SecureBlob& wrapped_key) {
  const FilePath key_file(kDefaultCryptohomeKeyFile);
  bool ok = platform_->WriteSecureBlobToFileAtomicDurable(key_file, wrapped_key,
                                                          0600);
  if (!ok)
    LOG(ERROR) << "Error writing key file of desired size: "
               << wrapped_key.size();
  return ok;
}

Tpm::TpmRetryAction CryptohomeKeyLoader::LoadCryptohomeKey(
    ScopedKeyHandle* key_handle) {
  CHECK(key_handle);
  // First, try loading the key from the key file.
  {
    SecureBlob raw_key;
    const FilePath key_file(kDefaultCryptohomeKeyFile);
    if (platform_->ReadFileToSecureBlob(key_file, &raw_key)) {
      Tpm::TpmRetryAction retry_action =
          tpm_->LoadWrappedKey(raw_key, key_handle);
      if (retry_action == Tpm::kTpmRetryNone ||
          tpm_->IsTransient(retry_action)) {
        return retry_action;
      }
    }
  }

  // Then try loading the key by the UUID (this is a legacy upgrade path).
  SecureBlob raw_key;
  if (!tpm_->LegacyLoadCryptohomeKey(key_handle, &raw_key)) {
    return Tpm::kTpmRetryFailNoRetry;
  }

  // Save the cryptohome key to the well-known location.
  if (!SaveCryptohomeKey(raw_key)) {
    LOG(ERROR) << "Couldn't save cryptohome key";
    return Tpm::kTpmRetryFailNoRetry;
  }
  return Tpm::kTpmRetryNone;
}

bool CryptohomeKeyLoader::LoadOrCreateCryptohomeKey(
    ScopedKeyHandle* key_handle) {
  CHECK(key_handle);
  // Try to load the cryptohome key.
  Tpm::TpmRetryAction retry_action = LoadCryptohomeKey(key_handle);
  if (retry_action != Tpm::kTpmRetryNone && !tpm_->IsTransient(retry_action)) {
    // The key couldn't be loaded, and it wasn't due to a transient error,
    // so we must create the key.
    if (CreateCryptohomeKey()) {
      retry_action = LoadCryptohomeKey(key_handle);
    }
  }
  return retry_action == Tpm::kTpmRetryNone;
}

bool CryptohomeKeyLoader::HasCryptohomeKey() {
  return (cryptohome_key_.value() != kInvalidKeyHandle);
}

TpmKeyHandle CryptohomeKeyLoader::GetCryptohomeKey() {
  return cryptohome_key_.value();
}

bool CryptohomeKeyLoader::ReloadCryptohomeKey() {
  CHECK(HasCryptohomeKey());
  // Release the handle first, we know this handle doesn't contain a loaded key
  // since ReloadCryptohomeKey only called after we failed to use it.
  // Otherwise we may flush the newly loaded key and fail to use it again,
  // if it is loaded to the same handle.
  // TODO(crbug.com/687330): change to closing the handle and ignoring errors
  // once checking for stale virtual handles is implemented in trunksd.
  cryptohome_key_.release();
  if (LoadCryptohomeKey(&cryptohome_key_) != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Error reloading Cryptohome key.";
    return false;
  }
  return true;
}

void CryptohomeKeyLoader::Init() {
  if (!LoadOrCreateCryptohomeKey(&cryptohome_key_)) {
    LOG(ERROR) << __func__ << ": failed to load or create cryptohome key";
  }
}

}  // namespace cryptohome
