// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains the functionality for configuring process management.

#ifndef INIT_STARTUP_SECURITY_MANAGER_H_
#define INIT_STARTUP_SECURITY_MANAGER_H_

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <libstorage/platform/platform.h>

#include "init/startup/startup_dep_impl.h"

namespace startup {

// Accumulate process management policies from the files in the policy dir
// and append them to to output_file.
bool AccumulatePolicyFiles(libstorage::Platform* platform,
                           const base::FilePath& root,
                           const base::FilePath& output_file,
                           const base::FilePath& policy_dir);
// Determine where securityfs files are placed and accumulate policy files.
bool ConfigureProcessMgmtSecurity(libstorage::Platform* platform,
                                  const base::FilePath& root);

// Sets up the LoadPin verity root digests to be trusted by the kernel.
bool SetupLoadPinVerityDigests(libstorage::Platform* platform,
                               const base::FilePath& root,
                               StartupDep* startup_dep);

// Block symlink and FIFO access on the given path.
bool BlockSymlinkAndFifo(libstorage::Platform* platform,
                         const base::FilePath& root,
                         const std::string& path);

void CreateSystemKey(libstorage::Platform* platform,
                     const base::FilePath& root,
                     const base::FilePath& stateful,
                     StartupDep* startup_dep);

bool AllowSymlink(libstorage::Platform* platform,
                  const base::FilePath& root,
                  const std::string& path);
bool AllowFifo(libstorage::Platform* platform,
               const base::FilePath& root,
               const std::string& path);

void SymlinkExceptions(libstorage::Platform* platform,
                       const base::FilePath& root);
void ExceptionsProjectSpecific(libstorage::Platform* platform,
                               const base::FilePath& root,
                               const base::FilePath& config_dir,
                               bool (*callback)(libstorage::Platform* platform,
                                                const base::FilePath& root,
                                                const std::string& path));

// Set up symlink traversal, FIFO blocking policy, and project specific
// symlink and FIFO exceptions.
void ConfigureFilesystemExceptions(libstorage::Platform* platform,
                                   const base::FilePath& root);

}  // namespace startup

#endif  // INIT_STARTUP_SECURITY_MANAGER_H_
