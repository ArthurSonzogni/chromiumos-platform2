// Copyright 2013 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_VAULT_KEYSET_FACTORY_H_
#define CRYPTOHOME_MOCK_VAULT_KEYSET_FACTORY_H_

#include <gmock/gmock.h>
#include <libstorage/platform/platform.h>

namespace cryptohome {
class VaultKeyset;
class Crypto;

class MockVaultKeysetFactory : public VaultKeysetFactory {
 public:
  MockVaultKeysetFactory() {}
  virtual ~MockVaultKeysetFactory() {}
  MOCK_METHOD(VaultKeyset*, New, (libstorage::Platform*, Crypto*), (override));
  MOCK_METHOD(VaultKeyset*,
              NewBackup,
              (libstorage::Platform*, Crypto*),
              (override));
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_VAULT_KEYSET_FACTORY_H_
