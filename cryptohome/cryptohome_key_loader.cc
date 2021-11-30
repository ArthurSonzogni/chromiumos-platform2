// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cryptohome_key_loader.h"

#include <utility>

#include <base/logging.h>

using brillo::SecureBlob;
using hwsec::StatusChain;
using hwsec::TPMError;
using hwsec::TPMErrorBase;
using hwsec::TPMRetryAction;
using hwsec_foundation::error::CreateError;
using hwsec_foundation::error::WrapError;
namespace cryptohome {

CryptohomeKeyLoader::CryptohomeKeyLoader(Tpm* tpm,
                                         Platform* platform,
                                         const base::FilePath& path)
    : tpm_(tpm), platform_(platform), cryptohome_key_path_(path) {}

bool CryptohomeKeyLoader::SaveCryptohomeKey(const SecureBlob& wrapped_key) {
  bool ok = platform_->WriteSecureBlobToFileAtomicDurable(cryptohome_key_path_,
                                                          wrapped_key, 0600);
  if (!ok)
    LOG(ERROR) << "Error writing key file of desired size: "
               << wrapped_key.size();
  return ok;
}

StatusChain<TPMErrorBase> CryptohomeKeyLoader::LoadCryptohomeKey(
    ScopedKeyHandle* key_handle) {
  CHECK(key_handle);
  // First, try loading the key from the key file.
  SecureBlob raw_key;
  if (platform_->ReadFileToSecureBlob(cryptohome_key_path_, &raw_key)) {
    if (StatusChain<TPMErrorBase> err =
            tpm_->LoadWrappedKey(raw_key, key_handle)) {
      if (err->ToTPMRetryAction() == TPMRetryAction::kNoRetry) {
        LOG(INFO) << "Using legacy upgrade path: " << err;
        goto legacy_upgrade_path;
      }
      return WrapError<TPMError>(std::move(err), "Failed to load wrapped key");
    }
    return nullptr;
  }

legacy_upgrade_path:
  // Then try loading the key by the UUID (this is a legacy upgrade path).
  if (!tpm_->LegacyLoadCryptohomeKey(key_handle, &raw_key)) {
    return CreateError<TPMError>("Failed to load legacy cryptohome key",
                                 TPMRetryAction::kNoRetry);
  }

  // Save the legacy cryptohome key to the well-known location.
  if (!SaveCryptohomeKey(raw_key)) {
    return CreateError<TPMError>("Couldn't save legacy cryptohome key",
                                 TPMRetryAction::kNoRetry);
  }

  return nullptr;
}

bool CryptohomeKeyLoader::LoadOrCreateCryptohomeKey(
    ScopedKeyHandle* key_handle) {
  CHECK(key_handle);

  if (!GetTpm()->IsEnabled() || !GetTpm()->IsOwned()) {
    LOG(WARNING) << "Canceled loading cryptohome key - TPM is not ready.";
    return false;
  }

  // Try to load the cryptohome key.
  if (StatusChain<TPMErrorBase> err = LoadCryptohomeKey(key_handle)) {
    if (err->ToTPMRetryAction() == TPMRetryAction::kNoRetry) {
      // The key couldn't be loaded, and it wasn't due to a transient error,
      // so we must create the key.
      SecureBlob wrapped_key;
      if (CreateCryptohomeKey(&wrapped_key)) {
        if (!SaveCryptohomeKey(wrapped_key)) {
          LOG(ERROR) << "Couldn't save cryptohome key";
          return false;
        }
        LOG(INFO) << "Created new cryptohome key.";
        err = LoadCryptohomeKey(key_handle);
      }
    }
    if (err) {
      LOG(ERROR) << "Failed to load or create cryptohome key: " << err;
      return false;
    }
  }
  return true;
}

bool CryptohomeKeyLoader::HasCryptohomeKey() {
  return cryptohome_key_.has_value();
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
  if (StatusChain<TPMErrorBase> err = LoadCryptohomeKey(&cryptohome_key_)) {
    LOG(ERROR) << "Error reloading Cryptohome key: " << err;
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
