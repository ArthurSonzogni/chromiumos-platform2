// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/external_backlight_controller.h"

#include <linux/i2c-dev.h>
#include <linux/i2c.h>

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/compiler_specific.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest.h>

#include "power_manager/common/fake_prefs.h"
#include "power_manager/powerd/policy/backlight_controller_observer_stub.h"
#include "power_manager/powerd/policy/backlight_controller_test_util.h"
#include "power_manager/powerd/system/ambient_light_sensor_watcher_stub.h"
#include "power_manager/powerd/system/dbus_wrapper_stub.h"
#include "power_manager/powerd/system/display/display_power_setter_stub.h"
#include "power_manager/powerd/system/display/display_watcher_stub.h"
#include "power_manager/powerd/system/display/external_display.h"
#include "power_manager/powerd/system/external_ambient_light_sensor_factory_stub.h"
#include "power_manager/powerd/testing/test_environment.h"
#include "power_manager/proto_bindings/backlight.pb.h"

namespace {

constexpr char kFirstDisplay[] = "/sys/devices/usb1/mydisplay";
constexpr char kFirstSensor[] = "/sys/devices/usb1/mysensor";
constexpr char kSecondDisplay[] = "/sys/devices/usb2/mydisplay";
constexpr char kSecondSensor[] = "/sys/devices/usb2/mysensor";

// Fake ExternalDisplay::Delegate that replies with a fixed initial brightness
// and supports injecting DDC/CI read, reply, and write failures.
class FakeExternalDisplayDelegate
    : public power_manager::system::ExternalDisplay::Delegate {
 public:
  FakeExternalDisplayDelegate(uint16_t current_brightness,
                              uint16_t max_brightness)
      : current_brightness_(current_brightness),
        max_brightness_(max_brightness) {}
  FakeExternalDisplayDelegate(const FakeExternalDisplayDelegate&) = delete;
  FakeExternalDisplayDelegate& operator=(const FakeExternalDisplayDelegate&) =
      delete;
  ~FakeExternalDisplayDelegate() override = default;

  void set_fail_get_writes(bool fail) { fail_get_writes_ = fail; }
  void set_fail_set_writes(bool fail) { fail_set_writes_ = fail; }
  void set_fail_reads(bool fail) { fail_reads_ = fail; }
  void set_reply_result_code(uint8_t code) { reply_result_code_ = code; }

  std::string GetName() const override { return "i2c-fake"; }

  bool PerformI2COperation(struct i2c_rdwr_ioctl_data* data) override {
    using power_manager::system::ExternalDisplay;
    CHECK(data);
    CHECK_EQ(data->nmsgs, 1u);
    struct i2c_msg* const msg = data->msgs;
    CHECK(msg);
    if (msg->flags == 0) {
      if (msg->len >= 3 && msg->buf[2] == ExternalDisplay::kDdcGetCommand) {
        return !fail_get_writes_;
      }
      return !fail_set_writes_;
    }
    if (msg->flags == I2C_M_RD) {
      if (fail_reads_) {
        return false;
      }
      std::vector<uint8_t> reply = {
          ExternalDisplay::kDdcDisplayAddress,
          static_cast<uint8_t>(ExternalDisplay::kDdcMessageBodyLengthMask | 8),
          ExternalDisplay::kDdcGetReplyCommand,
          reply_result_code_,
          ExternalDisplay::kDdcBrightnessIndex,
          0x0,
          static_cast<uint8_t>(max_brightness_ >> 8),
          static_cast<uint8_t>(max_brightness_ & 0xff),
          static_cast<uint8_t>(current_brightness_ >> 8),
          static_cast<uint8_t>(current_brightness_ & 0xff),
          0x0};
      uint8_t checksum = ExternalDisplay::kDdcVirtualHostAddress;
      for (size_t i = 0; i < reply.size() - 1; ++i) {
        checksum ^= reply[i];
      }
      reply.back() = checksum;
      CHECK_EQ(static_cast<size_t>(msg->len), reply.size());
      memcpy(msg->buf, reply.data(), reply.size());
      return true;
    }
    return false;
  }

 private:
  const uint16_t current_brightness_;
  const uint16_t max_brightness_;
  bool fail_get_writes_ = false;
  bool fail_set_writes_ = false;
  bool fail_reads_ = false;
  uint8_t reply_result_code_ = 0x0;
};

}  // namespace

namespace power_manager::policy {

class ExternalBacklightControllerTest : public TestEnvironment {
 public:
  ExternalBacklightControllerTest() {
    prefs_.SetString(kExternalBacklightAlsStepsPref, default_als_steps_);
    controller_.AddObserver(&observer_);
    controller_.Init(&prefs_, &ambient_light_sensor_watcher_,
                     &ambient_light_sensor_factory_, &display_watcher_,
                     &display_power_setter_, &dbus_wrapper_);
  }

  ~ExternalBacklightControllerTest() override {
    controller_.RemoveObserver(&observer_);
  }

 protected:
  void SetControllerTime(base::TimeTicks now) {
    controller_.clock_.set_current_time_for_testing(now);
  }

  void AddDisplay(const std::string& syspath) {
    system::DisplayInfo display = {
        .drm_path = base::FilePath(),
        .i2c_path = base::FilePath("/dev/i2c-1"),
        .sys_path = base::FilePath(syspath),
        .connector_status = system::DisplayInfo::ConnectorStatus::CONNECTED,
    };
    display_watcher_.AddDisplay(display);
  }
  void RemoveDisplay(const std::string& syspath) {
    system::DisplayInfo display = {
        .drm_path = base::FilePath(),
        .i2c_path = base::FilePath("/dev/i2c-1"),
        .sys_path = base::FilePath(syspath),
        .connector_status = system::DisplayInfo::ConnectorStatus::CONNECTED,
    };
    display_watcher_.RemoveDisplay(display);
  }
  void AddSensor(const std::string& syspath) {
    system::AmbientLightSensorInfo als = {
        .iio_path = base::FilePath(syspath),
        .device = std::string(),
    };
    ambient_light_sensor_watcher_.AddSensor(als);
  }
  void RemoveSensor(const std::string& syspath) {
    system::AmbientLightSensorInfo als = {
        .iio_path = base::FilePath(syspath),
        .device = std::string(),
    };
    ambient_light_sensor_watcher_.RemoveSensor(als);
  }

