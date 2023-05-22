// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/util/test_support_callbacks.h"
#include "base/task/sequenced_task_runner.h"

namespace reporting::test {

TestCallbackWaiter::TestCallbackWaiter()
    : sequenced_task_runner_(base::SequencedTaskRunner::GetCurrentDefault()) {}
TestCallbackWaiter::~TestCallbackWaiter() = default;

TestCallbackAutoWaiter::TestCallbackAutoWaiter() {
  Attach();
}
TestCallbackAutoWaiter::~TestCallbackAutoWaiter() {
  Wait();
}

}  // namespace reporting::test
