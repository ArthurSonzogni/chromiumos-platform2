// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/testing/test_environment.h"

#include <gtest/gtest.h>

#include <base/test/task_environment.h>

namespace power_manager {

TestEnvironment::TestEnvironment()
    : task_environment_(base::test::TaskEnvironment::MainThreadType::IO,
                        base::test::TaskEnvironment::TimeSource::SYSTEM_TIME) {}

}  // namespace power_manager