  void CallSetExternalDisplayALSBrightness(bool enabled) {
    dbus::MethodCall method_call(kPowerManagerInterface,
                                 kSetExternalDisplayALSBrightnessMethod);
    dbus::MessageWriter writer(&method_call);
    writer.AppendBool(enabled);
    ASSERT_TRUE(dbus_wrapper_.CallExportedMethodSync(&method_call));
  }

  void CallGetExternalDisplayALSBrightness(bool* enabled) {
    dbus::MethodCall method_call(kPowerManagerInterface,
                                 kGetExternalDisplayALSBrightnessMethod);
    dbus::MessageWriter writer(&method_call);
    std::unique_ptr<dbus::Response> response =
        dbus_wrapper_.CallExportedMethodSync(&method_call);
    ASSERT_TRUE(response.get());
    ASSERT_TRUE(dbus::MessageReader(response.get()).PopBool(enabled));
  }

  FakeExternalDisplayDelegate* AttachFakeDelegate(
      ExternalBacklightController* controller,
      uint16_t current_brightness,
      uint16_t max_brightness) {
    CHECK_EQ(1u, controller->external_displays_.size());
    return AttachFakeDelegate(controller,
                              controller->external_displays_.begin()->first,
                              current_brightness, max_brightness);
  }

  FakeExternalDisplayDelegate* AttachFakeDelegate(
      ExternalBacklightController* controller,
      const system::DisplayInfo& display_info,
      uint16_t current_brightness,
      uint16_t max_brightness) {
    auto it = controller->external_displays_.find(display_info);
    CHECK(it != controller->external_displays_.end());
    auto delegate = std::make_unique<FakeExternalDisplayDelegate>(
        current_brightness, max_brightness);
    FakeExternalDisplayDelegate* delegate_ptr = delegate.get();
    system::ExternalDisplay::TestApi test_api(it->second.get());
    test_api.SetDelegate(std::move(delegate));
    return delegate_ptr;
  }

  bool TriggerDisplayTimeout(ExternalBacklightController* controller) {
    if (controller->external_displays_.empty()) {
      return false;
    }
    return TriggerDisplayTimeout(controller,
                                 controller->external_displays_.begin()->first);
  }

  bool TriggerDisplayTimeout(ExternalBacklightController* controller,
                             const system::DisplayInfo& display_info) {
    auto it = controller->external_displays_.find(display_info);
    if (it == controller->external_displays_.end()) {
      return false;
    }
    system::ExternalDisplay::TestApi test_api(it->second.get());
    return test_api.TriggerTimeout();
  }

  [[nodiscard]] bool PopSingleSentSignal(const std::string& signal_name) {
    if (dbus_wrapper_.num_sent_signals() != 1u ||
        !dbus_wrapper_.GetSentSignal(0, signal_name, nullptr, nullptr)) {
      return false;
    }
    dbus_wrapper_.ClearSentSignals();
    return true;
  }

  std::string default_als_steps_ = "5.0 -1 600\n100.0 500 -1";

