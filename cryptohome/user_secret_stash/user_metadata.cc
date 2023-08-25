// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash/user_metadata.h"

#include <brillo/secure_blob.h>
#include <libhwsec-foundation/status/status_chain_macros.h>

#include "cryptohome/flatbuffer_schemas/user_secret_stash_container.h"
#include "cryptohome/user_secret_stash/encrypted.h"
#include "cryptohome/user_secret_stash/user_secret_stash.h"

namespace cryptohome {

UserMetadataReader::UserMetadataReader(UssStorage* storage)
    : storage_(storage) {}

CryptohomeStatusOr<UserMetadata> UserMetadataReader::Load(
    const ObfuscatedUsername& username) {
  ASSIGN_OR_RETURN(EncryptedUss uss,
                   EncryptedUss::FromStorage(username, *storage_));
  return uss.user_metadata();
}

}  // namespace cryptohome
