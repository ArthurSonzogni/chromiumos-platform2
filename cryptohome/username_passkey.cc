// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/username_passkey.h"

#include <brillo/secure_blob.h>

#include "cryptohome/crypto.h"
#include "cryptohome/obfuscated_username.h"

namespace cryptohome {
using brillo::SecureBlob;

UsernamePasskey::UsernamePasskey(const char *username,
                                 const brillo::SecureBlob& passkey)
    : username_(username, strlen(username)),
      passkey_() {
  passkey_.assign(passkey.begin(), passkey.end());
}

UsernamePasskey::~UsernamePasskey() {
}

void UsernamePasskey::Assign(const Credentials& rhs) {
  username_.assign(rhs.username());
  key_data_ = rhs.key_data();
  SecureBlob passkey;
  rhs.GetPasskey(&passkey);
  passkey_.swap(passkey);
}

void UsernamePasskey::set_key_data(const KeyData& data) {
  key_data_ = data;
}

const KeyData& UsernamePasskey::key_data() const {
  return key_data_;
}

std::string UsernamePasskey::username() const {
  return username_;
}

std::string UsernamePasskey::GetObfuscatedUsername(
    const brillo::Blob &system_salt) const {
  return BuildObfuscatedUsername(username_, system_salt);
}

void UsernamePasskey::GetPasskey(SecureBlob* passkey) const {
  *passkey = passkey_;
}

}  // namespace cryptohome