  FakePrefs prefs_;
  BacklightControllerObserverStub observer_;
  system::AmbientLightSensorWatcherStub ambient_light_sensor_watcher_;
  system::ExternalAmbientLightSensorFactoryStub ambient_light_sensor_factory_;
  system::DisplayWatcherStub display_watcher_;
  system::DisplayPowerSetterStub display_power_setter_;
  system::DBusWrapperStub dbus_wrapper_;
  ExternalBacklightController controller_;
};

TEST_F(ExternalBacklightControllerTest, BrightnessRequests) {
  // ExternalBacklightController doesn't support absolute-brightness-related
  // requests, but it does allow relative adjustments.
  double percent = 0.0;
  EXPECT_FALSE(controller_.GetBrightnessPercent(&percent));
  test::CallSetScreenBrightness(
      &dbus_wrapper_, 50.0, SetBacklightBrightnessRequest_Transition_INSTANT,
      SetBacklightBrightnessRequest_Cause_USER_REQUEST);
  EXPECT_EQ(0, controller_.GetNumUserAdjustments());
  test::CallIncreaseScreenBrightness(&dbus_wrapper_);
  EXPECT_EQ(1, controller_.GetNumUserAdjustments());
  test::CallDecreaseScreenBrightness(&dbus_wrapper_, true /* allow_off */);
  EXPECT_EQ(2, controller_.GetNumUserAdjustments());
  test::CallIncreaseExternalScreenBrightness(&dbus_wrapper_);
  EXPECT_EQ(3, controller_.GetNumUserAdjustments());
  test::CallDecreaseExternalScreenBrightness(&dbus_wrapper_);
  EXPECT_EQ(4, controller_.GetNumUserAdjustments());
  ASSERT_EQ(4u, dbus_wrapper_.num_sent_signals());
  for (size_t i = 0; i < 4u; ++i) {
    EXPECT_TRUE(dbus_wrapper_.GetSentSignal(
        i, kExternalScreenBrightnessFailedSignal, nullptr, nullptr));
  }
  dbus_wrapper_.ClearSentSignals();

  controller_.HandleSessionStateChange(SessionState::STARTED);
  EXPECT_EQ(0, controller_.GetNumUserAdjustments());

  // If ALS-based brightness is disabled, the brightness of external displays
  // with an ambient light sensor can be adjusted with an absolute request or a
  // relative request, and emits ExternalScreenBrightnessChanged with the
  // user/request cause rather than AMBIENT_LIGHT_CHANGED.
  AddDisplay(kFirstDisplay);
  AttachFakeDelegate(&controller_, 60, 100);
  AddSensor(kFirstSensor);
  bool enabled = false;
  CallGetExternalDisplayALSBrightness(&enabled);
  EXPECT_TRUE(enabled);
  CallSetExternalDisplayALSBrightness(false);
  CallGetExternalDisplayALSBrightness(&enabled);
  EXPECT_FALSE(enabled);
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_));
  ASSERT_EQ(1u, dbus_wrapper_.num_sent_signals());
  test::CheckBrightnessChangedSignal(
      &dbus_wrapper_, 0, kExternalScreenBrightnessChangedSignal, 100.0,
      BacklightBrightnessChange_Cause_USER_REQUEST_FROM_SETTINGS_APP);

  ASSERT_TRUE(TriggerDisplayTimeout(&controller_));
  test::CallSetScreenBrightness(
      &dbus_wrapper_, 42.0, SetBacklightBrightnessRequest_Transition_INSTANT,
      SetBacklightBrightnessRequest_Cause_USER_REQUEST_FROM_SETTINGS_APP);
  EXPECT_EQ(1, controller_.GetNumUserAdjustments());
  EXPECT_TRUE(controller_.GetBrightnessPercent(&percent));
  EXPECT_EQ(42.0, percent);
  ASSERT_EQ(2u, dbus_wrapper_.num_sent_signals());
  test::CheckBrightnessChangedSignal(
      &dbus_wrapper_, 1, kExternalScreenBrightnessChangedSignal, 42.0,
      BacklightBrightnessChange_Cause_USER_REQUEST_FROM_SETTINGS_APP);

  ASSERT_TRUE(TriggerDisplayTimeout(&controller_));
  test::CallIncreaseScreenBrightness(&dbus_wrapper_);
  EXPECT_TRUE(controller_.GetBrightnessPercent(&percent));
  EXPECT_EQ(47.0, percent);
  EXPECT_EQ(2, controller_.GetNumUserAdjustments());
  ASSERT_EQ(3u, dbus_wrapper_.num_sent_signals());
  test::CheckBrightnessChangedSignal(
      &dbus_wrapper_, 2, kExternalScreenBrightnessChangedSignal, 47.0,
      BacklightBrightnessChange_Cause_USER_REQUEST);

  ASSERT_TRUE(TriggerDisplayTimeout(&controller_));
  test::CallDecreaseScreenBrightness(&dbus_wrapper_, true /* allow_off */);
  EXPECT_TRUE(controller_.GetBrightnessPercent(&percent));
  EXPECT_EQ(42.0, percent);
  EXPECT_EQ(3, controller_.GetNumUserAdjustments());
  ASSERT_EQ(4u, dbus_wrapper_.num_sent_signals());
  test::CheckBrightnessChangedSignal(
      &dbus_wrapper_, 3, kExternalScreenBrightnessChangedSignal, 42.0,
      BacklightBrightnessChange_Cause_USER_REQUEST);
}

TEST_F(ExternalBacklightControllerTest, SecondaryControllerMode) {
  // Initialize a secondary ExternalBacklightController (as on Chromebooks with
  // an internal panel) where ambient_light_sensor_watcher,
  // external_ambient_light_sensor_factory, and display_power_setter are null.
  system::DBusWrapperStub secondary_dbus_wrapper;
  BacklightControllerObserverStub secondary_observer;
  ExternalBacklightController secondary_controller;
  secondary_controller.AddObserver(&secondary_observer);
  secondary_controller.Init(
      &prefs_, /*ambient_light_sensor_watcher=*/nullptr,
      /*external_ambient_light_sensor_factory=*/nullptr, &display_watcher_,
      /*display_power_setter=*/nullptr, &secondary_dbus_wrapper);

  // Primary screen brightness methods must not be exported when acting as a
  // secondary controller so they do not collide with
  // InternalBacklightController.
  EXPECT_FALSE(
      secondary_dbus_wrapper.IsMethodExported(kSetScreenBrightnessMethod));
  EXPECT_FALSE(
      secondary_dbus_wrapper.IsMethodExported(kIncreaseScreenBrightnessMethod));
  EXPECT_FALSE(
      secondary_dbus_wrapper.IsMethodExported(kDecreaseScreenBrightnessMethod));
  EXPECT_FALSE(secondary_dbus_wrapper.IsMethodExported(
      kGetScreenBrightnessPercentMethod));
  EXPECT_FALSE(secondary_dbus_wrapper.IsMethodExported(
      kSetExternalDisplayALSBrightnessMethod));
  EXPECT_FALSE(secondary_dbus_wrapper.IsMethodExported(
      kGetExternalDisplayALSBrightnessMethod));

  // Dedicated external display brightness methods must be exported and adjust
  // external monitors, emitting ExternalScreenBrightnessChanged with
  // USER_REQUEST via the callback bound in UpdateDisplays().
  AddDisplay(kFirstDisplay);
  AttachFakeDelegate(&secondary_controller, 60, 100);
  EXPECT_TRUE(secondary_dbus_wrapper.IsMethodExported(
      kIncreaseExternalScreenBrightnessMethod));
  EXPECT_TRUE(secondary_dbus_wrapper.IsMethodExported(
      kDecreaseExternalScreenBrightnessMethod));
  test::CallIncreaseExternalScreenBrightness(&secondary_dbus_wrapper);
  EXPECT_EQ(1, secondary_controller.GetNumUserAdjustments());
  ASSERT_TRUE(TriggerDisplayTimeout(&secondary_controller));
  ASSERT_EQ(1u, secondary_dbus_wrapper.num_sent_signals());
  test::CheckBrightnessChangedSignal(
      &secondary_dbus_wrapper, 0, kExternalScreenBrightnessChangedSignal, 65.0,
      BacklightBrightnessChange_Cause_USER_REQUEST);

  ASSERT_TRUE(TriggerDisplayTimeout(&secondary_controller));
  test::CallDecreaseExternalScreenBrightness(&secondary_dbus_wrapper);
  EXPECT_EQ(2, secondary_controller.GetNumUserAdjustments());
  ASSERT_EQ(2u, secondary_dbus_wrapper.num_sent_signals());
  test::CheckBrightnessChangedSignal(
      &secondary_dbus_wrapper, 1, kExternalScreenBrightnessChangedSignal, 60.0,
      BacklightBrightnessChange_Cause_USER_REQUEST);

  // Ambient-light brightness updates must emit ExternalScreenBrightnessChanged
  // with AMBIENT_LIGHT_CHANGED rather than USER_REQUEST.
  ASSERT_TRUE(TriggerDisplayTimeout(&secondary_controller));
  secondary_controller.SetBrightnessPercentForAmbientLight(
      display_watcher_.GetDisplays()[0], 40.0);
  ASSERT_EQ(3u, secondary_dbus_wrapper.num_sent_signals());
  test::CheckBrightnessChangedSignal(
      &secondary_dbus_wrapper, 2, kExternalScreenBrightnessChangedSignal, 40.0,
      BacklightBrightnessChange_Cause_AMBIENT_LIGHT_CHANGED);

  // Lifecycle and power state callbacks must safely no-op without dereferencing
  // a null display_power_setter_ or emitting duplicate observer notifications.
  secondary_controller.HandleDisplayServiceStart();
  secondary_controller.SetDimmedForInactivity(true);
  secondary_controller.SetOffForInactivity(true);
  secondary_controller.SetSuspended(true);
  secondary_controller.SetShuttingDown(true);
  secondary_controller.SetForcedOff(true);
  EXPECT_TRUE(secondary_controller.GetForcedOff());
  secondary_controller.SetForcedOff(false);
  secondary_controller.SetShuttingDown(false);
  secondary_controller.SetSuspended(false);
  secondary_controller.SetOffForInactivity(false);
  secondary_controller.SetDimmedForInactivity(false);
  EXPECT_TRUE(secondary_observer.changes().empty());

  secondary_controller.RemoveObserver(&secondary_observer);
}

