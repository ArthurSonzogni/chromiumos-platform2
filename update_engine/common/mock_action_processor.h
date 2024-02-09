// Copyright 2010 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_MOCK_ACTION_PROCESSOR_H_
#define UPDATE_ENGINE_COMMON_MOCK_ACTION_PROCESSOR_H_

#include <deque>
#include <memory>
#include <utility>

#include <gmock/gmock.h>

#include "update_engine/common/action.h"

namespace chromeos_update_engine {

class MockActionProcessor : public ActionProcessor {
 public:
  MOCK_METHOD0(StartProcessing, void());
  MOCK_METHOD1(EnqueueAction, void(AbstractAction* action));

  MOCK_METHOD2(ActionComplete, void(AbstractAction*, ErrorCode));

  // This is a legacy workaround described in:
  // https://github.com/google/googletest/blob/HEAD/docs/gmock_cook_book.md#legacy-workarounds-for-move-only-types-legacymoveonly
  void EnqueueAction(std::unique_ptr<AbstractAction> action) override {
    EnqueueAction(action.get());
  }
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_MOCK_ACTION_PROCESSOR_H_
