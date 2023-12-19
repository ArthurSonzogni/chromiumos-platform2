// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/floss_battery_provider.h"

#include <gtest/gtest.h>
#include "power_manager/powerd/testing/test_environment.h"

namespace power_manager::system {

class FlossBatteryProviderTest : public TestEnvironment {
 public:
  FlossBatteryProviderTest() {}
  FlossBatteryProviderTest(const FlossBatteryProviderTest&) = delete;
  FlossBatteryProviderTest& operator=(const FlossBatteryProviderTest&) = delete;

  ~FlossBatteryProviderTest() override = default;

  void SetUp() override {}

 protected:
  void TestInit() {}

  // Object to test.
  FlossBatteryProvider floss_battery_provider_;
};

}  // namespace power_manager::system
