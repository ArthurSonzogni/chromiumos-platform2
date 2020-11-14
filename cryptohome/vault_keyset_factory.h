// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_VAULT_KEYSET_FACTORY_H_
#define CRYPTOHOME_VAULT_KEYSET_FACTORY_H_

#include <base/macros.h>

namespace cryptohome {
class Crypto;
class Platform;
class VaultKeyset;
// Provide a means for mocks to be injected anywhere that new VaultKeyset
// objects are created.
class VaultKeysetFactory {
 public:
  VaultKeysetFactory();
  VaultKeysetFactory(const VaultKeysetFactory&) = delete;
  VaultKeysetFactory& operator=(const VaultKeysetFactory&) = delete;

  virtual ~VaultKeysetFactory();
  virtual VaultKeyset* New(Platform* platform, Crypto* crypto);
};

}  // namespace cryptohome
#endif  // CRYPTOHOME_VAULT_KEYSET_FACTORY_H_
