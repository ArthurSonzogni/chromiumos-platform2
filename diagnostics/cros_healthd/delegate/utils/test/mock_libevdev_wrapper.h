// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_TEST_MOCK_LIBEVDEV_WRAPPER_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_TEST_MOCK_LIBEVDEV_WRAPPER_H_

#include "diagnostics/cros_healthd/delegate/utils/libevdev_wrapper.h"

#include <string>

#include <gmock/gmock.h>

namespace diagnostics::test {

class MockLibevdevWrapper : public LibevdevWrapper {
 public:
  MockLibevdevWrapper() = default;
  MockLibevdevWrapper(const MockLibevdevWrapper&) = delete;
  MockLibevdevWrapper& operator=(const MockLibevdevWrapper&) = delete;
  ~MockLibevdevWrapper() = default;

  // LibevdevWrapper overrides:
  MOCK_METHOD(bool, HasProperty, (unsigned int prop), (override));
  MOCK_METHOD(bool, HasEventType, (unsigned int type), (override));
  MOCK_METHOD(bool,
              HasEventCode,
              (unsigned int type, unsigned int code),
              (override));
  // Set a default implementation since it's currnetly only for logging. It can
  // be changed to a MOCK_METHOD when needed.
  std::string GetName() override { return "Mock device name"; }
  MOCK_METHOD(int, GetIdBustype, (), (override));
  MOCK_METHOD(int, GetAbsMaximum, (unsigned int code), (override));
  MOCK_METHOD(int,
              GetEventValue,
              (unsigned int type, unsigned int code),
              (override));
  MOCK_METHOD(int, GetNumSlots, (), (override));
  MOCK_METHOD(int,
              FetchSlotValue,
              (unsigned int slot, unsigned int code, int* value),
              (override));
  MOCK_METHOD(int,
              NextEvent,
              (unsigned int flags, input_event* ev),
              (override));
};

}  // namespace diagnostics::test

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_TEST_MOCK_LIBEVDEV_WRAPPER_H_
