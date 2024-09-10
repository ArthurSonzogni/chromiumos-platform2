// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_MOCK_DAEMON_DELEGATE_H_
#define MODEMFWD_MOCK_DAEMON_DELEGATE_H_

#include <string>

#include <brillo/errors/error.h>
#include <gmock/gmock.h>

#include "modemfwd/daemon_delegate.h"

namespace modemfwd {

class MockDelegate : public Delegate {
 public:
  MockDelegate() = default;
  ~MockDelegate() override = default;

  MOCK_METHOD(void, TaskUpdated, (Task*), (override));
  MOCK_METHOD(void, FinishTask, (Task*, brillo::ErrorPtr), (override));
  MOCK_METHOD(void,
              ForceFlashForTesting,
              (const std::string&,
               const std::string&,
               const std::string&,
               bool,
               base::OnceCallback<void(const brillo::ErrorPtr&)>),
              (override));
  MOCK_METHOD(bool, ResetModem, (const std::string&), (override));
  MOCK_METHOD(void, NotifyFlashStarting, (const std::string&), (override));
  MOCK_METHOD(void,
              RegisterOnStartFlashingCallback,
              (const std::string&, base::OnceClosure),
              (override));
  MOCK_METHOD(void,
              RegisterOnModemReappearanceCallback,
              (const std::string&, base::OnceClosure),
              (override));
  MOCK_METHOD(void,
              RegisterOnModemStateChangedCallback,
              (const std::string&, base::RepeatingClosure),
              (override));
  MOCK_METHOD(void,
              RegisterOnModemPowerStateChangedCallback,
              (const std::string&, base::RepeatingClosure),
              (override));
};

}  // namespace modemfwd

#endif  // MODEMFWD_MOCK_DAEMON_DELEGATE_H_
