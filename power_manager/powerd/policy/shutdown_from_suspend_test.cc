// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/shutdown_from_suspend.h"

#include <gtest/gtest.h>

#include "power_manager/common/fake_prefs.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/common/test_main_loop_runner.h"
#include "power_manager/powerd/system/power_supply_stub.h"
#include "power_manager/powerd/system/suspend_configurator_stub.h"
#include "power_manager/powerd/system/wakeup_timer.h"
#include "power_manager/powerd/testing/test_environment.h"

namespace power_manager::policy {

namespace {
constexpr auto kRunLoopDelay = base::Milliseconds(200);
constexpr auto kHibernateAfter = base::Seconds(1);
constexpr auto kShutdownAfter = base::Seconds(2);
}  // namespace

class ShutdownFromSuspendTest : public TestEnvironment {
 public:
  ShutdownFromSuspendTest()
      : shutdown_from_suspend_(
            std::make_unique<power_manager::system::TestWakeupTimer>(),
            std::make_unique<power_manager::system::TestWakeupTimer>()) {}
  ShutdownFromSuspendTest(const ShutdownFromSuspendTest&) = delete;
  ShutdownFromSuspendTest& operator=(const ShutdownFromSuspendTest&) = delete;

  ~ShutdownFromSuspendTest() override = default;

 protected:
  void Init(bool enable_dark_resume = true,
            bool enable_hibernate = true,
            base::TimeDelta shutdown_after = kShutdownAfter,
            base::TimeDelta hibernate_after = kHibernateAfter) {
    prefs_.SetInt64(kLowerPowerFromSuspendSecPref, shutdown_after.InSeconds());
    prefs_.SetInt64(kDisableDarkResumePref, enable_dark_resume ? 0 : 1);
    if (!enable_dark_resume || !enable_hibernate)
      configurator_stub_.force_hibernate_unavailable_for_testing();

    shutdown_from_suspend_.Init(&prefs_, &power_supply_, &configurator_stub_);

    PowerManagementPolicy policy;
    policy.set_hibernate_delay_sec(hibernate_after.InSeconds());
    shutdown_from_suspend_.HandlePolicyChange(policy);
  }

  void SetLinePower(bool line_power) {
    system::PowerStatus status;
    status.line_power_on = line_power;
    power_supply_.set_status(status);
  }

  void SetPowerStatus(bool low_battery, bool line_power) {
    system::PowerStatus status;
    status.battery_below_shutdown_threshold = low_battery;
    status.line_power_on = line_power;
    power_supply_.set_status(status);
  }

