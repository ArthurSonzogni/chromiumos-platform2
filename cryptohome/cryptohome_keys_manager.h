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

  virtual void Init();

  virtual CryptohomeKeyLoader* GetKeyLoader(CryptohomeKeyType key_type);

  virtual bool ReloadAllCryptohomeKeys();

  virtual bool HasAnyCryptohomeKey();

 private:
  std::map<CryptohomeKeyType, std::unique_ptr<CryptohomeKeyLoader>>
      key_loaders_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTOHOME_KEYS_MANAGER_H_