TEST_F(ExternalBacklightControllerTest, IgnoreInternalDisplayConnectors) {
  // Internal panel DRM connectors (eDP, LVDS, DSI) must be ignored even if
  // they expose an I2C aux channel, while external DP/HDMI connectors are
  // tracked in external_displays_.
  const std::vector<system::DisplayInfo> displays = {
      {
          .drm_path = base::FilePath("/sys/class/drm/card0-eDP-1"),
          .i2c_path = base::FilePath("/dev/i2c-1"),
          .sys_path = base::FilePath(kFirstDisplay),
          .connector_status = system::DisplayInfo::ConnectorStatus::CONNECTED,
      },
      {
          .drm_path = base::FilePath("/sys/class/drm/card0-LVDS-1"),
          .i2c_path = base::FilePath("/dev/i2c-2"),
          .sys_path = base::FilePath(kFirstDisplay),
          .connector_status = system::DisplayInfo::ConnectorStatus::CONNECTED,
      },
      {
          .drm_path = base::FilePath("/sys/class/drm/card0-DSI-1"),
          .i2c_path = base::FilePath("/dev/i2c-3"),
          .sys_path = base::FilePath(kFirstDisplay),
          .connector_status = system::DisplayInfo::ConnectorStatus::CONNECTED,
      },
      {
          .drm_path = base::FilePath("/sys/class/drm/card0-DP-1"),
          .i2c_path = base::FilePath("/dev/i2c-4"),
          .sys_path = base::FilePath(kSecondDisplay),
          .connector_status = system::DisplayInfo::ConnectorStatus::CONNECTED,
      },
  };
  for (const auto& display : displays) {
    display_watcher_.AddDisplay(display);
  }
  AddSensor(kFirstSensor);
  AddSensor(kSecondSensor);

  // Only kSecondDisplay (card0-DP-1) should be present in external_displays_
  // and matched with its ambient light sensor.
  std::vector<std::pair<base::FilePath, system::DisplayInfo>> matches =
      controller_.GetAmbientLightSensorAndDisplayMatchesForTesting();
  ASSERT_EQ(1, matches.size());
  EXPECT_STREQ(kSecondSensor, matches[0].first.value().c_str());
  EXPECT_STREQ(kSecondDisplay, matches[0].second.sys_path.value().c_str());
}

TEST_F(ExternalBacklightControllerTest, DimAndTurnOffScreen) {
  EXPECT_FALSE(display_power_setter_.dimmed());
  EXPECT_EQ(chromeos::DISPLAY_POWER_ALL_ON, display_power_setter_.state());

  observer_.Clear();
  controller_.SetDimmedForInactivity(true);
  EXPECT_TRUE(display_power_setter_.dimmed());
  EXPECT_EQ(chromeos::DISPLAY_POWER_ALL_ON, display_power_setter_.state());
  EXPECT_EQ(0, static_cast<int>(observer_.changes().size()));

  observer_.Clear();
  controller_.SetOffForInactivity(true);
  EXPECT_TRUE(display_power_setter_.dimmed());
  EXPECT_EQ(chromeos::DISPLAY_POWER_ALL_OFF, display_power_setter_.state());
  ASSERT_EQ(1, static_cast<int>(observer_.changes().size()));
  EXPECT_DOUBLE_EQ(0.0, observer_.changes()[0].percent);
  EXPECT_EQ(BacklightBrightnessChange_Cause_USER_INACTIVITY,
            observer_.changes()[0].cause);
  EXPECT_EQ(&controller_, observer_.changes()[0].source);

  observer_.Clear();
  controller_.SetSuspended(true);
  EXPECT_TRUE(display_power_setter_.dimmed());
  EXPECT_EQ(chromeos::DISPLAY_POWER_ALL_OFF, display_power_setter_.state());
  EXPECT_EQ(0, static_cast<int>(observer_.changes().size()));

  observer_.Clear();
  controller_.SetSuspended(false);
  controller_.SetOffForInactivity(false);
  controller_.SetDimmedForInactivity(false);
  EXPECT_FALSE(display_power_setter_.dimmed());
  EXPECT_EQ(chromeos::DISPLAY_POWER_ALL_ON, display_power_setter_.state());
  ASSERT_EQ(1, static_cast<int>(observer_.changes().size()));
  EXPECT_DOUBLE_EQ(100.0, observer_.changes()[0].percent);
  EXPECT_EQ(BacklightBrightnessChange_Cause_USER_ACTIVITY,
            observer_.changes()[0].cause);
  EXPECT_EQ(&controller_, observer_.changes()[0].source);
}

