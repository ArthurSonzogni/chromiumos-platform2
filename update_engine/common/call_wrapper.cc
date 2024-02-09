// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/common/call_wrapper.h"

#include <memory>

#include <base/files/file_path.h>
#include <base/system/sys_info.h>

namespace chromeos_update_engine {

std::unique_ptr<CallWrapperInterface> CreateCallWrapper() {
  return std::make_unique<CallWrapper>();
}

int64_t CallWrapper::AmountOfFreeDiskSpace(const base::FilePath& path) {
  return base::SysInfo::AmountOfFreeDiskSpace(path);
}

}  // namespace chromeos_update_engine
