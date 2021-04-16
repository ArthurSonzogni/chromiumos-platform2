// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/values.h>

#include "runtime_probe/functions/sysfs.h"
#include "runtime_probe/utils/file_utils.h"

namespace runtime_probe {

std::unique_ptr<SysfsFunction> SysfsFunction::FromKwargsValue(
    const base::Value& dict_value) {
  PARSE_BEGIN(SysfsFunction);
  PARSE_ARGUMENT(dir_path);
  PARSE_ARGUMENT(keys);
  PARSE_ARGUMENT(optional_keys, {});
  PARSE_END();
}

SysfsFunction::DataType SysfsFunction::EvalImpl() const {
  DataType result{};
  const base::FilePath glob_path{dir_path_};

  if (!base::FilePath{"/sys/"}.IsParent(glob_path)) {
    if (sysfs_path_for_testing_.empty()) {
      LOG(ERROR) << glob_path.value() << " is not under /sys/";
      return {};
    }
    // While testing, |sysfs_path_for_testing_| can be set to allow additional
    // path.
    if (sysfs_path_for_testing_.IsParent(glob_path) ||
        sysfs_path_for_testing_ == glob_path.DirName()) {
      LOG(WARNING) << glob_path.value() << " is allowed because "
                   << "sysfs_path_for_testing_ is set to "
                   << sysfs_path_for_testing_.value();
    } else {
      LOG(ERROR) << glob_path.value() << " is neither under under /sys/ nor "
                 << sysfs_path_for_testing_.value();
      return {};
    }
  }

  for (const auto& sysfs_path : Glob(glob_path)) {
    auto dict_value = MapFilesToDict(sysfs_path, keys_, optional_keys_);
    if (dict_value)
      result.push_back(std::move(*dict_value));
  }
  return result;
}

}  // namespace runtime_probe
