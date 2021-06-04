// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/external_backlight_controller.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <chromeos/dbus/service_constants.h>

#include "power_manager/common/prefs.h"
#include "power_manager/powerd/policy/backlight_controller_observer.h"
#include "power_manager/powerd/system/ambient_light_sensor_delegate_file.h"
#include "power_manager/powerd/system/ambient_light_sensor_watcher_interface.h"
#include "power_manager/powerd/system/dbus_wrapper.h"
#include "power_manager/powerd/system/display/display_power_setter.h"
#include "power_manager/powerd/system/display/display_watcher.h"
#include "power_manager/powerd/system/display/external_display.h"
#include "power_manager/powerd/system/external_ambient_light_sensor_factory_interface.h"

#include <base/check.h>
#include <base/notreached.h>

namespace power_manager {
namespace policy {

namespace {

// Amount the brightness will be adjusted up or down in response to a user
// request, as a linearly-calculated percent in the range [0.0, 100.0].
constexpr double kBrightnessAdjustmentPercent = 5.0;

// Minimum number of syspath components that must be the same for an external
// display to be matched with an external ambient light sensor.
constexpr int kMinimumAssociationScore = 4;

// Constants used to initialize ExternalAmbientLightHandlers. See
// AmbientLightHandler::Init for a more detailed explanation of these values.
constexpr double kExternalAmbientLightHandlerInitialBrightness = 100.0;
constexpr double kExternalAmbientLightHandlerSmoothingConstant = 1.0;

}  // namespace

ExternalBacklightController::ExternalBacklightController()
    : weak_ptr_factory_(this) {}

ExternalBacklightController::~ExternalBacklightController() {
  if (display_watcher_)
    display_watcher_->RemoveObserver(this);
  if (ambient_light_sensor_watcher_) {
    ambient_light_sensor_watcher_->RemoveObserver(this);
  }
}

void ExternalBacklightController::Init(
    PrefsInterface* prefs,
    system::AmbientLightSensorWatcherInterface* ambient_light_sensor_watcher,
    system::ExternalAmbientLightSensorFactoryInterface*
        external_ambient_light_sensor_factory,
    system::DisplayWatcherInterface* display_watcher,
    system::DisplayPowerSetterInterface* display_power_setter,
    system::DBusWrapperInterface* dbus_wrapper) {
  prefs_ = prefs;
  ambient_light_sensor_watcher_ = ambient_light_sensor_watcher;
  external_ambient_light_sensor_factory_ =
      external_ambient_light_sensor_factory;
  if (ambient_light_sensor_watcher_) {
    CHECK(prefs_->GetString(kExternalBacklightAlsStepsPref,
                            &external_backlight_als_steps_))
        << "Failed to read pref " << kExternalBacklightAlsStepsPref;
    ambient_light_sensor_watcher_->AddObserver(this);
  }
  display_watcher_ = display_watcher;
  display_power_setter_ = display_power_setter;
  display_watcher_->AddObserver(this);
  dbus_wrapper_ = dbus_wrapper;

  RegisterSetBrightnessHandler(
      dbus_wrapper_, kSetScreenBrightnessMethod,
      base::Bind(&ExternalBacklightController::HandleSetBrightnessRequest,
                 weak_ptr_factory_.GetWeakPtr()));
  RegisterIncreaseBrightnessHandler(
      dbus_wrapper_, kIncreaseScreenBrightnessMethod,
      base::Bind(&ExternalBacklightController::HandleIncreaseBrightnessRequest,
                 weak_ptr_factory_.GetWeakPtr()));
  RegisterDecreaseBrightnessHandler(
      dbus_wrapper_, kDecreaseScreenBrightnessMethod,
      base::Bind(&ExternalBacklightController::HandleDecreaseBrightnessRequest,
                 weak_ptr_factory_.GetWeakPtr()));
  RegisterGetBrightnessHandler(
      dbus_wrapper_, kGetScreenBrightnessPercentMethod,
      base::Bind(&ExternalBacklightController::HandleGetBrightnessRequest,
                 weak_ptr_factory_.GetWeakPtr()));

  UpdateDisplays(display_watcher_->GetDisplays());
  if (ambient_light_sensor_watcher_) {
    external_ambient_light_sensors_info_ =
        ambient_light_sensor_watcher_->GetAmbientLightSensors();
    MatchAmbientLightSensorsToDisplays();
  }
}

void ExternalBacklightController::AddObserver(
    BacklightControllerObserver* observer) {
  DCHECK(observer);
  observers_.AddObserver(observer);
}

void ExternalBacklightController::RemoveObserver(
    BacklightControllerObserver* observer) {
  DCHECK(observer);
  observers_.RemoveObserver(observer);
}

void ExternalBacklightController::HandlePowerSourceChange(PowerSource source) {
  for (auto& [path, handler] : external_ambient_light_sensors_) {
    handler->HandlePowerSourceChange(source);
  }
}

void ExternalBacklightController::HandleDisplayModeChange(DisplayMode mode) {}

void ExternalBacklightController::HandleSessionStateChange(SessionState state) {
  if (state == SessionState::STARTED)
    num_brightness_adjustments_in_session_ = 0;
}

void ExternalBacklightController::HandlePowerButtonPress() {}

void ExternalBacklightController::HandleLidStateChange(LidState state) {}

void ExternalBacklightController::HandleVideoActivity(bool is_fullscreen) {}

void ExternalBacklightController::HandleHoverStateChange(bool hovering) {}

void ExternalBacklightController::HandleTabletModeChange(TabletMode mode) {}

void ExternalBacklightController::HandleUserActivity(UserActivityType type) {}

void ExternalBacklightController::HandleWakeNotification() {}

void ExternalBacklightController::HandlePolicyChange(
    const PowerManagementPolicy& policy) {}

void ExternalBacklightController::HandleDisplayServiceStart() {
  display_power_setter_->SetDisplaySoftwareDimming(dimmed_for_inactivity_);
  display_power_setter_->SetDisplayPower(currently_off_
                                             ? chromeos::DISPLAY_POWER_ALL_OFF
                                             : chromeos::DISPLAY_POWER_ALL_ON,
                                         base::TimeDelta());
  NotifyObservers(BacklightBrightnessChange_Cause_OTHER);
}

void ExternalBacklightController::SetDimmedForInactivity(bool dimmed) {
  if (dimmed != dimmed_for_inactivity_) {
    dimmed_for_inactivity_ = dimmed;
    display_power_setter_->SetDisplaySoftwareDimming(dimmed);
  }
}

void ExternalBacklightController::SetOffForInactivity(bool off) {
  if (off == off_for_inactivity_)
    return;
  off_for_inactivity_ = off;
  UpdateScreenPowerState(off ? BacklightBrightnessChange_Cause_USER_INACTIVITY
                             : BacklightBrightnessChange_Cause_USER_ACTIVITY);
}

void ExternalBacklightController::SetSuspended(bool suspended) {
  if (suspended == suspended_)
    return;
  suspended_ = suspended;
  UpdateScreenPowerState(BacklightBrightnessChange_Cause_OTHER);

  if (!suspended) {
    for (auto& [path, handler] : external_ambient_light_sensors_) {
      handler->HandleResume();
    }
  }
}

void ExternalBacklightController::SetShuttingDown(bool shutting_down) {
  if (shutting_down == shutting_down_)
    return;
  shutting_down_ = shutting_down;
  UpdateScreenPowerState(BacklightBrightnessChange_Cause_OTHER);
}

bool ExternalBacklightController::GetBrightnessPercent(double* percent) {
  return false;
}

void ExternalBacklightController::SetForcedOff(bool forced_off) {
  if (forced_off_ == forced_off)
    return;

  forced_off_ = forced_off;
  UpdateScreenPowerState(
      forced_off ? BacklightBrightnessChange_Cause_FORCED_OFF
                 : BacklightBrightnessChange_Cause_NO_LONGER_FORCED_OFF);
}

bool ExternalBacklightController::GetForcedOff() {
  return forced_off_;
}

int ExternalBacklightController::GetNumAmbientLightSensorAdjustments() const {
  return 0;
}

int ExternalBacklightController::GetNumUserAdjustments() const {
  return num_brightness_adjustments_in_session_;
}

double ExternalBacklightController::LevelToPercent(int64_t level) const {
  // This class doesn't have any knowledge of hardware backlight levels (since
  // it can simultaneously control multiple heterogeneous displays).
  NOTIMPLEMENTED();
  return 0.0;
}

int64_t ExternalBacklightController::PercentToLevel(double percent) const {
  NOTIMPLEMENTED();
  return 0;
}

void ExternalBacklightController::OnDisplaysChanged(
    const std::vector<system::DisplayInfo>& displays) {
  UpdateDisplays(displays);
  if (ambient_light_sensor_watcher_) {
    MatchAmbientLightSensorsToDisplays();
  }
}

void ExternalBacklightController::OnAmbientLightSensorsChanged(
    const std::vector<system::AmbientLightSensorInfo>& ambient_light_sensors) {
  external_ambient_light_sensors_info_ = ambient_light_sensors;
  MatchAmbientLightSensorsToDisplays();
}

void ExternalBacklightController::HandleIncreaseBrightnessRequest() {
  num_brightness_adjustments_in_session_++;
  AdjustBrightnessByPercent(kBrightnessAdjustmentPercent);
}

void ExternalBacklightController::HandleDecreaseBrightnessRequest(
    bool allow_off) {
  num_brightness_adjustments_in_session_++;
  AdjustBrightnessByPercent(-kBrightnessAdjustmentPercent);
}

void ExternalBacklightController::HandleSetBrightnessRequest(
    double percent,
    Transition transition,
    SetBacklightBrightnessRequest_Cause cause) {
  // Silently ignore requests to set to a specific percent. External displays
  // are buggy and DDC/CI is racy if the user is simultaneously adjusting the
  // brightness using physical buttons. Instead, we only support increasing and
  // decreasing the brightness.
}

void ExternalBacklightController::HandleGetBrightnessRequest(
    double* percent_out, bool* success_out) {
  // See HandleSetBrightnessRequest.
  *success_out = false;
}

void ExternalBacklightController::UpdateScreenPowerState(
    BacklightBrightnessChange_Cause cause) {
  bool should_turn_off =
      off_for_inactivity_ || suspended_ || shutting_down_ || forced_off_;
  if (should_turn_off != currently_off_) {
    currently_off_ = should_turn_off;
    display_power_setter_->SetDisplayPower(should_turn_off
                                               ? chromeos::DISPLAY_POWER_ALL_OFF
                                               : chromeos::DISPLAY_POWER_ALL_ON,
                                           base::TimeDelta());
    NotifyObservers(cause);
  }
}

void ExternalBacklightController::NotifyObservers(
    BacklightBrightnessChange_Cause cause) {
  for (BacklightControllerObserver& observer : observers_)
    observer.OnBrightnessChange(currently_off_ ? 0.0 : 100.0, cause, this);
}

void ExternalBacklightController::UpdateDisplays(
    const std::vector<system::DisplayInfo>& displays) {
  ExternalDisplayMap updated_displays;
  for (std::vector<system::DisplayInfo>::const_iterator it = displays.begin();
       it != displays.end(); ++it) {
    const system::DisplayInfo& info = *it;
    if (info.i2c_path.empty())
      continue;
    if (info.connector_status !=
        system::DisplayInfo::ConnectorStatus::CONNECTED)
      continue;

    auto existing_display_it = external_displays_.find(info);
    if (existing_display_it != external_displays_.end()) {
      // TODO(chromeos-power): Need to handle changed I2C paths?
      updated_displays.emplace(info, std::move(existing_display_it->second));
      continue;
    }
    auto delegate = std::make_unique<system::ExternalDisplay::RealDelegate>();
    delegate->Init(info.i2c_path);
    updated_displays.emplace(
        info, std::make_unique<system::ExternalDisplay>(std::move(delegate)));
  }
  external_displays_.swap(updated_displays);
}

void ExternalBacklightController::AdjustBrightnessByPercent(
    double percent_offset) {
  LOG(INFO) << "Adjusting brightness by " << percent_offset << "%";
  for (ExternalDisplayMap::const_iterator it = external_displays_.begin();
       it != external_displays_.end(); ++it) {
    it->second->AdjustBrightnessByPercent(percent_offset);
  }
}

int ExternalBacklightController::CalculateAssociationScore(
    const base::FilePath& a, const base::FilePath& b) {
  std::vector<std::string> a_components;
  std::vector<std::string> b_components;
  a.GetComponents(&a_components);
  b.GetComponents(&b_components);

  size_t score = 0;
  while (score < a_components.size() && score < b_components.size() &&
         a_components[score] == b_components[score]) {
    score++;
  }
  return score;
}

void ExternalBacklightController::MatchAmbientLightSensorsToDisplays() {
  ExternalAmbientLightSensorMap updated_ambient_light_sensors;
  for (const auto& als_info : external_ambient_light_sensors_info_) {
    int highest_score = 0;
    system::DisplayInfo best_matching_display;
    for (const auto& [display_info, external_display] : external_displays_) {
      int score =
          CalculateAssociationScore(display_info.sys_path, als_info.iio_path);
      if (score > highest_score) {
        highest_score = score;
        best_matching_display = display_info;
      }
    }
    if (highest_score >= kMinimumAssociationScore) {
      auto existing_als_it =
          external_ambient_light_sensors_.find(als_info.iio_path);
      if (existing_als_it != external_ambient_light_sensors_.end()) {
        updated_ambient_light_sensors.emplace(
            als_info.iio_path, std::move(existing_als_it->second));
        continue;
      }

      auto sensor =
          external_ambient_light_sensor_factory_->CreateSensor(als_info.device);
      auto handler = std::make_unique<ExternalAmbientLightHandler>(
          std::move(sensor), best_matching_display, this);
      handler->Init(external_backlight_als_steps_,
                    kExternalAmbientLightHandlerInitialBrightness,
                    kExternalAmbientLightHandlerSmoothingConstant);
      updated_ambient_light_sensors.emplace(als_info.iio_path,
                                            std::move(handler));

      LOG(INFO) << "Matched ALS (" << als_info.iio_path.value()
                << ") with display (" << best_matching_display.sys_path.value()
                << ") with score " << highest_score;
    }
  }
  external_ambient_light_sensors_.swap(updated_ambient_light_sensors);
}

void ExternalBacklightController::SetBrightnessPercentForAmbientLight(
    const system::DisplayInfo& display_info, double brightness_percent) {
  auto display_it = external_displays_.find(display_info);
  if (display_it != external_displays_.end()) {
    display_it->second->SetBrightness(brightness_percent);
  }
}

std::vector<std::pair<base::FilePath, system::DisplayInfo>>
ExternalBacklightController::
    GetAmbientLightSensorAndDisplayMatchesForTesting() {
  std::vector<std::pair<base::FilePath, system::DisplayInfo>> matches;
  for (const auto& [path, handler] : external_ambient_light_sensors_) {
    matches.push_back(std::make_pair(path, handler->GetDisplayInfo()));
  }
  return matches;
}

}  // namespace policy
}  // namespace power_manager