TEST_F(ExternalBacklightControllerTest, TurnDisplaysOffWhenShuttingDown) {
  controller_.SetShuttingDown(true);
  EXPECT_EQ(chromeos::DISPLAY_POWER_ALL_OFF, display_power_setter_.state());
  EXPECT_EQ(0, display_power_setter_.delay().InMilliseconds());
}

TEST_F(ExternalBacklightControllerTest, SetPowerOnDisplayServiceStart) {
  // The display power shouldn't be set by Init() (maybe Chrome hasn't started
  // yet).
  EXPECT_EQ(0, display_power_setter_.num_power_calls());
  EXPECT_EQ(0, static_cast<int>(observer_.changes().size()));

  // After Chrome starts, the state should be initialized to sane defaults.
  display_power_setter_.reset_num_power_calls();
  controller_.HandleDisplayServiceStart();
  EXPECT_EQ(1, display_power_setter_.num_power_calls());
  EXPECT_FALSE(display_power_setter_.dimmed());
  ASSERT_EQ(chromeos::DISPLAY_POWER_ALL_ON, display_power_setter_.state());
  ASSERT_EQ(1, static_cast<int>(observer_.changes().size()));
  EXPECT_DOUBLE_EQ(100.0, observer_.changes()[0].percent);
  EXPECT_EQ(BacklightBrightnessChange_Cause_OTHER,
            observer_.changes()[0].cause);
  EXPECT_EQ(&controller_, observer_.changes()[0].source);

  controller_.SetDimmedForInactivity(true);
  ASSERT_TRUE(display_power_setter_.dimmed());
  controller_.SetOffForInactivity(true);
  ASSERT_EQ(chromeos::DISPLAY_POWER_ALL_OFF, display_power_setter_.state());

  // Reset the power setter's dimming state so we can check that another dimming
  // request is sent when Chrome restarts.
  display_power_setter_.reset_num_power_calls();
  display_power_setter_.SetDisplaySoftwareDimming(false);
  observer_.Clear();
  controller_.HandleDisplayServiceStart();
  EXPECT_EQ(chromeos::DISPLAY_POWER_ALL_OFF, display_power_setter_.state());
  EXPECT_EQ(1, display_power_setter_.num_power_calls());
  EXPECT_TRUE(display_power_setter_.dimmed());
  ASSERT_EQ(1, static_cast<int>(observer_.changes().size()));
  EXPECT_DOUBLE_EQ(0.0, observer_.changes()[0].percent);
  EXPECT_EQ(BacklightBrightnessChange_Cause_OTHER,
            observer_.changes()[0].cause);
  EXPECT_EQ(&controller_, observer_.changes()[0].source);
}

TEST_F(ExternalBacklightControllerTest, ForcedOff) {
  controller_.SetForcedOff(true);
  EXPECT_EQ(chromeos::DISPLAY_POWER_ALL_OFF, display_power_setter_.state());
  EXPECT_EQ(0, display_power_setter_.delay().InMilliseconds());

  controller_.SetForcedOff(false);
  EXPECT_EQ(chromeos::DISPLAY_POWER_ALL_ON, display_power_setter_.state());
  EXPECT_EQ(0, display_power_setter_.delay().InMilliseconds());
}

TEST_F(ExternalBacklightControllerTest,
       MatchTwoDisplaysWithAmbientLightSensors) {
  AddDisplay(kFirstDisplay);

  // Connecting just the first display should not generate any matches.
  EXPECT_EQ(
      0, controller_.GetAmbientLightSensorAndDisplayMatchesForTesting().size());

  AddSensor(kFirstSensor);

  // After adding the first sensor, one match should be generated.
  std::vector<std::pair<base::FilePath, system::DisplayInfo>> matches =
      controller_.GetAmbientLightSensorAndDisplayMatchesForTesting();
  ASSERT_EQ(1, matches.size());
  EXPECT_STREQ(kFirstSensor, matches[0].first.value().c_str());
  EXPECT_STREQ(kFirstDisplay, matches[0].second.sys_path.value().c_str());

  AddSensor(kSecondSensor);

  // Adding a second sensor without the second display should still result in
  // one match.
  matches = controller_.GetAmbientLightSensorAndDisplayMatchesForTesting();
  ASSERT_EQ(1, matches.size());
  EXPECT_STREQ(kFirstSensor, matches[0].first.value().c_str());
  EXPECT_STREQ(kFirstDisplay, matches[0].second.sys_path.value().c_str());

  AddDisplay(kSecondDisplay);

  // After adding both sensors/displays, there should be two matches.
  matches = controller_.GetAmbientLightSensorAndDisplayMatchesForTesting();
  ASSERT_EQ(2, matches.size());
  if (matches[0].first.value().compare(kFirstSensor) == 0) {
    EXPECT_STREQ(kFirstSensor, matches[0].first.value().c_str());
    EXPECT_STREQ(kFirstDisplay, matches[0].second.sys_path.value().c_str());
    EXPECT_STREQ(kSecondSensor, matches[1].first.value().c_str());
    EXPECT_STREQ(kSecondDisplay, matches[1].second.sys_path.value().c_str());
  } else if (matches[0].first.value().compare(kSecondSensor) == 0) {
    EXPECT_STREQ(kSecondSensor, matches[0].first.value().c_str());
    EXPECT_STREQ(kSecondDisplay, matches[0].second.sys_path.value().c_str());
    EXPECT_STREQ(kFirstSensor, matches[1].first.value().c_str());
    EXPECT_STREQ(kFirstDisplay, matches[1].second.sys_path.value().c_str());
  } else {
    ADD_FAILURE();
  }

  RemoveDisplay(kFirstDisplay);

  // Removing the first display should drop that match.
  matches = controller_.GetAmbientLightSensorAndDisplayMatchesForTesting();
  ASSERT_EQ(1, matches.size());
  EXPECT_STREQ(kSecondSensor, matches[0].first.value().c_str());
  EXPECT_STREQ(kSecondDisplay, matches[0].second.sys_path.value().c_str());

  RemoveSensor(kSecondSensor);

  // There should be 0 matches after removing the second sensor. The first
  // sensor and second display should not match even though they are both
  // connected because their association score of 3 is not above the minimum
  // threshold.
  EXPECT_EQ(
      0, controller_.GetAmbientLightSensorAndDisplayMatchesForTesting().size());
}

