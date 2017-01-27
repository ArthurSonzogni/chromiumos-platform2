// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/backlight_controller_stub.h"

#include <base/logging.h>

namespace power_manager {
namespace policy {

BacklightControllerStub::BacklightControllerStub() {}

BacklightControllerStub::~BacklightControllerStub() {}

void BacklightControllerStub::ResetStats() {
  power_source_changes_.clear();
  display_mode_changes_.clear();
  session_state_changes_.clear();
  power_button_presses_ = 0;
  user_activity_reports_.clear();
  video_activity_reports_.clear();
  hover_state_changes_.clear();
  tablet_mode_changes_.clear();
  policy_changes_.clear();
  chrome_starts_ = 0;
  user_brightness_percent_ = 0.0;
  num_user_brightness_increases_ = 0;
  num_user_brightness_decreases_ = 0;
}

void BacklightControllerStub::NotifyObservers(double percent,
                                              BrightnessChangeCause cause) {
  percent_ = percent;
  FOR_EACH_OBSERVER(BacklightControllerObserver,
                    observers_,
                    OnBrightnessChange(percent_, cause, this));
}

void BacklightControllerStub::AddObserver(
    policy::BacklightControllerObserver* observer) {
  DCHECK(observer);
  observers_.AddObserver(observer);
}

void BacklightControllerStub::RemoveObserver(
    policy::BacklightControllerObserver* observer) {
  DCHECK(observer);
  observers_.RemoveObserver(observer);
}

void BacklightControllerStub::HandlePowerSourceChange(PowerSource source) {
  power_source_changes_.push_back(source);
}

void BacklightControllerStub::HandleDisplayModeChange(DisplayMode mode) {
  display_mode_changes_.push_back(mode);
}

void BacklightControllerStub::HandleSessionStateChange(SessionState state) {
  session_state_changes_.push_back(state);
}

void BacklightControllerStub::HandlePowerButtonPress() {
  power_button_presses_++;
}

void BacklightControllerStub::HandleUserActivity(UserActivityType type) {
  user_activity_reports_.push_back(type);
}

void BacklightControllerStub::HandleVideoActivity(bool is_fullscreen) {
  video_activity_reports_.push_back(is_fullscreen);
}

void BacklightControllerStub::HandleHoverStateChange(bool hovering) {
  hover_state_changes_.push_back(hovering);
}

void BacklightControllerStub::HandleTabletModeChange(TabletMode mode) {
  tablet_mode_changes_.push_back(mode);
}

void BacklightControllerStub::HandlePolicyChange(
    const PowerManagementPolicy& policy) {
  policy_changes_.push_back(policy);
}

void BacklightControllerStub::HandleChromeStart() {
  chrome_starts_++;
}

void BacklightControllerStub::SetDimmedForInactivity(bool dimmed) {
  dimmed_ = dimmed;
}

void BacklightControllerStub::SetOffForInactivity(bool off) {
  off_ = off;
}

void BacklightControllerStub::SetSuspended(bool suspended) {
  suspended_ = suspended;
}

void BacklightControllerStub::SetShuttingDown(bool shutting_down) {
  shutting_down_ = shutting_down;
}

void BacklightControllerStub::SetDocked(bool docked) {
  docked_ = docked;
}

void BacklightControllerStub::SetForcedOff(bool forced_off) {
  forced_off_ = forced_off;
}

bool BacklightControllerStub::GetForcedOff() {
  return forced_off_;
}

bool BacklightControllerStub::GetBrightnessPercent(double* percent) {
  DCHECK(percent);
  *percent = percent_;
  return true;
}

bool BacklightControllerStub::SetUserBrightnessPercent(double percent,
                                                       Transition transition) {
  user_brightness_percent_ = percent;
  return true;
}

bool BacklightControllerStub::IncreaseUserBrightness() {
  num_user_brightness_increases_++;
  return true;
}

bool BacklightControllerStub::DecreaseUserBrightness(bool allow_off) {
  num_user_brightness_decreases_++;
  return true;
}

}  // namespace policy
}  // namespace power_manager
