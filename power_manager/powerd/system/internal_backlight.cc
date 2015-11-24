// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/internal_backlight.h"

#include <cmath>
#include <string>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <linux/fb.h>

#include "power_manager/common/clock.h"
#include "power_manager/common/util.h"

namespace power_manager {
namespace system {

namespace {

// Reads the value from |path| to |level|. Returns false on failure.
bool ReadBrightnessLevelFromFile(const base::FilePath& path, int64_t* level) {
  DCHECK(level);

  std::string level_str;
  if (!base::ReadFileToString(path, &level_str)) {
    PLOG(ERROR) << "Unable to read brightness from " << path.value();
    return false;
  }

  base::TrimWhitespaceASCII(level_str, base::TRIM_TRAILING, &level_str);
  if (!base::StringToInt64(level_str, level)) {
    LOG(ERROR) << "Unable to parse brightness \"" << level_str << "\" from "
               << path.value();
    return false;
  }

  return true;
}

// Writes |value| to |path|. Returns false on failure.
bool WriteFile(const base::FilePath& path, int64_t value) {
  std::string buf = base::Int64ToString(value);
  VLOG(1) << "Writing " << buf << " to " << path.value();
  if (base::WriteFile(path, buf.data(), buf.size()) == -1) {
    PLOG(ERROR) << "Unable to write \"" << buf << "\" to " << path.value();
    return false;
  }
  return true;
}

// When animating a brightness level transition, amount of time in milliseconds
// to wait between each update.
const int kTransitionIntervalMs = 20;

}  // namespace

const char InternalBacklight::kBrightnessFilename[] = "brightness";
const char InternalBacklight::kMaxBrightnessFilename[] = "max_brightness";
const char InternalBacklight::kActualBrightnessFilename[] = "actual_brightness";
const char InternalBacklight::kResumeBrightnessFilename[] = "resume_brightness";
const char InternalBacklight::kBlPowerFilename[] = "bl_power";

InternalBacklight::InternalBacklight()
    : clock_(new Clock),
      max_brightness_level_(0),
      current_brightness_level_(0),
      transition_start_level_(0),
      transition_end_level_(0) {
}

InternalBacklight::~InternalBacklight() {}

bool InternalBacklight::Init(const base::FilePath& base_path,
                             const base::FilePath::StringType& pattern) {
  base::FileEnumerator enumerator(
      base_path, false, base::FileEnumerator::DIRECTORIES, pattern);

  // Find the backlight interface with greatest granularity (highest max).
  for (base::FilePath device_path = enumerator.Next(); !device_path.empty();
       device_path = enumerator.Next()) {
    if (device_path.BaseName().value()[0] == '.')
      continue;

    const base::FilePath max_brightness_path =
        device_path.Append(kMaxBrightnessFilename);
    if (!base::PathExists(max_brightness_path)) {
      LOG(WARNING) << "Can't find " << max_brightness_path.value();
      continue;
    }

    const base::FilePath brightness_path =
        device_path.Append(kBrightnessFilename);
    if (access(brightness_path.value().c_str(), R_OK | W_OK) != 0) {
      LOG(WARNING) << "Can't write to " << brightness_path.value();
      continue;
    }

    int64_t max_level = 0;
    if (!ReadBrightnessLevelFromFile(max_brightness_path, &max_level))
      continue;

    if (max_level <= max_brightness_level_)
      continue;

    brightness_path_ = brightness_path;
    max_brightness_path_ = max_brightness_path;
    max_brightness_level_ = max_level;

    // Technically all screen backlights should implement actual_brightness,
    // but we'll handle ones that don't. This allows us to work with keyboard
    // backlights too.
    actual_brightness_path_ = device_path.Append(kActualBrightnessFilename);
    if (!base::PathExists(actual_brightness_path_))
      actual_brightness_path_ = brightness_path_;

    resume_brightness_path_ = device_path.Append(kResumeBrightnessFilename);

    const base::FilePath power_path = device_path.Append(kBlPowerFilename);
    if (base::PathExists(power_path))
      bl_power_path_ = power_path;
  }

  if (max_brightness_level_ <= 0) {
    LOG(ERROR) << "Can't init backlight interface";
    return false;
  }

  ReadBrightnessLevelFromFile(actual_brightness_path_,
                              &current_brightness_level_);
  return true;
}

bool InternalBacklight::TriggerTransitionTimeoutForTesting() {
  CHECK(transition_timer_.IsRunning());
  HandleTransitionTimeout();
  return transition_timer_.IsRunning();
}

int64_t InternalBacklight::GetMaxBrightnessLevel() {
  return max_brightness_level_;
}

int64_t InternalBacklight::GetCurrentBrightnessLevel() {
  return current_brightness_level_;
}

bool InternalBacklight::SetBrightnessLevel(int64_t level,
                                           base::TimeDelta interval) {
  if (brightness_path_.empty()) {
    LOG(ERROR) << "Cannot find backlight brightness file.";
    return false;
  }

  if (level == current_brightness_level_) {
    CancelTransition();
    return true;
  }

  if (interval.InMilliseconds() <= kTransitionIntervalMs) {
    CancelTransition();
    return WriteBrightness(level);
  }

  transition_start_time_ = clock_->GetCurrentTime();
  transition_end_time_ = transition_start_time_ + interval;
  transition_start_level_ = current_brightness_level_;
  transition_end_level_ = level;
  if (!transition_timer_.IsRunning()) {
    transition_timer_.Start(FROM_HERE,
        base::TimeDelta::FromMilliseconds(kTransitionIntervalMs), this,
        &InternalBacklight::HandleTransitionTimeout);
    transition_timer_start_time_ = transition_start_time_;
  }
  return true;
}

bool InternalBacklight::SetResumeBrightnessLevel(int64_t level) {
  if (resume_brightness_path_.empty()) {
    LOG(ERROR) << "Cannot find backlight resume brightness file.";
    return false;
  }

  return WriteFile(resume_brightness_path_, level);
}

bool InternalBacklight::TransitionInProgress() const {
  return transition_timer_.IsRunning();
}

bool InternalBacklight::WriteBrightness(int64_t new_level) {
  // If the backlight is about to be turned on, write FB_BLANK_UNBLANK
  // to bl_power first.
  if (current_brightness_level_ == 0 && !bl_power_path_.empty())
    WriteFile(bl_power_path_, FB_BLANK_UNBLANK);

  if (!WriteFile(brightness_path_, new_level))
    return false;

  current_brightness_level_ = new_level;

  // If the backlight level just went to 0, write FB_BLANK_POWERDOWN
  // to bl_power.
  if (current_brightness_level_ == 0 && !bl_power_path_.empty())
    WriteFile(bl_power_path_, FB_BLANK_POWERDOWN);

  return true;
}

void InternalBacklight::HandleTransitionTimeout() {
  base::TimeTicks now = clock_->GetCurrentTime();
  int64_t new_level = 0;

  if (now >= transition_end_time_) {
    new_level = transition_end_level_;
    transition_timer_.Stop();
  } else {
    double transition_fraction =
        (now - transition_start_time_).InMillisecondsF() /
        (transition_end_time_ - transition_start_time_).InMillisecondsF();
    int64_t intermediate_amount = lround(
        transition_fraction *
        (transition_end_level_ - transition_start_level_));
    new_level = transition_start_level_ + intermediate_amount;
  }

  if (new_level == current_brightness_level_)
    return;

  WriteBrightness(new_level);
}

void InternalBacklight::CancelTransition() {
  transition_timer_.Stop();
  transition_start_time_ = base::TimeTicks();
  transition_end_time_ = base::TimeTicks();
  transition_start_level_ = current_brightness_level_;
  transition_end_level_ = current_brightness_level_;
}

}  // namespace system
}  // namespace power_manager
