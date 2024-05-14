// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_STARTUP_MOCK_STARTUP_DEP_IMPL_H_
#define INIT_STARTUP_MOCK_STARTUP_DEP_IMPL_H_

#include <string>

#include <gmock/gmock.h>

#include "init/startup/startup_dep_impl.h"

namespace startup {

class MockStartupDep : public StartupDep {
 public:
  MockStartupDep() = default;

  MockStartupDep(const MockStartupDep&) = delete;
  MockStartupDep& operator=(const MockStartupDep&) = delete;

  MOCK_METHOD(void, RunProcess, (const base::FilePath& cmd_path), (override));
};

}  // namespace startup

#endif  // INIT_STARTUP_MOCK_STARTUP_DEP_IMPL_H_
