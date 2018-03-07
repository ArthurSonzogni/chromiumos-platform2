// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/input_event_handler.h"

#include <stdint.h>

#include <string>
#include <vector>

#include <base/format_macros.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest.h>

#include "power_manager/common/action_recorder.h"
#include "power_manager/common/clock.h"
#include "power_manager/common/fake_prefs.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/system/dbus_wrapper_stub.h"
#include "power_manager/powerd/system/display/display_info.h"
#include "power_manager/powerd/system/display/display_watcher_stub.h"
#include "power_manager/powerd/system/input_watcher_stub.h"
#include "power_manager/proto_bindings/input_event.pb.h"

namespace power_manager {
namespace policy {
namespace {

const char kNoActions[] = "";
const char kLidClosed[] = "lid_closed";
const char kLidOpened[] = "lid_opened";
const char kPowerButtonDown[] = "power_down";
const char kPowerButtonUp[] = "power_up";
const char kPowerButtonRepeat[] = "power_repeat";
const char kShutDown[] = "shut_down";
const char kMissingPowerButtonAcknowledgment[] = "missing_power_button_ack";
const char kHoverOn[] = "hover_on";
const char kHoverOff[] = "hover_off";
const char kTabletOn[] = "tablet_on";
const char kTabletOff[] = "tablet_off";
const char kTabletUnsupported[] = "tablet_unsupported";

const char* GetTabletModeAction(TabletMode mode) {
  switch (mode) {
    case TabletMode::ON:
      return kTabletOn;
    case TabletMode::OFF:
      return kTabletOff;
    case TabletMode::UNSUPPORTED:
      return kTabletUnsupported;
  }
  NOTREACHED() << "Invalid tablet mode " << mode;
  return "tablet_invalid";
}

const char* GetPowerButtonAction(ButtonState state) {
  switch (state) {
    case ButtonState::DOWN:
      return kPowerButtonDown;
    case ButtonState::UP:
      return kPowerButtonUp;
    case ButtonState::REPEAT:
      return kPowerButtonRepeat;
  }
  NOTREACHED() << "Invalid power button state " << state;
  return "power_invalid";
}

std::string GetAcknowledgmentDelayAction(base::TimeDelta delay) {
  return base::StringPrintf("power_button_ack_delay(%" PRId64 ")",
                            delay.InMilliseconds());
}

class TestInputEventHandlerDelegate : public InputEventHandler::Delegate,
                                      public ActionRecorder {
 public:
  TestInputEventHandlerDelegate() {}
  ~TestInputEventHandlerDelegate() override {}

  // InputEventHandler::Delegate implementation:
  void HandleLidClosed() override { AppendAction(kLidClosed); }
  void HandleLidOpened() override { AppendAction(kLidOpened); }
  void HandlePowerButtonEvent(ButtonState state) override {
    AppendAction(GetPowerButtonAction(state));
  }
  void HandleHoverStateChange(bool hovering) override {
    AppendAction(hovering ? kHoverOn : kHoverOff);
  }
  void HandleTabletModeChange(TabletMode mode) override {
    EXPECT_NE(TabletMode::UNSUPPORTED, mode);
    AppendAction(GetTabletModeAction(mode));
  }
  void ShutDownForPowerButtonWithNoDisplay() override {
    AppendAction(kShutDown);
  }
  void HandleMissingPowerButtonAcknowledgment() override {
    AppendAction(kMissingPowerButtonAcknowledgment);
  }
  void ReportPowerButtonAcknowledgmentDelay(base::TimeDelta delay) override {
    AppendAction(GetAcknowledgmentDelayAction(delay));
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(TestInputEventHandlerDelegate);
};

}  // namespace

class InputEventHandlerTest : public ::testing::Test {
 public:
  InputEventHandlerTest() {
    handler_.clock_for_testing()->set_current_time_for_testing(
        base::TimeTicks::FromInternalValue(1000));
  }
  ~InputEventHandlerTest() override {}

