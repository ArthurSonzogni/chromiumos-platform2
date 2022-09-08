// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains the functionality for configuring process management.

#ifndef INIT_STARTUP_SECURITY_MANAGER_H_
#define INIT_STARTUP_SECURITY_MANAGER_H_

#include <base/files/file_path.h>
#include <base/files/file_util.h>

#include "init/startup/platform_impl.h"

namespace startup {

bool AccumulatePolicyFiles(const base::FilePath& root,
                           const base::FilePath& output_file,
                           const base::FilePath& policy_dir,
                           bool gid_policies);
bool ConfigureProcessMgmtSecurity(const base::FilePath& root);

// Sets up the LoadPin verity root digests to be trusted by the kernel.
bool SetupLoadPinVerityDigests(const base::FilePath& root, Platform* platform);

}  // namespace startup

#endif  // INIT_STARTUP_SECURITY_MANAGER_H_
