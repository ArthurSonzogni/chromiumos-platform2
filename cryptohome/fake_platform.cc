// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/fake_platform.h"

#include <stdint.h>

#include <linux/fs.h>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

#include <brillo/secure_blob.h>

#include "cryptohome/filesystem_layout.h"

namespace cryptohome {

// Constructor/destructor

FakePlatform::FakePlatform() {
  brillo::SecureBlob system_salt;
  InitializeFilesystemLayout(this, &system_salt);
  SetSystemSaltForLibbrillo(system_salt);
}

FakePlatform::~FakePlatform() {
  RemoveSystemSaltForLibbrillo();
}

// Test API

void FakePlatform::SetSystemSaltForLibbrillo(const brillo::SecureBlob& salt) {
  CHECK(!old_salt_);
  std::string* brillo_salt = new std::string();
  brillo_salt->resize(salt.size());
  brillo_salt->assign(reinterpret_cast<const char*>(salt.data()), salt.size());
  old_salt_ = brillo::cryptohome::home::GetSystemSalt();
  brillo::cryptohome::home::SetSystemSalt(brillo_salt);
}

void FakePlatform::RemoveSystemSaltForLibbrillo() {
  std::string* salt = brillo::cryptohome::home::GetSystemSalt();
  brillo::cryptohome::home::SetSystemSalt(old_salt_);
  delete salt;
  old_salt_ = nullptr;
}

}  // namespace cryptohome
