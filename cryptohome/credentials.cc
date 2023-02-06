// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/credentials.h"

#include <brillo/cryptohome.h>
#include "cryptohome/username.h"

using brillo::SecureBlob;
using brillo::cryptohome::home::SanitizeUserName;
using brillo::cryptohome::home::SanitizeUserNameWithSalt;

namespace cryptohome {

Credentials::Credentials() = default;

Credentials::Credentials(const Username& username, const SecureBlob& passkey)
    : username_(username), passkey_(passkey) {}

Credentials::Credentials(const Credentials& rhs) = default;

Credentials::~Credentials() = default;

Credentials& Credentials::operator=(const Credentials& rhs) = default;

ObfuscatedUsername Credentials::GetObfuscatedUsername() const {
  return SanitizeUserName(username_);
}

}  // namespace cryptohome