  ShutdownFromSuspend shutdown_from_suspend_;
  FakePrefs prefs_;
  system::PowerSupplyStub power_supply_;
  system::SuspendConfiguratorStub configurator_stub_;
  TestMainLoopRunner runner_;
};

// Test that ShutdownFromSuspend is enabled and hibernate is disabled when
//  1. Dark resume is enabled
//  2. Hibernate is disabled
//  3. |kLowerPowerFromSuspendSecPref| value is set to positive
//     integer.
TEST_F(ShutdownFromSuspendTest, TestShutdownEnable) {
  Init(true, false);
  EXPECT_TRUE(shutdown_from_suspend_.enabled_for_testing());
  EXPECT_FALSE(shutdown_from_suspend_.hibernate_enabled_for_testing());
}

// Test that ShutdownFromSuspend and hibernate are enabled when
//  1. Dark resume is enabled
//  2. Hibernate is enabled
//  3. |kLowerPowerFromSuspendSecPref| value is set to positive
//     integer.
TEST_F(ShutdownFromSuspendTest, TestHibernateEnable) {
  Init(true, true);
  EXPECT_TRUE(shutdown_from_suspend_.enabled_for_testing());
  EXPECT_TRUE(shutdown_from_suspend_.hibernate_enabled_for_testing());
}

// Test that ShutdownFromSuspend and hibernate are disabled when dark resume
// is disabled (even if hibernate is otherwise enabled).
TEST_F(ShutdownFromSuspendTest, TestDarkResumeDisabled) {
  Init(false, true);
  EXPECT_FALSE(shutdown_from_suspend_.enabled_for_testing());
  EXPECT_FALSE(shutdown_from_suspend_.hibernate_enabled_for_testing());
}

// Test that ShutdownFromSuspend and hibernate are disabled when
// |kLowerPowerFromSuspendSecPref| value is set to 0.
TEST_F(ShutdownFromSuspendTest, TestkLowerPowerFromSuspendSecPref0) {
  Init(true, true, base::Seconds(0));
  EXPECT_FALSE(shutdown_from_suspend_.enabled_for_testing());
  EXPECT_FALSE(shutdown_from_suspend_.hibernate_enabled_for_testing());
}

// Test that ShutdownFromSuspend is enabled but hibernate is disabled
// if hibernate is reported as unavailable by the configurator.
TEST_F(ShutdownFromSuspendTest, TestHibernateNotAvailable) {
  Init(true, false);
  EXPECT_TRUE(shutdown_from_suspend_.enabled_for_testing());
  EXPECT_FALSE(shutdown_from_suspend_.hibernate_enabled_for_testing());
}

// Test that ShutdownFromSuspend asks the system to shut down when
// 1. ShutdownFromSuspend is enabled
// 2. Hibernate is disabled
// 3. Device has spent |kLowerPowerFromSuspendSecPref| in suspend
// 4. Device is not on line power when dark resumed.
TEST_F(ShutdownFromSuspendTest, TestShutdownPath) {
  Init(true, false, kShutdownAfter);
  // First |PrepareForSuspendAttempt| after boot should always return
  // Action::SUSPEND
  EXPECT_EQ(shutdown_from_suspend_.PrepareForSuspendAttempt(),
            ShutdownFromSuspend::Action::SUSPEND);
  base::TimeDelta run_loop_for = kShutdownAfter + kRunLoopDelay;
  runner_.StartLoop(run_loop_for);
  // Fake a dark resume.
  shutdown_from_suspend_.HandleDarkResume();
  EXPECT_EQ(shutdown_from_suspend_.PrepareForSuspendAttempt(),
            ShutdownFromSuspend::Action::SHUT_DOWN);
}

// Test that ShutdownFromSuspend asks the system to hibernate when
// 1. ShutdownFromSuspend is enabled
// 2. Hibernate is enabled
// 3. Device has spent |kLowerPowerFromSuspendSecPref| in suspend
TEST_F(ShutdownFromSuspendTest, TestHibernatePath) {
  Init(true, true);
  // First |PrepareForSuspendAttempt| after boot should always return
  // Action::SUSPEND
  EXPECT_EQ(shutdown_from_suspend_.PrepareForSuspendAttempt(),
            ShutdownFromSuspend::Action::SUSPEND);
  base::TimeDelta run_loop_for = kHibernateAfter + kRunLoopDelay;
  runner_.StartLoop(run_loop_for);
  // Fake a dark resume.
  shutdown_from_suspend_.HandleDarkResume();
  EXPECT_EQ(shutdown_from_suspend_.PrepareForSuspendAttempt(),
            ShutdownFromSuspend::Action::HIBERNATE);
}

// Test that ShutdownFromSuspend asks the system to suspend if the device is on
// line power and hibernate is disabled.
TEST_F(ShutdownFromSuspendTest, TestOnLinePower) {
  Init(true, false);
  shutdown_from_suspend_.PrepareForSuspendAttempt();
  base::TimeDelta run_loop_for = kShutdownAfter + kRunLoopDelay;
  runner_.StartLoop(run_loop_for);
  // Fake a dark resume with line power. PrepareForSuspendAttempt|
  // should return Action::SUSPEND.
  SetLinePower(true);
  shutdown_from_suspend_.HandleDarkResume();
  EXPECT_EQ(shutdown_from_suspend_.PrepareForSuspendAttempt(),
            ShutdownFromSuspend::Action::SUSPEND);
}

// Test that ShutdownFromSuspend asks the system to shutdown if the device is
// not on line power and hibernate is disabled.
TEST_F(ShutdownFromSuspendTest, TestNotOnLinePower) {
  Init(true, false);
  shutdown_from_suspend_.PrepareForSuspendAttempt();
  base::TimeDelta run_loop_for = kShutdownAfter + kRunLoopDelay;
  runner_.StartLoop(run_loop_for);
  // Fake a dark resume without line power. PrepareForSuspendAttempt|
  // should return Action::SHUT_DOWN.
  SetLinePower(false);
  shutdown_from_suspend_.HandleDarkResume();
  EXPECT_EQ(shutdown_from_suspend_.PrepareForSuspendAttempt(),
            ShutdownFromSuspend::Action::SHUT_DOWN);
}

//  This test will validate that we will hibernate on a dark resume when the
//  battery is low even if the minimum time has not been met.
TEST_F(ShutdownFromSuspendTest, TestHibernateEnabledLowBatteryDarkResume) {
  Init();

  // We expect to suspend initially as normal.
  SetPowerStatus(/* low_battery= */ false, /* line_power= */ false);
  shutdown_from_suspend_.PrepareForSuspendAttempt();
  base::TimeDelta run_loop_for = kRunLoopDelay;
  runner_.StartLoop(run_loop_for);
  EXPECT_EQ(shutdown_from_suspend_.PrepareForSuspendAttempt(),
            ShutdownFromSuspend::Action::SUSPEND);

  shutdown_from_suspend_.HandleDarkResume();
  SetPowerStatus(/* low_battery= */ true, /* line_power= */ false);
  // Change the power state to low battery and even though we have not hit our
  // minimum time we will hibernate.
  EXPECT_EQ(shutdown_from_suspend_.PrepareForSuspendAttempt(),
            ShutdownFromSuspend::Action::HIBERNATE);
}

// This test just confirms that we will hibernate after our minimum time.
TEST_F(ShutdownFromSuspendTest, TestHibernateEnabledHibernateAfterMinTime) {
  Init();
  shutdown_from_suspend_.PrepareForSuspendAttempt();
  // We haven't been running long enough to hibernate.
  runner_.StartLoop(base::Milliseconds(500));
  shutdown_from_suspend_.HandleDarkResume();
  EXPECT_EQ(shutdown_from_suspend_.PrepareForSuspendAttempt(),
            ShutdownFromSuspend::Action::SUSPEND);

  shutdown_from_suspend_.HandleDarkResume();
  runner_.StartLoop(base::Milliseconds(501));
  EXPECT_EQ(shutdown_from_suspend_.PrepareForSuspendAttempt(),
            ShutdownFromSuspend::Action::HIBERNATE);
}

// Test that ShutdownFromSuspend asks the policy to suspend when in full
// resume.
TEST_F(ShutdownFromSuspendTest, TestFullResume) {
  Init(true, true, kShutdownAfter);
  shutdown_from_suspend_.PrepareForSuspendAttempt();
  base::TimeDelta run_loop_for = kShutdownAfter + kRunLoopDelay;
  runner_.StartLoop(run_loop_for);
  // Fake a full resume.
  shutdown_from_suspend_.HandleFullResume();
  // Now |PrepareForSuspendAttempt| should return Action::SUSPEND
  EXPECT_EQ(shutdown_from_suspend_.PrepareForSuspendAttempt(),
            ShutdownFromSuspend::Action::SUSPEND);
}

// This test confirms that we're rechecking that hibernate is available after
// our timer may have fired.
TEST_F(ShutdownFromSuspendTest,
       TestHibernateBecomesUnavailableAfterTimerStarted) {
  Init();
  shutdown_from_suspend_.PrepareForSuspendAttempt();
  // We haven't been running long enough to hibernate.
  runner_.StartLoop(base::Milliseconds(500));
  EXPECT_EQ(shutdown_from_suspend_.PrepareForSuspendAttempt(),
            ShutdownFromSuspend::Action::SUSPEND);

  configurator_stub_.force_hibernate_unavailable_for_testing();
  // Now run for another 150ms and although we met our time cutoff hibernate
  // is now unavailable so we will suspend again.
  shutdown_from_suspend_.HandleDarkResume();
  runner_.StartLoop(base::Milliseconds(600));
  EXPECT_EQ(shutdown_from_suspend_.PrepareForSuspendAttempt(),
            ShutdownFromSuspend::Action::SUSPEND);
}

// This test will confirm that we do not hibernate when on line power, even if
// we're eligible to.
TEST_F(ShutdownFromSuspendTest,
       TestHibernateEnabledOnLinePowerDoesntHibernate) {
  shutdown_from_suspend_.PrepareForSuspendAttempt();
  // We haven't been running long enough to hibernate.
  runner_.StartLoop(base::Milliseconds(500));
  EXPECT_EQ(shutdown_from_suspend_.PrepareForSuspendAttempt(),
            ShutdownFromSuspend::Action::SUSPEND);

  // Now run for another 1 second but since we're on line power we won't
  // hibernate.
  SetPowerStatus(/* low_battery = */ false, /* line_power= */ true);
  shutdown_from_suspend_.HandleDarkResume();
  runner_.StartLoop(base::Seconds(1));
  EXPECT_EQ(shutdown_from_suspend_.PrepareForSuspendAttempt(),
            ShutdownFromSuspend::Action::SUSPEND);
}

TEST_F(ShutdownFromSuspendTest, TestBothTimersExpiredWhenBothSupported) {
  Init();
  shutdown_from_suspend_.PrepareForSuspendAttempt();
  // We haven't been running long enough to hibernate.
  runner_.StartLoop(base::Milliseconds(500));
  EXPECT_EQ(shutdown_from_suspend_.PrepareForSuspendAttempt(),
            ShutdownFromSuspend::Action::SUSPEND);

  // Run for another 1 second and both timers will have expired, but the
  // shutdown timer will take precedence over the hibernate timer in this
  // situation.
  shutdown_from_suspend_.HandleDarkResume();
  runner_.StartLoop(base::Seconds(2));
  EXPECT_EQ(shutdown_from_suspend_.PrepareForSuspendAttempt(),
            ShutdownFromSuspend::Action::SHUT_DOWN);
}

// This test will confirm that we do not hibernate when on line power, even if
// we're eligible to and the battery is low.
TEST_F(ShutdownFromSuspendTest,
       TestHibernateEnabledOnLinePowerDoesntHibernateWhenBattLow) {
  Init();
  shutdown_from_suspend_.PrepareForSuspendAttempt();
  // We haven't been running long enough to hibernate.
  runner_.StartLoop(base::Milliseconds(500));
  EXPECT_EQ(shutdown_from_suspend_.PrepareForSuspendAttempt(),
            ShutdownFromSuspend::Action::SUSPEND);

  SetPowerStatus(/* low_battery = */ true, /* line_power= */ true);
  shutdown_from_suspend_.HandleDarkResume();
  runner_.StartLoop(base::Milliseconds(600));
  EXPECT_EQ(shutdown_from_suspend_.PrepareForSuspendAttempt(),
            ShutdownFromSuspend::Action::SUSPEND);
}

}  // namespace power_manager::policy
