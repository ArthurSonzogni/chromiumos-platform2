// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_ROUTINES_URANDOM_DELEGATE_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_ROUTINES_URANDOM_DELEGATE_H_

#include <memory>

#include <base/files/file.h>

#include "diagnostics/cros_healthd/delegate/routines/cpu_routine_task_delegate.h"

namespace diagnostics {

class UrandomDelegate : public CpuRoutineTaskDelegate {
 public:
  // Number of bytes to read from urandom.
  static constexpr int kNumBytesRead = 1024 * 1024;

  // Creates and returns a UrandomDelegate, or null if the creation fails.
  static std::unique_ptr<UrandomDelegate> Create();

  UrandomDelegate();
  UrandomDelegate(const UrandomDelegate&) = delete;
  UrandomDelegate& operator=(const UrandomDelegate&) = delete;
  ~UrandomDelegate();

  // Executes urandom task. Returns true if task is completed without any error,
  // false otherwise.
  bool Run() override;

 protected:
  // Used only by the factory function.
  explicit UrandomDelegate(base::File urandom_file);

 private:
  // The opened urandom file.
  base::File urandom_file_;
  // The buffer to read data from urandom. Declared as a class member to avoid
  // allocating memory on stack when each time `Run()` is called.
  char urandom_data_[kNumBytesRead];
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_ROUTINES_URANDOM_DELEGATE_H_
