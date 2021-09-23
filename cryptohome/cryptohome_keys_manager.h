// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTOHOME_KEYS_MANAGER_H_
#define CRYPTOHOME_CRYPTOHOME_KEYS_MANAGER_H_

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "cryptohome/cryptohome_key_loader.h"

namespace cryptohome {

enum class CryptohomeKeyType {
  kRSA,
  kECC,
};

class CryptohomeKeysManager {
 public:
  CryptohomeKeysManager(Tpm* tpm, Platform* platform);
  CryptohomeKeysManager(const CryptohomeKeysManager&) = delete;
  CryptohomeKeysManager& operator=(const CryptohomeKeysManager&) = delete;

  // constructor for testing purpose.
  explicit CryptohomeKeysManager(
      std::vector<std::pair<CryptohomeKeyType,
                            std::unique_ptr<CryptohomeKeyLoader>>> init_list) {
    for (auto& pair : init_list) {
      key_loaders_.emplace(pair.first, std::move(pair.second));
    }
  }

  virtual ~CryptohomeKeysManager() = default;

  // Init all key loaders.
  virtual void Init();

  // Return the specific key loader.
  virtual CryptohomeKeyLoader* GetKeyLoader(CryptohomeKeyType key_type);

  // Reload cryptohome keys in all key loaders. Return true when all key loaders
  // reload successfully.
  virtual bool ReloadAllCryptohomeKeys();

  // Whether the key manager has any cryptohome key or not.
  virtual bool HasAnyCryptohomeKey();

  // Whether the specific key loader has cryptohome key or not.
  virtual bool HasCryptohomeKey(CryptohomeKeyType key_type);

 private:
  std::map<CryptohomeKeyType, std::unique_ptr<CryptohomeKeyLoader>>
      key_loaders_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTOHOME_KEYS_MANAGER_H_
