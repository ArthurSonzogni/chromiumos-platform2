// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/process/process.h>
#include <rootdev/rootdev.h>
#include "init/utils.h"

namespace utils {

// |strip_partition| attempts to remove the partition number from the result.
bool GetRootDevice(base::FilePath* root, bool strip_partition) {
  char buf[PATH_MAX];
  int ret = rootdev(buf, PATH_MAX, true, strip_partition);
  if (ret == 0) {
    *root = base::FilePath(buf);
  } else {
    *root = base::FilePath();
  }
  return !ret;
}

bool ReadFileToInt(const base::FilePath& path, int* value) {
  std::string str;
  if (!base::ReadFileToString(path, &str)) {
    return false;
  }
  base::TrimWhitespaceASCII(str, base::TRIM_ALL, &str);
  return base::StringToInt(str, value);
}

bool CreateEncryptedRebootVault() {
  brillo::ProcessImpl create_erv;
  create_erv.AddArg("/usr/sbin/encrypted-reboot-vault");
  create_erv.AddArg("--action=create");
  if (create_erv.Run() != 0) {
    return false;
  }
  return true;
}

bool UnlockEncryptedRebootVault() {
  brillo::ProcessImpl unlock_erv;
  unlock_erv.AddArg("/usr/sbin/encrypted-reboot-vault");
  unlock_erv.AddArg("--action=unlock");
  if (unlock_erv.Run() != 0) {
    return false;
  }
  return true;
}

void Reboot() {
  brillo::ProcessImpl proc;
  proc.AddArg("/sbin/shutdown");
  proc.AddArg("-r");
  proc.AddArg("now");
  int ret = proc.Run();
  if (ret == 0) {
    // Wait for reboot to finish (it's an async call).
    sleep(60 * 60 * 24);
  }
  // If we've reached here, reboot (probably) failed.
  LOG(ERROR) << "Requesting reboot failed with failure code " << ret;
}

}  // namespace utils
