// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/system/fake_power_manager_client.h"

#include <base/files/file_path.h>
#include <brillo/file_utils.h>

#include "rmad/constants.h"

namespace rmad {
namespace fake {

FakePowerManagerClient::FakePowerManagerClient(
    const base::FilePath& working_dir_path)
    : PowerManagerClient(), working_dir_path_(working_dir_path) {}

bool FakePowerManagerClient::Restart() {
  const base::FilePath reboot_request_file_path =
      working_dir_path_.AppendASCII(kRebootRequestFilePath);
  brillo::TouchFile(reboot_request_file_path);
  return true;
}

bool FakePowerManagerClient::Shutdown() {
  const base::FilePath shutdown_request_file_path =
      working_dir_path_.AppendASCII(kShutdownRequestFilePath);
  brillo::TouchFile(shutdown_request_file_path);
  return true;
}

}  // namespace fake
}  // namespace rmad
