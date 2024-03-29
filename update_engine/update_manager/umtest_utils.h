// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_UMTEST_UTILS_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_UMTEST_UTILS_H_

#include <iostream>  // NOLINT(readability/streams)
#include <memory>

#include <base/time/time.h>
#include <gtest/gtest.h>

#include "update_engine/update_manager/policy_interface.h"
#include "update_engine/update_manager/variable.h"

namespace chromeos_update_manager {

// A help class with common functionality for use in Update Manager testing.
class UmTestUtils {
 public:
  // A default timeout to use when making various queries.
  static constexpr base::TimeDelta DefaultTimeout() { return kDefaultTimeout; }

  // Calls GetValue on |variable| and expects its result to be |expected|.
  template <typename T>
  static void ExpectVariableHasValue(const T& expected, Variable<T>* variable) {
    ASSERT_NE(nullptr, variable);
    std::unique_ptr<const T> value(
        variable->GetValue(DefaultTimeout(), nullptr));
    ASSERT_NE(nullptr, value.get()) << "Variable: " << variable->GetName();
    EXPECT_EQ(expected, *value) << "Variable: " << variable->GetName();
  }

  // Calls GetValue on |variable| and expects its result to be null.
  template <typename T>
  static void ExpectVariableNotSet(Variable<T>* variable) {
    ASSERT_NE(nullptr, variable);
    std::unique_ptr<const T> value(
        variable->GetValue(DefaultTimeout(), nullptr));
    EXPECT_EQ(nullptr, value.get()) << "Variable: " << variable->GetName();
  }

 private:
  static constexpr base::TimeDelta kDefaultTimeout = base::Seconds(1);
};

// PrintTo() functions are used by gtest to print these values. They need to be
// defined on the same namespace where the type was defined.
void PrintTo(const EvalStatus& status, ::std::ostream* os);

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_UMTEST_UTILS_H_
