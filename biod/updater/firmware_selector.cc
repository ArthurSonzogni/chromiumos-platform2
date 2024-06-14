// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/updater/firmware_selector.h"

#include <base/files/file_util.h>
#include <base/logging.h>

namespace {

constexpr char kUseTestFirmwareFile[] = ".use_test_firmware";
constexpr char kFirmwareDir[] = "/opt/google/biod/fw";
constexpr char kTestFirmwareSubdir[] = "test";

}  // namespace

namespace biod {

base::FilePath FirmwareSelector::GetFirmwarePath() const {
  base::FilePath firmware_path(kFirmwareDir);

  if (base::PathExists(base_path_.Append(kUseTestFirmwareFile))) {
    LOG(INFO) << "Test firmware was requested.";
    firmware_path = firmware_path.Append(kTestFirmwareSubdir);
  }

  return firmware_path;
}

}  // namespace biod