TEST_F(ExternalBacklightControllerTest,
       ExternalScreenBrightnessFailedSignalAndDebounceSuppression) {
  // User requests with no external DDC/CI display connected emit
  // ExternalScreenBrightnessFailed unless DisplayWatcher has a pending display
  // configuration change waiting to be published.
  display_watcher_.set_has_pending_display_change(true);
  test::CallIncreaseExternalScreenBrightness(&dbus_wrapper_);
  test::CallDecreaseExternalScreenBrightness(&dbus_wrapper_);
  EXPECT_EQ(0u, dbus_wrapper_.num_sent_signals());

  display_watcher_.set_has_pending_display_change(false);
  test::CallIncreaseExternalScreenBrightness(&dbus_wrapper_);
  EXPECT_TRUE(PopSingleSentSignal(kExternalScreenBrightnessFailedSignal));

  // Connect a single external display and verify all three DDC/CI failure
  // points (RequestBrightness write, ReadBrightness I2C/reply, WriteBrightness
  // write), non-user suppression, and self-healing when DDC/CI succeeds again.
  AddDisplay(kFirstDisplay);
  FakeExternalDisplayDelegate* delegate =
      AttachFakeDelegate(&controller_, 95, 100);

  // 1. RequestBrightness() send failure schedules a retry; once the retry
  // fails, ExternalScreenBrightnessFailed is emitted.
  delegate->set_fail_get_writes(true);
  test::CallIncreaseExternalScreenBrightness(&dbus_wrapper_);
  EXPECT_EQ(0u, dbus_wrapper_.num_sent_signals());
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_));
  EXPECT_TRUE(PopSingleSentSignal(kExternalScreenBrightnessFailedSignal));

  // Ambient-light failure must never emit ExternalScreenBrightnessFailed.
  controller_.SetBrightnessPercentForAmbientLight(
      display_watcher_.GetDisplays()[0], 40.0);
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_));
  EXPECT_EQ(0u, dbus_wrapper_.num_sent_signals());

  // 2. Coalesced rapid user requests whose ReadBrightness() fails emit exactly
  // one failure signal when the retry finishes.
  delegate->set_fail_get_writes(false);
  delegate->set_fail_reads(true);
  test::CallIncreaseExternalScreenBrightness(&dbus_wrapper_);
  test::CallIncreaseExternalScreenBrightness(&dbus_wrapper_);
  EXPECT_EQ(0u, dbus_wrapper_.num_sent_signals());
  ASSERT_TRUE(TriggerDisplayTimeout(
      &controller_));  // initial read fails, retry timer started
  ASSERT_TRUE(TriggerDisplayTimeout(
      &controller_));  // retry request sent, read timer started
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_));  // retry read fails
  EXPECT_TRUE(PopSingleSentSignal(kExternalScreenBrightnessFailedSignal));

  // Non-zero VCP 0x10 reply result code also emits
  // ExternalScreenBrightnessFailed after retry.
  delegate->set_fail_reads(false);
  delegate->set_reply_result_code(0x1);
  test::CallDecreaseExternalScreenBrightness(&dbus_wrapper_);
  ASSERT_TRUE(
      TriggerDisplayTimeout(&controller_));  // read fails, retry timer started
  ASSERT_TRUE(TriggerDisplayTimeout(
      &controller_));  // retry request sent, read timer started
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_));  // retry read fails
  EXPECT_TRUE(PopSingleSentSignal(kExternalScreenBrightnessFailedSignal));

  // 3. WriteBrightness() write failure schedules a retry; once the retry write
  // fails, ExternalScreenBrightnessFailed is emitted.
  delegate->set_reply_result_code(0x0);
  delegate->set_fail_set_writes(true);
  test::CallIncreaseExternalScreenBrightness(&dbus_wrapper_);
  ASSERT_TRUE(TriggerDisplayTimeout(
      &controller_));  // read succeeds (95, 100), write fails, retry timer
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_));  // retry write fails
  EXPECT_TRUE(PopSingleSentSignal(kExternalScreenBrightnessFailedSignal));

  // Re-enabling DDC/CI self-heals on the next user request (95% -> 100%), and a
  // clamped boundary request at 100% still emits
  // ExternalScreenBrightnessChanged rather than ExternalScreenBrightnessFailed.
  delegate->set_fail_set_writes(false);
  test::CallIncreaseExternalScreenBrightness(&dbus_wrapper_);
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_));
  test::CallIncreaseExternalScreenBrightness(&dbus_wrapper_);
  ASSERT_EQ(2u, dbus_wrapper_.num_sent_signals());
  test::CheckBrightnessChangedSignal(
      &dbus_wrapper_, 0, kExternalScreenBrightnessChangedSignal, 100.0,
      BacklightBrightnessChange_Cause_USER_REQUEST);
  test::CheckBrightnessChangedSignal(
      &dbus_wrapper_, 1, kExternalScreenBrightnessChangedSignal, 100.0,
      BacklightBrightnessChange_Cause_USER_REQUEST);
}

