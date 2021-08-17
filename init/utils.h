// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_UTILS_H_
#define INIT_UTILS_H_

#include <string>

#include <base/files/file.h>
#include <base/files/file_path.h>

namespace utils {

// Try to set root to the root device filepath, optionally removing the
// partition number
bool GetRootDevice(base::FilePath* root, bool strip_partition);

// Helper function to read a file to int
bool ReadFileToInt(const base::FilePath& path, int* value);

// Run encrypted-reboot-vault --action=create
bool CreateEncryptedRebootVault();

// Run encrypted-reboot-vault --action=unlock
bool UnlockEncryptedRebootVault();

}  // namespace utils

#endif  // INIT_UTILS_H_
