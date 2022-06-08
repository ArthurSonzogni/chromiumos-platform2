// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/utils.h"

#include <selinux/restorecon.h>
#include <selinux/selinux.h>

#include <algorithm>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/process/process.h>
#include <rootdev/rootdev.h>

namespace {

// A callback function for SELinux restorecon.
PRINTF_FORMAT(2, 3)
int RestoreConLogCallback(int type, const char* fmt, ...) {
  va_list ap;

  std::string message = "restorecon: ";
  va_start(ap, fmt);
  message += base::StringPrintV(fmt, ap);
  va_end(ap);

  // This already has a line feed at the end, so trim it off to avoid
  // empty lines in the log.
  base::TrimString(message, "\r\n", &message);

  if (type == SELINUX_INFO)
    LOG(INFO) << message;
  else
    LOG(ERROR) << message;

  return 0;
}

}  // namespace

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

void Restorecon(const base::FilePath& path,
                const std::vector<base::FilePath>& exclude,
                bool is_recursive,
                bool set_digests) {
  union selinux_callback cb;
  cb.func_log = RestoreConLogCallback;
  selinux_set_callback(SELINUX_CB_LOG, cb);

  if (!exclude.empty()) {
    std::vector<const char*> exclude_cstring(exclude.size());
    std::transform(exclude.begin(), exclude.end(), exclude_cstring.begin(),
                   [](const base::FilePath& path) -> const char* {
                     return path.value().c_str();
                   });
    exclude_cstring.push_back(NULL);
    // We need to exclude directories because restoring context could
    // mislabel files if the encrypted filename happens to match something
    // or could increase boot time.
    selinux_restorecon_set_exclude_list(exclude_cstring.data());
  }

  const unsigned int recurse_flags =
      (set_digests
           ? SELINUX_RESTORECON_RECURSE
           : SELINUX_RESTORECON_RECURSE | SELINUX_RESTORECON_SKIP_DIGEST);

  const unsigned int base_flags =
      (is_recursive ? recurse_flags : 0) | SELINUX_RESTORECON_REALPATH;

  selinux_restorecon(path.value().c_str(), base_flags);
}

}  // namespace utils
