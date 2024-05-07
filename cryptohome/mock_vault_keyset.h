// Copyright 2013 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_VAULT_KEYSET_H_
#define CRYPTOHOME_MOCK_VAULT_KEYSET_H_

#include <base/files/file_path.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <libstorage/platform/platform.h>

#include "cryptohome/crypto.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {
class Crypto;

class MockVaultKeyset : public VaultKeyset {
 public:
  MOCK_METHOD(bool, Load, (const base::FilePath&), (override));
  MOCK_METHOD(bool, Save, (const base::FilePath&), (override));

  MOCK_METHOD(CryptoStatus, DecryptEx, (const KeyBlobs&), (override));
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_VAULT_KEYSET_H_