TEST_F(ExternalBacklightControllerTest,
       MultiDisplayMixedDdcCapabilityAndMidCycleUnplug) {
  AddDisplay(kFirstDisplay);
  AddDisplay(kSecondDisplay);
  ASSERT_EQ(2u, display_watcher_.GetDisplays().size());
  const system::DisplayInfo first_info = display_watcher_.GetDisplays()[0];
  const system::DisplayInfo second_info = display_watcher_.GetDisplays()[1];
  FakeExternalDisplayDelegate* first_delegate =
      AttachFakeDelegate(&controller_, first_info, 50, 100);
  FakeExternalDisplayDelegate* second_delegate =
      AttachFakeDelegate(&controller_, second_info, 70, 100);

  // Even when the first display fails synchronously on RequestBrightness()
  // before the second display completes its 40 ms DDC/CI read, no failure
  // signal is emitted when the second display succeeds.
  first_delegate->set_fail_get_writes(true);
  test::CallIncreaseExternalScreenBrightness(&dbus_wrapper_);
  EXPECT_EQ(0u, dbus_wrapper_.num_sent_signals());
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_, second_info));
  ASSERT_EQ(1u, dbus_wrapper_.num_sent_signals());
  test::CheckBrightnessChangedSignal(
      &dbus_wrapper_, 0, kExternalScreenBrightnessChangedSignal, 75.0,
      BacklightBrightnessChange_Cause_USER_REQUEST);
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_, first_info));
  dbus_wrapper_.ClearSentSignals();

  // Reverse ordering: second display succeeds synchronously from cache while
  // first display fails asynchronously after 40 ms -> still 0 failure signals.
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_, second_info));
  first_delegate->set_fail_get_writes(false);
  first_delegate->set_fail_reads(true);
  test::CallIncreaseExternalScreenBrightness(&dbus_wrapper_);
  ASSERT_EQ(1u, dbus_wrapper_.num_sent_signals());
  test::CheckBrightnessChangedSignal(
      &dbus_wrapper_, 0, kExternalScreenBrightnessChangedSignal, 80.0,
      BacklightBrightnessChange_Cause_USER_REQUEST);
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_, first_info));
  EXPECT_EQ(1u, dbus_wrapper_.num_sent_signals());
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_, first_info));
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_, first_info));
  dbus_wrapper_.ClearSentSignals();

  // When ALL connected external displays fail in the same user request cycle,
  // ExternalScreenBrightnessFailed is emitted exactly once.
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_, second_info));
  second_delegate->set_fail_set_writes(true);
  test::CallDecreaseExternalScreenBrightness(&dbus_wrapper_);
  EXPECT_EQ(0u, dbus_wrapper_.num_sent_signals());
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_, second_info));
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_, first_info));
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_, first_info));
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_, first_info));
  EXPECT_TRUE(PopSingleSentSignal(kExternalScreenBrightnessFailedSignal));

  // Cancelling user adjustments (+5% then -5%) during kDdcSetDelay combined
  // with an ambient-light SetBrightness() on one display complete the user
  // cycle cleanly without leaking pending state.
  first_delegate->set_fail_reads(false);
  second_delegate->set_fail_set_writes(false);
  test::CallIncreaseExternalScreenBrightness(&dbus_wrapper_);
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_, first_info));
  dbus_wrapper_.ClearSentSignals();

  test::CallIncreaseExternalScreenBrightness(&dbus_wrapper_);
  test::CallDecreaseExternalScreenBrightness(&dbus_wrapper_);
  controller_.SetBrightnessPercentForAmbientLight(first_info, 40.0);
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_, first_info));
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_, second_info));
  ASSERT_EQ(2u, dbus_wrapper_.num_sent_signals());
  test::CheckBrightnessChangedSignal(
      &dbus_wrapper_, 0, kExternalScreenBrightnessChangedSignal, 40.0,
      BacklightBrightnessChange_Cause_USER_REQUEST);
  test::CheckBrightnessChangedSignal(
      &dbus_wrapper_, 1, kExternalScreenBrightnessChangedSignal, 85.0,
      BacklightBrightnessChange_Cause_USER_REQUEST);
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_, first_info));
  dbus_wrapper_.ClearSentSignals();

  // If one display has already failed in the cycle and the last remaining
  // pending display is unplugged before replying,
  // ExternalScreenBrightnessFailed is still emitted.
  RemoveDisplay(kFirstDisplay);
  AddDisplay(kFirstDisplay);
  first_delegate = AttachFakeDelegate(&controller_, first_info, 50, 100);
  second_delegate->set_fail_set_writes(true);
  test::CallDecreaseExternalScreenBrightness(&dbus_wrapper_);
  EXPECT_EQ(0u, dbus_wrapper_.num_sent_signals());
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_, second_info));
  RemoveDisplay(kFirstDisplay);
  EXPECT_TRUE(PopSingleSentSignal(kExternalScreenBrightnessFailedSignal));

  // Non-user (ambient-light) failures or successes on a mid-cycle hotplugged
  // display must neither trigger a spurious failure on unplug nor mask an
  // all-user-targets-failed cycle.
  AddDisplay(kFirstDisplay);
  first_delegate = AttachFakeDelegate(&controller_, first_info, 50, 100);
  RemoveDisplay(kSecondDisplay);
  test::CallIncreaseExternalScreenBrightness(&dbus_wrapper_);
  AddDisplay(kSecondDisplay);
  second_delegate = AttachFakeDelegate(&controller_, second_info, 70, 100);
  second_delegate->set_fail_get_writes(true);
  controller_.SetBrightnessPercentForAmbientLight(second_info, 40.0);
  RemoveDisplay(kFirstDisplay);
  EXPECT_EQ(0u, dbus_wrapper_.num_sent_signals());

  AddDisplay(kFirstDisplay);
  first_delegate = AttachFakeDelegate(&controller_, first_info, 50, 100);
  first_delegate->set_fail_reads(true);
  RemoveDisplay(kSecondDisplay);
  test::CallIncreaseExternalScreenBrightness(&dbus_wrapper_);
  AddDisplay(kSecondDisplay);
  second_delegate = AttachFakeDelegate(&controller_, second_info, 70, 100);
  controller_.SetBrightnessPercentForAmbientLight(second_info, 40.0);
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_, second_info));
  ASSERT_EQ(1u, dbus_wrapper_.num_sent_signals());
  test::CheckBrightnessChangedSignal(
      &dbus_wrapper_, 0, kExternalScreenBrightnessChangedSignal, 40.0,
      BacklightBrightnessChange_Cause_AMBIENT_LIGHT_CHANGED);
  dbus_wrapper_.ClearSentSignals();
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_, first_info));
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_, first_info));
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_, first_info));
  EXPECT_TRUE(PopSingleSentSignal(kExternalScreenBrightnessFailedSignal));
}

