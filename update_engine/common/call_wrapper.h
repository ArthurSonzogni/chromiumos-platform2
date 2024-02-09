// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_CALL_WRAPPER_H_
#define UPDATE_ENGINE_COMMON_CALL_WRAPPER_H_

#include "update_engine/common/call_wrapper_interface.h"

#include <base/files/file_path.h>

namespace chromeos_update_engine {

class CallWrapper : public CallWrapperInterface {
 public:
  CallWrapper() = default;
  CallWrapper(const CallWrapper&) = delete;
  CallWrapper& operator=(const CallWrapper&) = delete;

  ~CallWrapper() = default;

  // CallWrapperInterface overrides.
  int64_t AmountOfFreeDiskSpace(const base::FilePath& path) override;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_CALL_WRAPPER_H_
