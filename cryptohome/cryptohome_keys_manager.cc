// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cryptohome_keys_manager.h"
#include <memory>

#include "cryptohome/cryptohome_ecc_key_loader.h"
#include "cryptohome/cryptohome_key_loader.h"
#include "cryptohome/cryptohome_rsa_key_loader.h"

#include <base/logging.h>

namespace cryptohome {

namespace {

using KeyLoaderCreator =
    std::unique_ptr<CryptohomeKeyLoader> (*)(Tpm* tpm, Platform* platform);

template <typename T>
std::unique_ptr<CryptohomeKeyLoader> MakeUniqueKeyLoader(Tpm* tpm,
                                                         Platform* platform) {
  return std::make_unique<T>(tpm, platform);
}

struct KeyLoaderPair {
  CryptohomeKeyType type;
  KeyLoaderCreator creator;
};

KeyLoaderPair kKeyLoaderPairs[] = {
    {CryptohomeKeyType::kRSA, MakeUniqueKeyLoader<CryptohomeRsaKeyLoader>},
#if USE_TPM2
    {CryptohomeKeyType::kECC, MakeUniqueKeyLoader<CryptohomeEccKeyLoader>},
#endif
};

}  // namespace

CryptohomeKeysManager::CryptohomeKeysManager(Tpm* tpm, Platform* platform) {
  for (const KeyLoaderPair& pair : kKeyLoaderPairs) {
    key_loaders_[pair.type] = pair.creator(tpm, platform);
  }
}

void CryptohomeKeysManager::Init() {
  for (auto& loader : key_loaders_) {
    loader.second->Init();
  }
}

CryptohomeKeyLoader* CryptohomeKeysManager::GetKeyLoader(
    CryptohomeKeyType key_type) {
  auto iter = key_loaders_.find(key_type);
  if (iter != key_loaders_.end()) {
    return iter->second.get();
  }
  return nullptr;
}

bool CryptohomeKeysManager::ReloadAllCryptohomeKeys() {
  for (auto& loader : key_loaders_) {
    if (!loader.second->ReloadCryptohomeKey()) {
      LOG(ERROR) << "Failed to reload cryptohome key "
                 << static_cast<int>(loader.first);
      return false;
    }
  }
  return true;
}

bool CryptohomeKeysManager::HasAnyCryptohomeKey() {
  for (auto& loader : key_loaders_) {
    if (loader.second->HasCryptohomeKey()) {
      return true;
    }
  }
  return false;
}

bool CryptohomeKeysManager::HasCryptohomeKey(CryptohomeKeyType key_type) {
  CryptohomeKeyLoader* key_loader = GetKeyLoader(key_type);
  return key_loader && key_loader->HasCryptohomeKey();
}

}  // namespace cryptohome
