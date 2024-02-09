// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_manager/real_time_provider.h"

#include <memory>
#include <tuple>

#include <base/logging.h>
#include <base/time/time.h>
#include <gtest/gtest.h>

#include "update_engine/cros/fake_system_state.h"
#include "update_engine/update_manager/umtest_utils.h"

using base::Time;
using chromeos_update_engine::FakeSystemState;
using std::unique_ptr;

namespace chromeos_update_manager {

class UmRealTimeProviderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    FakeSystemState::CreateInstance();
    // The provider initializes correctly.
    provider_.reset(new RealTimeProvider());
    ASSERT_NE(nullptr, provider_.get());
    ASSERT_TRUE(provider_->Init());
  }

  // Generates a fixed timestamp for use in faking the current time.
  Time CurrTime() {
    Time::Exploded now_exp;
    now_exp.year = 2014;
    now_exp.month = 3;
    now_exp.day_of_week = 2;
    now_exp.day_of_month = 18;
    now_exp.hour = 8;
    now_exp.minute = 5;
    now_exp.second = 33;
    now_exp.millisecond = 675;
    Time time;
    std::ignore = Time::FromLocalExploded(now_exp, &time);
    return time;
  }

  unique_ptr<RealTimeProvider> provider_;
};

TEST_F(UmRealTimeProviderTest, CurrDateValid) {
  const Time now = CurrTime();
  Time::Exploded exploded;
  now.LocalExplode(&exploded);
  exploded.hour = 0;
  exploded.minute = 0;
  exploded.second = 0;
  exploded.millisecond = 0;
  Time expected;
  std::ignore = Time::FromLocalExploded(exploded, &expected);

  FakeSystemState::Get()->fake_clock()->SetWallclockTime(now);
  UmTestUtils::ExpectVariableHasValue(expected, provider_->var_curr_date());
}

TEST_F(UmRealTimeProviderTest, CurrHourValid) {
  const Time now = CurrTime();
  Time::Exploded expected;
  now.LocalExplode(&expected);
  FakeSystemState::Get()->fake_clock()->SetWallclockTime(now);
  UmTestUtils::ExpectVariableHasValue(expected.hour,
                                      provider_->var_curr_hour());
}

TEST_F(UmRealTimeProviderTest, CurrMinuteValid) {
  const Time now = CurrTime();
  Time::Exploded expected;
  now.LocalExplode(&expected);
  FakeSystemState::Get()->fake_clock()->SetWallclockTime(now);
  UmTestUtils::ExpectVariableHasValue(expected.minute,
                                      provider_->var_curr_minute());
}

}  // namespace chromeos_update_manager
