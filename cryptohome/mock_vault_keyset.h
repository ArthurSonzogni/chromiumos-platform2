// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_VAULT_KEYSET_H_
#define CRYPTOHOME_MOCK_VAULT_KEYSET_H_

#include "cryptohome/vault_keyset.h"

#include <string>

#include <base/files/file_path.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"

namespace cryptohome {
class Platform;
class Crypto;

class MockVaultKeyset : public VaultKeyset {
 public:
  MockVaultKeyset() = default;
  virtual ~MockVaultKeyset() = default;

  MOCK_METHOD(void, Initialize, (Platform*, Crypto*), (override));

  MOCK_METHOD(void, FromKeys, (const VaultKeysetKeys&), (override));
  MOCK_METHOD(bool, FromKeysBlob, (const brillo::SecureBlob&), (override));

  MOCK_METHOD(bool, ToKeys, (VaultKeysetKeys*), (const, override));
  MOCK_METHOD(bool, ToKeysBlob, (brillo::SecureBlob*), (const, override));

  MOCK_METHOD(bool, Load, (const base::FilePath&), (override));
  MOCK_METHOD(bool, Save, (const base::FilePath&), (override));

  MOCK_METHOD(bool,
              Decrypt,
              (const brillo::SecureBlob&, bool, CryptoError*),
              (override));
  MOCK_METHOD(bool,
              Encrypt,
              (const brillo::SecureBlob&, const std::string&),
              (override));

  MOCK_METHOD(void, CreateRandom, (), (override));

  MOCK_METHOD(const brillo::SecureBlob&, GetFek, (), (const, override));
  MOCK_METHOD(const brillo::SecureBlob&, GetFekSig, (), (const, override));
  MOCK_METHOD(const brillo::SecureBlob&, GetFekSalt, (), (const, override));
  MOCK_METHOD(const brillo::SecureBlob&, GetFnek, (), (const, override));
  MOCK_METHOD(const brillo::SecureBlob&, GetFnekSig, (), (const, override));
  MOCK_METHOD(const brillo::SecureBlob&, GetFnekSalt, (), (const, override));

  MOCK_METHOD(std::string, GetLabel, (), (const, override));
  MOCK_METHOD(const base::FilePath&, GetSourceFile, (), (const, override));
  MOCK_METHOD(void, SetLegacyIndex, (int), (override));
  MOCK_METHOD(const int, GetLegacyIndex, (), (const, override));
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_VAULT_KEYSET_H_