 protected:
  // Initializes |handler_|.
  void Init() {
    handler_.Init(&input_watcher_, &delegate_, &display_watcher_,
                  &dbus_wrapper_, &prefs_);
  }

  // Tests that one InputEvent D-Bus signal has been sent and returns the
  // signal's |type| field.
  int GetInputEventSignalType() {
    InputEvent proto;
    EXPECT_EQ(1, dbus_wrapper_.num_sent_signals());
    EXPECT_TRUE(
        dbus_wrapper_.GetSentSignal(0, kInputEventSignal, &proto, nullptr));
    return proto.type();
  }

  // Tests that one InputEvent D-Bus signal has been sent and returns the
  // signal's |timestamp| field.
  int64_t GetInputEventSignalTimestamp() {
    InputEvent proto;
    EXPECT_EQ(1, dbus_wrapper_.num_sent_signals());
    EXPECT_TRUE(
        dbus_wrapper_.GetSentSignal(0, kInputEventSignal, &proto, nullptr));
    return proto.timestamp();
  }

  // Returns the current (fake) time.
  base::TimeTicks Now() {
    return handler_.clock_for_testing()->GetCurrentTime();
  }

  // Advances the current time by |interval|.
  void AdvanceTime(const base::TimeDelta& interval) {
    handler_.clock_for_testing()->set_current_time_for_testing(Now() +
                                                               interval);
  }

