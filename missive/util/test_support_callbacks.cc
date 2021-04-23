// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/util/test_support_callbacks.h"

#include <base/run_loop.h>

namespace reporting {
namespace test {

TestCallbackWaiter::TestCallbackWaiter()
    : run_loop_(base::RunLoop::Type::kNestableTasksAllowed) {}
TestCallbackWaiter::~TestCallbackWaiter() = default;

TestCallbackAutoWaiter::TestCallbackAutoWaiter() {
  Attach();
}
TestCallbackAutoWaiter::~TestCallbackAutoWaiter() {
  Wait();
}

}  // namespace test
}  // namespace reporting
