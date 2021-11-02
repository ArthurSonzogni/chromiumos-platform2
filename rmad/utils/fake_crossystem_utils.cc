// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/fake_crossystem_utils.h"

#include <string>
#include <unordered_set>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>

#include "rmad/constants.h"

namespace {

const std::unordered_set<std::string> read_only_values = {"wpsw_cur"};

}  // namespace

namespace rmad {
namespace fake {

FakeCrosSystemUtils::FakeCrosSystemUtils(const base::FilePath& working_dir_path)
    : CrosSystemUtils(), working_dir_path_(working_dir_path) {
  json_store_ = base::MakeRefCounted<JsonStore>(
      working_dir_path_.AppendASCII(kCrosSystemFilePath));
  CHECK(!json_store_->ReadOnly());
}

bool FakeCrosSystemUtils::SetInt(const std::string& key, int value) {
  if (read_only_values.count(key)) {
    return false;
  }
  return json_store_->SetValue(key, value);
}

bool FakeCrosSystemUtils::GetInt(const std::string& key, int* value) const {
  // "wpsw_cur" is a special case. It is determined by HWWP status and cr50
  // factory mode.
  if (key == "wpsw_cur") {
    const base::FilePath factory_mode_enabled_file_path =
        working_dir_path_.AppendASCII(kFactoryModeEnabledFilePath);
    const base::FilePath hwwp_disabled_file_path =
        working_dir_path_.AppendASCII(kHwwpDisabledFilePath);
    if (base::PathExists(factory_mode_enabled_file_path) ||
        base::PathExists(hwwp_disabled_file_path)) {
      *value = 0;
    } else {
      *value = 1;
    }
    return true;
  }
  return json_store_->GetValue(key, value);
}

bool FakeCrosSystemUtils::SetString(const std::string& key,
                                    const std::string& value) {
  if (read_only_values.count(key)) {
    return false;
  }
  return json_store_->SetValue(key, value);
}

bool FakeCrosSystemUtils::GetString(const std::string& key,
                                    std::string* value) const {
  if (key == "wpsw_cur") {
    return false;
  }
  return json_store_->GetValue(key, value);
}

}  // namespace fake
}  // namespace rmad
