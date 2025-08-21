// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_SHIMS_MOCK_ENVIRONMENT_H_
#define SHILL_SHIMS_MOCK_ENVIRONMENT_H_

#include <string>

#include <gmock/gmock.h>

#include "shill/shims/environment.h"

namespace shill::shims {

// A mock for the Environment class to simulate environment variables.
class MockEnvironment : public Environment {
 public:
  MOCK_METHOD(bool,
              GetVariable,
              (const std::string& name, std::string* value),
              (override));

  // Helper to set up expectations for GetVariable(). If |value| is not null,
  // the call is expected to succeed and set the output value. Otherwise, it's
  // expected to fail.
  void ExpectVariable(const std::string& name, const char* value);
};

}  // namespace shill::shims

#endif  // SHILL_SHIMS_MOCK_ENVIRONMENT_H_