  FakePrefs prefs_;
  system::InputWatcherStub input_watcher_;
  system::DisplayWatcherStub display_watcher_;
  system::DBusWrapperStub dbus_wrapper_;
  TestInputEventHandlerDelegate delegate_;
  InputEventHandler handler_;
};

TEST_F(InputEventHandlerTest, LidEvents) {
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  // Initialization shouldn't generate a synthetic event.
  prefs_.SetInt64(kUseLidPref, 1);
  Init();
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  EXPECT_EQ(0, dbus_wrapper_.num_sent_signals());
  dbus_wrapper_.ClearSentSignals();

  AdvanceTime(base::TimeDelta::FromSeconds(1));
  input_watcher_.set_lid_state(LidState::CLOSED);
  input_watcher_.NotifyObserversAboutLidState();
  EXPECT_EQ(kLidClosed, delegate_.GetActions());
  EXPECT_EQ(InputEvent_Type_LID_CLOSED, GetInputEventSignalType());
  EXPECT_EQ(Now().ToInternalValue(), GetInputEventSignalTimestamp());
  dbus_wrapper_.ClearSentSignals();

  AdvanceTime(base::TimeDelta::FromSeconds(5));
  input_watcher_.set_lid_state(LidState::OPEN);
  input_watcher_.NotifyObserversAboutLidState();
  EXPECT_EQ(kLidOpened, delegate_.GetActions());
  EXPECT_EQ(InputEvent_Type_LID_OPEN, GetInputEventSignalType());
  EXPECT_EQ(Now().ToInternalValue(), GetInputEventSignalTimestamp());
  dbus_wrapper_.ClearSentSignals();
}

TEST_F(InputEventHandlerTest, TabletModeEvents) {
  Init();
  EXPECT_EQ(0, dbus_wrapper_.num_sent_signals());
  dbus_wrapper_.ClearSentSignals();

  AdvanceTime(base::TimeDelta::FromSeconds(1));
  input_watcher_.set_tablet_mode(TabletMode::ON);
  input_watcher_.NotifyObserversAboutTabletMode();
  EXPECT_EQ(kTabletOn, delegate_.GetActions());
  EXPECT_EQ(InputEvent_Type_TABLET_MODE_ON, GetInputEventSignalType());
  EXPECT_EQ(Now().ToInternalValue(), GetInputEventSignalTimestamp());
  dbus_wrapper_.ClearSentSignals();

  AdvanceTime(base::TimeDelta::FromSeconds(1));
  input_watcher_.set_tablet_mode(TabletMode::OFF);
  input_watcher_.NotifyObserversAboutTabletMode();
  EXPECT_EQ(kTabletOff, delegate_.GetActions());
  EXPECT_EQ(InputEvent_Type_TABLET_MODE_OFF, GetInputEventSignalType());
  EXPECT_EQ(Now().ToInternalValue(), GetInputEventSignalTimestamp());
  dbus_wrapper_.ClearSentSignals();
}

TEST_F(InputEventHandlerTest, PowerButtonEvents) {
  prefs_.SetInt64(kExternalDisplayOnlyPref, 1);
  std::vector<system::DisplayInfo> displays(1, system::DisplayInfo());
  display_watcher_.set_displays(displays);
  Init();

  input_watcher_.NotifyObserversAboutPowerButtonEvent(ButtonState::DOWN);
  EXPECT_EQ(kPowerButtonDown, delegate_.GetActions());
  EXPECT_EQ(InputEvent_Type_POWER_BUTTON_DOWN, GetInputEventSignalType());
  EXPECT_EQ(Now().ToInternalValue(), GetInputEventSignalTimestamp());
  dbus_wrapper_.ClearSentSignals();

  AdvanceTime(base::TimeDelta::FromMilliseconds(100));
  input_watcher_.NotifyObserversAboutPowerButtonEvent(ButtonState::UP);
  EXPECT_EQ(kPowerButtonUp, delegate_.GetActions());
  EXPECT_EQ(InputEvent_Type_POWER_BUTTON_UP, GetInputEventSignalType());
  EXPECT_EQ(Now().ToInternalValue(), GetInputEventSignalTimestamp());
  dbus_wrapper_.ClearSentSignals();

  // With no displays connected, the system should shut down immediately.
  displays.clear();
  display_watcher_.set_displays(displays);
  input_watcher_.NotifyObserversAboutPowerButtonEvent(ButtonState::DOWN);
  EXPECT_EQ(kShutDown, delegate_.GetActions());
  EXPECT_EQ(0, dbus_wrapper_.num_sent_signals());
}

TEST_F(InputEventHandlerTest, IgnorePowerButtonPresses) {
  Init();
  dbus_wrapper_.ClearSentSignals();

  const base::TimeDelta kShortDelay = base::TimeDelta::FromMilliseconds(100);
  const base::TimeDelta kIgnoreTimeout = base::TimeDelta::FromSeconds(3);

  // Ignore the power button events.
  handler_.IgnoreNextPowerButtonPress(kIgnoreTimeout);
  input_watcher_.NotifyObserversAboutPowerButtonEvent(ButtonState::DOWN);
  EXPECT_TRUE(delegate_.GetActions().empty());
  EXPECT_EQ(0, dbus_wrapper_.num_sent_signals());

  // Release the power button.
  AdvanceTime(kShortDelay);
  input_watcher_.NotifyObserversAboutPowerButtonEvent(ButtonState::UP);
  EXPECT_TRUE(delegate_.GetActions().empty());
  EXPECT_EQ(0, dbus_wrapper_.num_sent_signals());

  // Next press is going through.
  AdvanceTime(kShortDelay);
  input_watcher_.NotifyObserversAboutPowerButtonEvent(ButtonState::DOWN);
  EXPECT_EQ(kPowerButtonDown, delegate_.GetActions());
  EXPECT_EQ(InputEvent_Type_POWER_BUTTON_DOWN, GetInputEventSignalType());
  dbus_wrapper_.ClearSentSignals();
  AdvanceTime(kShortDelay);
  input_watcher_.NotifyObserversAboutPowerButtonEvent(ButtonState::UP);
  EXPECT_EQ(kPowerButtonUp, delegate_.GetActions());
  EXPECT_EQ(InputEvent_Type_POWER_BUTTON_UP, GetInputEventSignalType());
  dbus_wrapper_.ClearSentSignals();

  // Ignore again the power button events.
  handler_.IgnoreNextPowerButtonPress(kIgnoreTimeout);
  // Expire the timeout.
  AdvanceTime(kIgnoreTimeout + base::TimeDelta::FromMilliseconds(500));
  // The next press is going through.
  input_watcher_.NotifyObserversAboutPowerButtonEvent(ButtonState::DOWN);
  EXPECT_EQ(kPowerButtonDown, delegate_.GetActions());
  AdvanceTime(kShortDelay);
  input_watcher_.NotifyObserversAboutPowerButtonEvent(ButtonState::UP);
  EXPECT_EQ(kPowerButtonUp, delegate_.GetActions());

  // Ignore again the power button events.
  handler_.IgnoreNextPowerButtonPress(kIgnoreTimeout);
  // Cancel the timeout.
  handler_.IgnoreNextPowerButtonPress(base::TimeDelta());
  // The next press is going through.
  input_watcher_.NotifyObserversAboutPowerButtonEvent(ButtonState::DOWN);
  EXPECT_EQ(kPowerButtonDown, delegate_.GetActions());
  AdvanceTime(kShortDelay);
  input_watcher_.NotifyObserversAboutPowerButtonEvent(ButtonState::UP);
  EXPECT_EQ(kPowerButtonUp, delegate_.GetActions());

  // Race condition between the user and the U2F code, the down event happens
  // before the ignore event.
  input_watcher_.NotifyObserversAboutPowerButtonEvent(ButtonState::DOWN);
  EXPECT_EQ(kPowerButtonDown, delegate_.GetActions());
  AdvanceTime(kShortDelay);
  // Then the daemon receives the request to ignore the physical presence on
  // the power button.
  handler_.IgnoreNextPowerButtonPress(kIgnoreTimeout);
  // The user release the button but the release needs to go through else we
  // have a press without a release (which becomes a long press).
  input_watcher_.NotifyObserversAboutPowerButtonEvent(ButtonState::UP);
  EXPECT_EQ(kPowerButtonUp, delegate_.GetActions());
}

TEST_F(InputEventHandlerTest, AcknowledgePowerButtonPresses) {
  Init();

  const base::TimeDelta kShortDelay = base::TimeDelta::FromMilliseconds(100);
  const base::TimeDelta kTimeout = base::TimeDelta::FromMilliseconds(
      InputEventHandler::kPowerButtonAcknowledgmentTimeoutMs);

  // Press the power button, acknowledge the event nearly immediately, and check
  // that no further actions are performed and that the timeout is stopped.
  input_watcher_.NotifyObserversAboutPowerButtonEvent(ButtonState::DOWN);
  EXPECT_EQ(kPowerButtonDown, delegate_.GetActions());
  AdvanceTime(kShortDelay);
  handler_.HandlePowerButtonAcknowledgment(
      base::TimeTicks::FromInternalValue(GetInputEventSignalTimestamp()));
  EXPECT_EQ(GetAcknowledgmentDelayAction(kShortDelay), delegate_.GetActions());
  ASSERT_FALSE(handler_.TriggerPowerButtonAcknowledgmentTimeoutForTesting());
  input_watcher_.NotifyObserversAboutPowerButtonEvent(ButtonState::UP);
  EXPECT_EQ(kPowerButtonUp, delegate_.GetActions());

  // Check that releasing the power button before it's been acknowledged also
  // stops the timeout.
  AdvanceTime(base::TimeDelta::FromSeconds(1));
  input_watcher_.NotifyObserversAboutPowerButtonEvent(ButtonState::DOWN);
  EXPECT_EQ(kPowerButtonDown, delegate_.GetActions());
  input_watcher_.NotifyObserversAboutPowerButtonEvent(ButtonState::UP);
  EXPECT_EQ(kPowerButtonUp, delegate_.GetActions());
  ASSERT_FALSE(handler_.TriggerPowerButtonAcknowledgmentTimeoutForTesting());
  dbus_wrapper_.ClearSentSignals();

  // Let the timeout fire and check that the delegate is notified.
  AdvanceTime(base::TimeDelta::FromSeconds(1));
  input_watcher_.NotifyObserversAboutPowerButtonEvent(ButtonState::DOWN);
  EXPECT_EQ(kPowerButtonDown, delegate_.GetActions());
  ASSERT_TRUE(handler_.TriggerPowerButtonAcknowledgmentTimeoutForTesting());
  EXPECT_EQ(JoinActions(GetAcknowledgmentDelayAction(kTimeout).c_str(),
                        kMissingPowerButtonAcknowledgment, NULL),
            delegate_.GetActions());
  ASSERT_FALSE(handler_.TriggerPowerButtonAcknowledgmentTimeoutForTesting());
  input_watcher_.NotifyObserversAboutPowerButtonEvent(ButtonState::UP);
  EXPECT_EQ(kPowerButtonUp, delegate_.GetActions());

  // Send an acknowledgment with a stale timestamp and check that it doesn't
  // stop the timeout.
  AdvanceTime(base::TimeDelta::FromSeconds(1));
  dbus_wrapper_.ClearSentSignals();
  input_watcher_.NotifyObserversAboutPowerButtonEvent(ButtonState::DOWN);
  EXPECT_EQ(kPowerButtonDown, delegate_.GetActions());
  handler_.HandlePowerButtonAcknowledgment(
      base::TimeTicks::FromInternalValue(GetInputEventSignalTimestamp() - 100));
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ASSERT_TRUE(handler_.TriggerPowerButtonAcknowledgmentTimeoutForTesting());
  EXPECT_EQ(JoinActions(GetAcknowledgmentDelayAction(kTimeout).c_str(),
                        kMissingPowerButtonAcknowledgment, NULL),
            delegate_.GetActions());
  ASSERT_FALSE(handler_.TriggerPowerButtonAcknowledgmentTimeoutForTesting());
  input_watcher_.NotifyObserversAboutPowerButtonEvent(ButtonState::UP);
  EXPECT_EQ(kPowerButtonUp, delegate_.GetActions());
}

TEST_F(InputEventHandlerTest, FactoryMode) {
  prefs_.SetInt64(kFactoryModePref, 1);
  Init();

  // Power button events shouldn't be reported to the delegate or announced to
  // Chrome over D-Bus when in factory mode.
  input_watcher_.NotifyObserversAboutPowerButtonEvent(ButtonState::DOWN);
  input_watcher_.NotifyObserversAboutPowerButtonEvent(ButtonState::UP);
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  EXPECT_EQ(0, dbus_wrapper_.num_sent_signals());

  // Tablet mode and lid events should still be reported, though.
  input_watcher_.set_tablet_mode(TabletMode::ON);
  input_watcher_.NotifyObserversAboutTabletMode();
  EXPECT_EQ(kTabletOn, delegate_.GetActions());
  EXPECT_EQ(InputEvent_Type_TABLET_MODE_ON, GetInputEventSignalType());
  dbus_wrapper_.ClearSentSignals();

  input_watcher_.set_lid_state(LidState::CLOSED);
  input_watcher_.NotifyObserversAboutLidState();
  EXPECT_EQ(kLidClosed, delegate_.GetActions());
  EXPECT_EQ(InputEvent_Type_LID_CLOSED, GetInputEventSignalType());
  dbus_wrapper_.ClearSentSignals();
}

TEST_F(InputEventHandlerTest, OnHoverStateChangeTest) {
  Init();
  input_watcher_.NotifyObserversAboutHoverState(true);
  EXPECT_EQ(kHoverOn, delegate_.GetActions());
  input_watcher_.NotifyObserversAboutHoverState(false);
  EXPECT_EQ(kHoverOff, delegate_.GetActions());
}

}  // namespace policy
}  // namespace power_manager
