// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A token manager provides a set of methods for login agents to create and
// validate token files.  This is not currently used on ChromeOS, where
// Cryptohome does this job.

#include "chaps/token_file_manager.h"

#include <string>

#include <base/logging.h>
#include <chromeos/secure_blob.h>

#include "chaps/chaps_utility.h"

using std::string;
using base::FilePath;
using chromeos::SecureBlob;

namespace chaps {

TokenFileManager::TokenFileManager(uid_t chapsd_uid, gid_t chapsd_gid)
    : chapsd_uid_(chapsd_uid),
      chapsd_gid_(chapsd_gid) { }

TokenFileManager::~TokenFileManager() { }

bool TokenFileManager::GetUserTokenPath(const string& user,
                                        FilePath* token_path) {
  NOTREACHED();
  return false;
}

bool TokenFileManager::CreateUserTokenDirectory(const FilePath& token_path) {
  NOTREACHED();
  return false;
}

bool TokenFileManager::CheckUserTokenPermissions(const FilePath& token_path) {
  NOTREACHED();
  return false;
}

bool TokenFileManager::SaltAuthData(const FilePath& token_path,
                                    const SecureBlob& auth_data,
                                    SecureBlob* salted_auth_data) {
  NOTREACHED();
  return false;
}

}
