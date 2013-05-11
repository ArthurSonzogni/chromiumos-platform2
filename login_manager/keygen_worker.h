// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/file_path.h>

namespace login_manager {
class NssUtil;

namespace keygen {

// Generates a keypair using the NSSDB under user_homedir, extracts
// the public half and stores it at file_path.
int GenerateKey(const base::FilePath& file_path,
                const base::FilePath& user_homedir,
                NssUtil* nss);

}  // namespace keygen

}  // namespace login_manager
