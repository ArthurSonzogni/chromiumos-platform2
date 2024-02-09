// Copyright 2010 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_MOCK_ACTION_H_
#define UPDATE_ENGINE_COMMON_MOCK_ACTION_H_

#include <string>

#include <gmock/gmock.h>

#include "update_engine/common/action.h"

namespace chromeos_update_engine {

class MockAction;

template <>
class ActionTraits<MockAction> {
 public:
  typedef NoneType OutputObjectType;
  typedef NoneType InputObjectType;
};

class MockAction : public Action<MockAction> {
 public:
  MockAction() {
    ON_CALL(*this, Type()).WillByDefault(testing::Return("MockAction"));
  }

  MOCK_METHOD0(PerformAction, void());
  MOCK_METHOD0(TerminateProcessing, void());
  MOCK_METHOD0(SuspendAction, void());
  MOCK_METHOD0(ResumeAction, void());
  MOCK_CONST_METHOD0(Type, std::string());
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_MOCK_ACTION_H_