TEST_F(ExternalBacklightControllerTest, CurrentlyOffSuppressionAndWakeSettle) {
  AddDisplay(kFirstDisplay);
  FakeExternalDisplayDelegate* delegate =
      AttachFakeDelegate(&controller_, 50, 100);

  // Initialize clock with a non-zero time.
  const base::TimeTicks now = base::TimeTicks() + base::Hours(1);
  SetControllerTime(now);

  // Turn off display for inactivity. Even if DDC/CI fails, failure signals are
  // suppressed while displays are off.
  controller_.SetOffForInactivity(true);
  delegate->set_fail_get_writes(true);
  test::CallIncreaseExternalScreenBrightness(&dbus_wrapper_);
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_));  // retry fails, suppressed
  EXPECT_EQ(0u, dbus_wrapper_.num_sent_signals());

  // Wake display from inactivity. During the post-wake settle window, external
  // brightness failure signals are suppressed to let the monitor modeset.
  controller_.SetOffForInactivity(false);
  test::CallIncreaseExternalScreenBrightness(&dbus_wrapper_);
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_));  // retry fails, suppressed
  EXPECT_EQ(0u, dbus_wrapper_.num_sent_signals());

  // Advance time by 1 second (within the 2-second settle window).
  SetControllerTime(now + base::Seconds(1));
  test::CallIncreaseExternalScreenBrightness(&dbus_wrapper_);
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_));  // retry fails, suppressed
  EXPECT_EQ(0u, dbus_wrapper_.num_sent_signals());

  // Advance time past the settle window (3 seconds total).
  SetControllerTime(now + base::Seconds(3));

  // Now that the settle window has elapsed, DDC/CI failures emit
  // ExternalScreenBrightnessFailed.
  test::CallIncreaseExternalScreenBrightness(&dbus_wrapper_);
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_));  // retry fails
  EXPECT_TRUE(PopSingleSentSignal(kExternalScreenBrightnessFailedSignal));

  // Allow delegate writes to succeed now that the monitor has settled.
  delegate->set_fail_get_writes(false);
  test::CallIncreaseExternalScreenBrightness(&dbus_wrapper_);
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_));  // read
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_));  // write
  EXPECT_EQ(1u, dbus_wrapper_.num_sent_signals());
  test::CheckBrightnessChangedSignal(
      &dbus_wrapper_, 0, kExternalScreenBrightnessChangedSignal, 55.0,
      BacklightBrightnessChange_Cause_USER_REQUEST);
}

TEST_F(ExternalBacklightControllerTest,
       MidCycleDockUnplugAndDebounceSuppression) {
  // Case 1: Dual-monitor dock unplugged mid-cycle after Display 1 failed.
  AddDisplay(kFirstDisplay);
  AddDisplay(kSecondDisplay);
  const system::DisplayInfo first_info = display_watcher_.GetDisplays()[0];
  const system::DisplayInfo second_info = display_watcher_.GetDisplays()[1];
  FakeExternalDisplayDelegate* first_delegate =
      AttachFakeDelegate(&controller_, first_info, 50, 100);
  AttachFakeDelegate(&controller_, second_info, 70, 100);

  first_delegate->set_fail_get_writes(true);
  test::CallIncreaseExternalScreenBrightness(&dbus_wrapper_);
  // Unplug entire dock so external_displays_ becomes empty.
  RemoveDisplay(kFirstDisplay);
  RemoveDisplay(kSecondDisplay);
  // Verify no failure signal is emitted because no external displays remain.
  EXPECT_EQ(0u, dbus_wrapper_.num_sent_signals());

  // Case 2: Monitor cable pulled mid-adjustment sets HasPendingDisplayChange().
  AddDisplay(kFirstDisplay);
  first_delegate = AttachFakeDelegate(&controller_, 50, 100);
  first_delegate->set_fail_get_writes(true);
  display_watcher_.set_has_pending_display_change(true);
  test::CallIncreaseExternalScreenBrightness(&dbus_wrapper_);
  EXPECT_EQ(0u, dbus_wrapper_.num_sent_signals());
  ASSERT_TRUE(TriggerDisplayTimeout(&controller_));
  EXPECT_EQ(0u, dbus_wrapper_.num_sent_signals());
}

}  // namespace power_manager::policy
