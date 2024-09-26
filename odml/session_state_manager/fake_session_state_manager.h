// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#ifndef ODML_SESSION_STATE_MANAGER_FAKE_SESSION_STATE_MANAGER_H_
#define ODML_SESSION_STATE_MANAGER_FAKE_SESSION_STATE_MANAGER_H_

#include "odml/session_state_manager/session_state_manager.h"

namespace odml {

class FakeSessionStateManager : public SessionStateManagerInterface {
 public:
  MOCK_METHOD(void, AddObserver, (Observer * observer), (override));
  MOCK_METHOD(void, RemoveObserver, (Observer * observer), (override));
};

}  // namespace odml

#endif  // ODML_SESSION_STATE_MANAGER_FAKE_SESSION_STATE_MANAGER_H_
