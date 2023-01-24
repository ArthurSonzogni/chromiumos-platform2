// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/syslog_logging.h>

#include "oobe_config/filesystem/file_handler.h"

// Cleans up after a rollback happened by deleting any remaining files.
// Should be called once the device is owned.
int main(int argc, char* argv[]) {
  oobe_config::FileHandler file_handler;
  file_handler.RemoveRestorePath();
  file_handler.RemoveOpensslEncryptedRollbackData();
  return 0;
}
