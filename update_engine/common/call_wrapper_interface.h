// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_CALL_WRAPPER_INTERFACE_H_
#define UPDATE_ENGINE_COMMON_CALL_WRAPPER_INTERFACE_H_

#include <memory>

#include <base/files/file_path.h>

namespace chromeos_update_engine {

// The abstract call_wrapper interface used mainly to intercept libc or system
// calls for testing.
class CallWrapperInterface {
 public:
  CallWrapperInterface(const CallWrapperInterface&) = delete;
  CallWrapperInterface& operator=(const CallWrapperInterface&) = delete;

  virtual ~CallWrapperInterface() = default;

  virtual int64_t AmountOfFreeDiskSpace(const base::FilePath& path) = 0;

 protected:
  CallWrapperInterface() = default;
};

// This factory function creates a new CallWrapperInterface instance for the
// current platform.
std::unique_ptr<CallWrapperInterface> CreateCallWrapper();

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_CALL_WRAPPER_INTERFACE_H_
