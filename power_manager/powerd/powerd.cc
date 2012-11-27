// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/powerd.h"

#include <cras_client.h>
// cras_client.h includes cras_util.h which defines macros with the name min and
// max. This causes all sorts of fun issues when trying to use the STL version.
#undef min
#undef max

#include <libudev.h>
#include <stdint.h>
#include <sys/inotify.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

#include "base/bind.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/string_util.h"
#include "chromeos/dbus/dbus.h"
#include "chromeos/dbus/service_constants.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/common/util.h"
#include "power_manager/common/util_dbus.h"
#include "power_manager/powerd/backlight_controller.h"
#include "power_manager/powerd/metrics_constants.h"
#include "power_manager/powerd/power_supply.h"
#include "power_manager/powerd/state_control.h"
#include "power_supply_properties.pb.h"
#include "video_activity_update.pb.h"

namespace {

// Path for storing FileTagger files.
const char kTaggedFilePath[] = "/var/lib/power_manager";

// Path to power supply info.
const char kPowerStatusPath[] = "/sys/class/power_supply";

// Power supply subsystem for udev events.
const char kPowerSupplyUdevSubsystem[] = "power_supply";

// How long after last known audio activity to consider audio not to be playing,
// in milliseconds.
const int64 kAudioActivityThresholdMs = 5000;

// Valid string values for the state value of Session Manager
const std::string kValidStateStrings[] = { "started", "stopping", "stopped" };

// Set of valid state strings for easy sanity testing
std::set<std::string> kValidStates(
    kValidStateStrings,
    kValidStateStrings  + sizeof(kValidStateStrings) / sizeof(std::string));

// Minimum time a user must be idle to have returned from idle
const int64 kMinTimeForIdle = 10;

// Delay before retrying connecting to ChromeOS audio server.
const int64 kCrasRetryConnectMs = 1000;

const char kSysClassInputPath[] = "/sys/class/input";
const char kInputMatchPattern[] = "input*";
const char kUsbMatchString[] = "usb";
const char kBluetoothMatchString[] = "bluetooth";

// Upper limit to accept for raw battery times, in seconds. If the time of
// interest is above this level assume something is wrong.
const int64 kBatteryTimeMaxValidSec = 24 * 60 * 60;

}  // namespace

namespace power_manager {

// Timeouts are multiplied by this factor when projecting to external display.
static const int64 kProjectionTimeoutFactor = 2;

// Daemon: Main power manager. Adjusts device status based on whether the
//         user is idle and on video activity indicator from Chrome.
//         This daemon is responsible for dimming of the backlight, turning
//         the screen off, and suspending to RAM. The daemon also has the
//         capability of shutting the system down.

Daemon::Daemon(BacklightController* backlight_controller,
               PowerPrefs* prefs,
               MetricsLibraryInterface* metrics_lib,
               VideoDetector* video_detector,
               IdleDetector* idle,
               KeyboardBacklightController* keyboard_controller,
               const FilePath& run_dir)
    : backlight_controller_(backlight_controller),
      prefs_(prefs),
      metrics_lib_(metrics_lib),
      video_detector_(video_detector),
      idle_(idle),
      keyboard_controller_(keyboard_controller),
      low_battery_shutdown_time_s_(0),
      low_battery_shutdown_percent_(0.0),
      sample_window_max_(0),
      sample_window_min_(0),
      sample_window_diff_(0),
      taper_time_max_s_(0),
      taper_time_min_s_(0),
      taper_time_diff_s_(0),
      clean_shutdown_initiated_(false),
      low_battery_(false),
      battery_poll_interval_ms_(0),
      battery_poll_short_interval_ms_(0),
      enforce_lock_(false),
      plugged_state_(PLUGGED_STATE_UNKNOWN),
      file_tagger_(FilePath(kTaggedFilePath)),
      shutdown_state_(SHUTDOWN_STATE_NONE),
      suspender_(&locker_, &file_tagger_),
      run_dir_(run_dir),
      power_supply_(FilePath(kPowerStatusPath), prefs),
      power_state_(BACKLIGHT_UNINITIALIZED),
      is_power_status_stale_(true),
      battery_discharge_rate_metric_last_(0),
      current_session_state_("stopped"),
      udev_(NULL),
      state_control_(new StateControl(this)),
      poll_power_supply_timer_id_(0),
      is_projecting_(false),
      cras_client_(NULL),
      connected_to_cras_(false),
      shutdown_reason_(kShutdownReasonUnknown),
      require_usb_input_device_to_suspend_(false),
      battery_report_state_(BATTERY_REPORT_ADJUSTED),
      disable_dbus_for_testing_(false),
      keep_backlight_on_for_audio_(false) {
  idle_->AddObserver(this);
}

Daemon::~Daemon() {
  if (udev_)
    udev_unref(udev_);
  idle_->RemoveObserver(this);

  if (cras_client_) {
    if (connected_to_cras_)
      cras_client_stop(cras_client_);
    cras_client_destroy(cras_client_);
  }
}

void Daemon::Init() {
  ReadSettings();
  prefs_->StartPrefWatching(&(PrefChangeHandler), this);
  MetricInit();
  LOG_IF(ERROR, (!metrics_store_.Init()))
      << "Unable to initialize metrics store, so we are going to drop number of"
      << " sessions per charge data";

  locker_.Init(lock_on_idle_suspend_);
  RegisterUdevEventHandler();
  RegisterDBusMessageHandler();
  RetrieveSessionState();
  suspender_.Init(run_dir_, this);
  time_to_empty_average_.Init(sample_window_max_);
  time_to_full_average_.Init(sample_window_max_);
  power_supply_.Init();
  power_supply_.GetPowerStatus(&power_status_, false);
  OnPowerEvent(this, power_status_);
  UpdateAveragedTimes(&time_to_empty_average_,
                      &time_to_full_average_);
  file_tagger_.Init();
  backlight_controller_->SetObserver(this);

  // Create a client and connect it to the CRAS server.
  if (cras_client_create(&cras_client_)) {
    LOG(WARNING) << "Couldn't create CRAS client.";
    cras_client_ = NULL;
  }
  if (cras_client_connect(cras_client_) ||
      cras_client_run_thread(cras_client_)) {
    LOG(WARNING) << "Couldn't connect CRAS client, trying again later.";
    g_timeout_add(kCrasRetryConnectMs, ConnectToCrasThunk, this);
  } else {
    connected_to_cras_ = true;
  }

  // TODO(crosbug.com/31927): Send a signal to announce that powerd has started.
  // This is necessary for receiving external display projection status from
  // Chrome, for instance.
}

void Daemon::ReadSettings() {
  int64 enforce_lock;
  int64 low_battery_shutdown_time_s = 0;
  double low_battery_shutdown_percent = 0.0;
  if (!prefs_->GetInt64(kLowBatteryShutdownTimePref,
                        &low_battery_shutdown_time_s)) {
    LOG(INFO) << "No low battery shutdown time threshold perf found";
    low_battery_shutdown_time_s = 0;
  }
  if (!prefs_->GetDouble(kLowBatteryShutdownPercentPref,
                         &low_battery_shutdown_percent)) {
    LOG(INFO) << "No low battery shutdown percent threshold perf found";
    low_battery_shutdown_percent = 0.0;
  }
  CHECK(prefs_->GetInt64(kLowBatteryShutdownTimePref,
                         &low_battery_shutdown_time_s));
  CHECK(prefs_->GetInt64(kSampleWindowMaxPref, &sample_window_max_));
  CHECK(prefs_->GetInt64(kSampleWindowMinPref, &sample_window_min_));
  CHECK(prefs_->GetInt64(kTaperTimeMaxPref, &taper_time_max_s_));
  CHECK(prefs_->GetInt64(kTaperTimeMinPref, &taper_time_min_s_));
  CHECK(prefs_->GetInt64(kCleanShutdownTimeoutMsPref,
                         &clean_shutdown_timeout_ms_));
  CHECK(prefs_->GetInt64(kPluggedDimMsPref, &plugged_dim_ms_));
  CHECK(prefs_->GetInt64(kPluggedOffMsPref, &plugged_off_ms_));
  CHECK(prefs_->GetInt64(kUnpluggedDimMsPref, &unplugged_dim_ms_));
  CHECK(prefs_->GetInt64(kUnpluggedOffMsPref, &unplugged_off_ms_));
  CHECK(prefs_->GetInt64(kReactMsPref, &react_ms_));
  CHECK(prefs_->GetInt64(kFuzzMsPref, &fuzz_ms_));
  CHECK(prefs_->GetInt64(kBatteryPollIntervalPref, &battery_poll_interval_ms_));
  CHECK(prefs_->GetInt64(kBatteryPollShortIntervalPref,
                         &battery_poll_short_interval_ms_));
  CHECK(prefs_->GetInt64(kEnforceLockPref, &enforce_lock));
  CHECK(prefs_->GetBool(kKeepBacklightOnForAudioPref,
                        &keep_backlight_on_for_audio_));

  ReadSuspendSettings();
  ReadLockScreenSettings();
  if ((low_battery_shutdown_time_s >= 0) &&
      (low_battery_shutdown_time_s <= 8 * 3600)) {
    low_battery_shutdown_time_s_ = low_battery_shutdown_time_s;
  } else {
    LOG(INFO) << "Unreasonable low battery shutdown time threshold:"
              << low_battery_shutdown_time_s;
    LOG(INFO) << "Disabling time based low battery shutdown.";
    low_battery_shutdown_time_s_ = 0;
  }
  if ((low_battery_shutdown_percent >= 0.0) &&
      (low_battery_shutdown_percent <= 100.0)) {
    low_battery_shutdown_percent_ = low_battery_shutdown_percent;
  } else {
    LOG(INFO) << "Unreasonable low battery shutdown percent threshold:"
              << low_battery_shutdown_percent;
    LOG(INFO) << "Disabling percent based low battery shutdown.";
    low_battery_shutdown_percent_ = 0.0;
  }

  LOG_IF(WARNING, low_battery_shutdown_percent_ == 0.0 &&
         low_battery_shutdown_time_s_ == 0) << "No low battery thresholds set!";
  // We only want one of the thresholds to be in use
  CHECK(low_battery_shutdown_percent_ == 0.0 ||
        low_battery_shutdown_time_s_ == 0)
      << "Both low battery thresholds set!";
  LOG(INFO) << "Using low battery time threshold of "
            << low_battery_shutdown_time_s_
            << " secs and using low battery percent threshold of "
            << low_battery_shutdown_percent_;

  CHECK(sample_window_max_ > 0);
  CHECK(sample_window_min_ > 0);
  if (sample_window_max_ < sample_window_min_) {
    LOG(WARNING) << "Sampling window minimum was greater then the maximum, "
                 << "swapping!";
    int64 sample_window_temp = sample_window_max_;
    sample_window_max_ = sample_window_min_;
    sample_window_min_ = sample_window_temp;
  }
  LOG(INFO) << "Using Sample Window Max = " << sample_window_max_
            << " and Min = " << sample_window_min_;
  sample_window_diff_ = sample_window_max_ - sample_window_min_;
  CHECK(taper_time_max_s_ > 0);
  CHECK(taper_time_min_s_ > 0);
  if (taper_time_max_s_ < taper_time_min_s_) {
    LOG(WARNING) << "Taper time minimum was greater then the maximum, "
                 << "swapping!";
    int64 taper_time_temp = taper_time_max_s_;
    taper_time_max_s_ = taper_time_min_s_;
    taper_time_min_s_ = taper_time_temp;
  }
  LOG(INFO) << "Using Taper Time Max(secs) = " << taper_time_max_s_
            << " and Min(secs) = " << taper_time_min_s_;
  taper_time_diff_s_ = taper_time_max_s_ - taper_time_min_s_;
  lock_ms_ = default_lock_ms_;
  enforce_lock_ = enforce_lock;

  LOG(INFO) << "Using battery polling interval of " << battery_poll_interval_ms_
            << " mS and short interval of " << battery_poll_short_interval_ms_
            << " mS";

  // Check that timeouts are sane.
  CHECK(kMetricIdleMin >= fuzz_ms_);
  CHECK(plugged_dim_ms_ >= react_ms_);
  CHECK(plugged_off_ms_ >= plugged_dim_ms_ + react_ms_);
  CHECK(plugged_suspend_ms_ >= plugged_off_ms_ + react_ms_);
  CHECK(unplugged_dim_ms_ >= react_ms_);
  CHECK(unplugged_off_ms_ >= unplugged_dim_ms_ + react_ms_);
  CHECK(unplugged_suspend_ms_ >= unplugged_off_ms_ + react_ms_);
  CHECK(default_lock_ms_ >= unplugged_off_ms_ + react_ms_);
  CHECK(default_lock_ms_ >= plugged_off_ms_ + react_ms_);

  // Store unmodified timeout values for switching between projecting and non-
  // projecting timeouts.
  base_timeout_values_[kPluggedDimMsPref]       = plugged_dim_ms_;
  base_timeout_values_[kPluggedOffMsPref]       = plugged_off_ms_;
  base_timeout_values_[kPluggedSuspendMsPref]   = plugged_suspend_ms_;
  base_timeout_values_[kUnpluggedDimMsPref]     = unplugged_dim_ms_;
  base_timeout_values_[kUnpluggedOffMsPref]     = unplugged_off_ms_;
  base_timeout_values_[kUnpluggedSuspendMsPref] = unplugged_suspend_ms_;

  // Initialize from prefs as might be used before AC plug status evaluated.
  dim_ms_ = unplugged_dim_ms_;
  off_ms_ = unplugged_off_ms_;

  state_control_->ReadSettings(prefs_);
}

void Daemon::ReadLockScreenSettings() {
  int64 lock_on_idle_suspend = 0;
  if (prefs_->GetInt64(kLockOnIdleSuspendPref, &lock_on_idle_suspend) &&
      lock_on_idle_suspend) {
    LOG(INFO) << "Enabling screen lock on idle and suspend";
    CHECK(prefs_->GetInt64(kLockMsPref, &default_lock_ms_));
  } else {
    LOG(INFO) << "Disabling screen lock on idle and suspend";
    default_lock_ms_ = INT64_MAX;
  }
  base_timeout_values_[kLockMsPref] = default_lock_ms_;
  lock_on_idle_suspend_ = lock_on_idle_suspend;
}

void Daemon::ReadSuspendSettings() {
  int64 disable_idle_suspend = 0;
  if (prefs_->GetInt64(kDisableIdleSuspendPref, &disable_idle_suspend) &&
      disable_idle_suspend) {
    LOG(INFO) << "Idle suspend feature disabled";
    plugged_suspend_ms_ = INT64_MAX;
    unplugged_suspend_ms_ = INT64_MAX;
  } else {
    CHECK(prefs_->GetInt64(kPluggedSuspendMsPref, &plugged_suspend_ms_));
    CHECK(prefs_->GetInt64(kUnpluggedSuspendMsPref, &unplugged_suspend_ms_));

    LOG(INFO) << "Idle suspend enabled. plugged_suspend_ms_ = "
              << plugged_suspend_ms_ << " unplugged_suspend_ms = "
              << unplugged_suspend_ms_;
    prefs_->GetBool(kRequireUsbInputDeviceToSuspendPref,
                    &require_usb_input_device_to_suspend_);
  }
  // Store unmodified timeout values for switching between projecting and
  // non-projecting timeouts.
  base_timeout_values_[kPluggedSuspendMsPref]   = plugged_suspend_ms_;
  base_timeout_values_[kUnpluggedSuspendMsPref] = unplugged_suspend_ms_;
}

void Daemon::Run() {
  GMainLoop* loop = g_main_loop_new(NULL, false);
  ResumePollPowerSupply();
  g_main_loop_run(loop);
}

void Daemon::UpdateIdleStates() {
  LOG(INFO) << "Daemon : UpdateIdleStates";
  SetIdleState(idle_->GetIdleTimeMs());
}

void Daemon::SetPlugged(bool plugged) {
  if (plugged == plugged_state_)
    return;

  HandleNumOfSessionsPerChargeOnSetPlugged(&metrics_store_,
                                           plugged ?
                                           PLUGGED_STATE_CONNECTED :
                                           PLUGGED_STATE_DISCONNECTED);

  // If we are moving from kPowerUknown then we don't know how long the device
  // has been on AC for and thus our metric would not tell us anything about the
  // battery state when the user decided to charge.
  if (plugged_state_ != PLUGGED_STATE_UNKNOWN) {
    GenerateBatteryInfoWhenChargeStartsMetric(
        plugged ? PLUGGED_STATE_CONNECTED : PLUGGED_STATE_DISCONNECTED,
        power_status_);
  }

  LOG(INFO) << "Daemon : SetPlugged = " << plugged;
  plugged_state_ = plugged ? PLUGGED_STATE_CONNECTED :
      PLUGGED_STATE_DISCONNECTED;
  int64 idle_time_ms = idle_->GetIdleTimeMs();
  // If the screen is on, and the user plugged or unplugged the computer,
  // we should wait a bit before turning off the screen.
  // If the screen is off, don't immediately suspend, wait another
  // suspend timeout.
  // If the state is uninitialized, this is the powerd startup condition, so
  // we ignore any idle time from before powerd starts.
  if (backlight_controller_->GetPowerState() == BACKLIGHT_ACTIVE ||
      backlight_controller_->GetPowerState() == BACKLIGHT_DIM)
    SetIdleOffset(idle_time_ms, IDLE_STATE_NORMAL);
  else if (backlight_controller_->GetPowerState() == BACKLIGHT_IDLE_OFF)
    SetIdleOffset(idle_time_ms, IDLE_STATE_SUSPEND);
  else if (backlight_controller_->GetPowerState() == BACKLIGHT_UNINITIALIZED)
    SetIdleOffset(idle_time_ms, IDLE_STATE_NORMAL);
  else
    SetIdleOffset(0, IDLE_STATE_NORMAL);

  backlight_controller_->OnPlugEvent(plugged);
  SetIdleState(idle_time_ms);
}

void Daemon::OnRequestRestart() {
  if (shutdown_state_ == SHUTDOWN_STATE_NONE) {
    shutdown_state_ = SHUTDOWN_STATE_RESTARTING;
    StartCleanShutdown();
  }
}

void Daemon::OnRequestShutdown() {
  if (shutdown_state_ == SHUTDOWN_STATE_NONE) {
    shutdown_state_ = SHUTDOWN_STATE_POWER_OFF;
    StartCleanShutdown();
  }
}

void Daemon::StartCleanShutdown() {
  clean_shutdown_initiated_ = true;
  // Cancel any outstanding suspend in flight.
  suspender_.CancelSuspend();
  util::SendSignalToPowerM(kRequestCleanShutdown);
  g_timeout_add(clean_shutdown_timeout_ms_, CleanShutdownTimedOutThunk, this);
}

void Daemon::SetIdleOffset(int64 offset_ms, IdleState state) {
  AdjustIdleTimeoutsForProjection();
  int64 prev_dim_ms = dim_ms_;
  int64 prev_off_ms = off_ms_;
  LOG(INFO) << "offset_ms_ = " << offset_ms;
  offset_ms_ = offset_ms;
  if (plugged_state_ == PLUGGED_STATE_CONNECTED) {
    dim_ms_ = plugged_dim_ms_;
    off_ms_ = plugged_off_ms_;
    suspend_ms_ = plugged_suspend_ms_;
  } else {
    CHECK(plugged_state_ == PLUGGED_STATE_DISCONNECTED);
    dim_ms_ = unplugged_dim_ms_;
    off_ms_ = unplugged_off_ms_;
    suspend_ms_ = unplugged_suspend_ms_;
  }
  lock_ms_ = default_lock_ms_;

  // Protect against overflow
  dim_ms_ = std::max(dim_ms_ + offset_ms, dim_ms_);
  off_ms_ = std::max(off_ms_ + offset_ms, off_ms_);
  suspend_ms_ = std::max(suspend_ms_ + offset_ms, suspend_ms_);

  if (enforce_lock_) {
    // Make sure that the screen turns off before it locks, and dims before
    // it turns off. This ensures the user gets a warning before we lock the
    // screen.
    off_ms_ = std::min(off_ms_, lock_ms_ - react_ms_);
    dim_ms_ = std::min(dim_ms_, lock_ms_ - 2 * react_ms_);
  } else {
    lock_ms_ = std::max(lock_ms_ + offset_ms, lock_ms_);
  }

  // Only offset timeouts for states starting with idle state provided.
  switch (state) {
    case IDLE_STATE_SUSPEND:
      off_ms_ = prev_off_ms;
    case IDLE_STATE_SCREEN_OFF:
      dim_ms_ = prev_dim_ms;
    case IDLE_STATE_DIM:
    case IDLE_STATE_NORMAL:
      break;
    case IDLE_STATE_UNKNOWN:
    default: {
      LOG(ERROR) << "SetIdleOffset : Improper Idle State";
      break;
    }
  }

  // Sync up idle state with new settings.
  idle_->ClearTimeouts();
  if (offset_ms > fuzz_ms_)
    idle_->AddIdleTimeout(fuzz_ms_);
  if (kMetricIdleMin <= dim_ms_ - fuzz_ms_)
    idle_->AddIdleTimeout(kMetricIdleMin);
  // XIdle timeout events for dimming and idle-off.
  idle_->AddIdleTimeout(dim_ms_);
  idle_->AddIdleTimeout(off_ms_);
  // This is to start polling audio before a suspend.
  // |suspend_ms_| must be >= |off_ms_| + |react_ms_|, so if the following
  // condition is false, then they must be equal.  In that case, the idle
  // timeout at |off_ms_| would be equivalent, and the following timeout would
  // be redundant.
  if (suspend_ms_ - react_ms_ > off_ms_)
    idle_->AddIdleTimeout(suspend_ms_ - react_ms_);
  // XIdle timeout events for lock and/or suspend.
  if (lock_ms_ < suspend_ms_ - fuzz_ms_ || lock_ms_ - fuzz_ms_ > suspend_ms_) {
    idle_->AddIdleTimeout(lock_ms_);
    idle_->AddIdleTimeout(suspend_ms_);
  } else {
    idle_->AddIdleTimeout(std::max(lock_ms_, suspend_ms_));
  }
  // XIdle timeout events for idle notify status
  for (IdleThresholds::iterator iter = thresholds_.begin();
       iter != thresholds_.end();
       ++iter) {
    if (*iter == 0) {
      idle_->AddIdleTimeout(kMinTimeForIdle);
    } else  if (*iter > 0) {
      idle_->AddIdleTimeout(*iter);
    }
  }
}

// SetActive will transition to Normal state. Used for transitioning on events
// that do not result in activity monitored by chrome, i.e. lid open.
void Daemon::SetActive() {
  idle_->HandleUserActivity(base::TimeTicks::Now());
  int64 idle_time_ms = idle_->GetIdleTimeMs();
  SetIdleOffset(idle_time_ms, IDLE_STATE_NORMAL);
  SetIdleState(idle_time_ms);
}

void Daemon::SetIdleState(int64 idle_time_ms) {
  PowerState old_state = backlight_controller_->GetPowerState();
  if (idle_time_ms >= suspend_ms_ &&
      !state_control_->IsStateDisabled(STATE_CONTROL_IDLE_SUSPEND)) {
    SetPowerState(BACKLIGHT_SUSPENDED);
    Suspend();
  } else if (idle_time_ms >= off_ms_ &&
             !state_control_->IsStateDisabled(STATE_CONTROL_IDLE_BLANK)) {
    if (util::IsSessionStarted())
      SetPowerState(BACKLIGHT_IDLE_OFF);
  } else if (idle_time_ms >= dim_ms_ &&
             !state_control_->IsStateDisabled(STATE_CONTROL_IDLE_DIM)) {
    SetPowerState(BACKLIGHT_DIM);
  } else if (backlight_controller_->GetPowerState() != BACKLIGHT_ACTIVE) {
    if (backlight_controller_->SetPowerState(BACKLIGHT_ACTIVE)) {
      if (backlight_controller_->GetPowerState() == BACKLIGHT_SUSPENDED) {
        util::CreateStatusFile(FilePath(run_dir_).Append(kUserActiveFile));
        suspender_.CancelSuspend();
      }
    }
    if (keyboard_controller_)
      keyboard_controller_->SetPowerState(BACKLIGHT_ACTIVE);
    power_state_ = BACKLIGHT_ACTIVE;
  } else if (idle_time_ms < react_ms_ && locker_.is_locked()) {
    BrightenScreenIfOff();
  }
  if (idle_time_ms >= lock_ms_ && util::IsSessionStarted() &&
      backlight_controller_->GetPowerState() != BACKLIGHT_SUSPENDED) {
    locker_.LockScreen();
  }
  if (old_state != backlight_controller_->GetPowerState())
    idle_transition_timestamps_[backlight_controller_->GetPowerState()] =
        base::TimeTicks::Now();
}

void Daemon::OnPowerEvent(void* object, const PowerStatus& info) {
  Daemon* daemon = static_cast<Daemon*>(object);
  daemon->SetPlugged(info.line_power_on);
  daemon->GenerateMetricsOnPowerEvent(info);
  // Do not emergency suspend if no battery exists.
  if (info.battery_is_present) {
    if (info.battery_percentage < 0) {
      LOG(WARNING) << "Negative battery percent: " << info.battery_percentage
                   << "%";
    }
    if (info.battery_time_to_empty < 0 && !info.line_power_on) {
      LOG(WARNING) << "Negative battery time remaining: "
                   << info.battery_time_to_empty << " seconds";
    }
    daemon->OnLowBattery(info.battery_time_to_empty,
                         info.battery_time_to_full,
                         info.battery_percentage);
  }
}

void Daemon::AddIdleThreshold(int64 threshold) {
  idle_->AddIdleTimeout((threshold == 0) ? kMinTimeForIdle : threshold);
  thresholds_.push_back(threshold);
}

void Daemon::IdleEventNotify(int64 threshold) {
  dbus_int64_t threshold_int =
      static_cast<dbus_int64_t>(threshold);

  chromeos::dbus::Proxy proxy(chromeos::dbus::GetSystemBusConnection(),
                              kPowerManagerServicePath,
                              kPowerManagerInterface);
  DBusMessage* signal = dbus_message_new_signal(
      kPowerManagerServicePath,
      kPowerManagerInterface,
      threshold ? kIdleNotifySignal : kActiveNotifySignal);
  CHECK(signal);
  dbus_message_append_args(signal,
                           DBUS_TYPE_INT64, &threshold_int,
                           DBUS_TYPE_INVALID);
  dbus_g_proxy_send(proxy.gproxy(), signal, NULL);
  dbus_message_unref(signal);
}

void Daemon::BrightenScreenIfOff() {
  if (util::IsSessionStarted() && backlight_controller_->IsBacklightActiveOff())
    backlight_controller_->IncreaseBrightness(BRIGHTNESS_CHANGE_AUTOMATED);
}

void Daemon::OnIdleEvent(bool is_idle, int64 idle_time_ms) {
  CHECK(plugged_state_ != PLUGGED_STATE_UNKNOWN);
  if (is_idle && backlight_controller_->GetPowerState() == BACKLIGHT_ACTIVE &&
      dim_ms_ <= idle_time_ms && !locker_.is_locked()) {
    int64 video_time_ms = 0;
    bool video_is_playing = false;
    int64 dim_timeout = (PLUGGED_STATE_CONNECTED == plugged_state_) ?
        plugged_dim_ms_ :
        unplugged_dim_ms_;
    CHECK(video_detector_->GetActivity(dim_timeout,
                                       &video_time_ms,
                                       &video_is_playing));
    if (video_is_playing)
      SetIdleOffset(idle_time_ms - video_time_ms, IDLE_STATE_NORMAL);
  }
  if (is_idle && backlight_controller_->GetPowerState() == BACKLIGHT_DIM &&
      !util::OOBECompleted()) {
    LOG(INFO) << "OOBE not complete. Delaying screenoff until done.";
    SetIdleOffset(idle_time_ms, IDLE_STATE_SCREEN_OFF);
  }
  if (is_idle && backlight_controller_->GetPowerState() == BACKLIGHT_DIM &&
      keep_backlight_on_for_audio_ && idle_time_ms  >= off_ms_ &&
      IsAudioPlaying()) {
    LOG(INFO) << "Backlight must stay on for audio. Delaying screenoff.";
    SetIdleOffset(idle_time_ms, IDLE_STATE_SCREEN_OFF);
  }
  if (is_idle &&
      backlight_controller_->GetPowerState() != BACKLIGHT_SUSPENDED &&
      idle_time_ms >= suspend_ms_) {
    bool audio_is_playing = IsAudioPlaying();
    bool delay_suspend = false;
    if (audio_is_playing || ShouldStayAwakeForHeadphoneJack()) {
      LOG(INFO) << "Delaying suspend because " <<
          (audio_is_playing ? "audio is playing." : "headphones are attached.");
      delay_suspend = true;
    } else if (require_usb_input_device_to_suspend_ &&
               !USBInputDeviceConnected()) {
      LOG(INFO) << "Delaying suspend because no USB input device is connected.";
      delay_suspend = true;
    }
    if (delay_suspend) {
      // Increase the suspend offset by the react time.  Since the offset is
      // calculated relative to the ORIGINAL [un]plugged_suspend_ms_ value, we
      // need to use that here.
      int64 base_suspend_ms = (plugged_state_ == PLUGGED_STATE_CONNECTED) ?
                               plugged_suspend_ms_ : unplugged_suspend_ms_;
      SetIdleOffset(suspend_ms_ - base_suspend_ms + react_ms_,
                    IDLE_STATE_SUSPEND);
    }
  }

  if (is_idle) {
    last_idle_event_timestamp_ = base::TimeTicks::Now();
    last_idle_timedelta_ = base::TimeDelta::FromMilliseconds(idle_time_ms);
  } else if (!last_idle_event_timestamp_.is_null() &&
             idle_time_ms < last_idle_timedelta_.InMilliseconds()) {
    GenerateMetricsOnIdleEvent(is_idle, idle_time_ms);
  }
  SetIdleState(idle_time_ms);
  if (!is_idle && offset_ms_ != 0)
    SetIdleOffset(0, IDLE_STATE_NORMAL);

  // Notify once for each threshold.
  IdleThresholds::iterator iter = thresholds_.begin();
  while (iter != thresholds_.end()) {
    // If we're idle and past a threshold, notify and erase the threshold.
    if (is_idle && *iter != 0 && idle_time_ms >= *iter) {
      IdleEventNotify(*iter);
      iter = thresholds_.erase(iter);
    // Else, if we just went active and the threshold is a check for active.
    } else if (!is_idle && *iter == 0) {
      IdleEventNotify(0);
      iter = thresholds_.erase(iter);
    } else {
      ++iter;
    }
  }
}

void Daemon::AdjustKeyboardBrightness(int direction) {
  if (!keyboard_controller_)
    return;

  if (direction > 0) {
    keyboard_controller_->IncreaseBrightness(BRIGHTNESS_CHANGE_USER_INITIATED);
  } else if (direction < 0) {
    keyboard_controller_->DecreaseBrightness(true,
                                             BRIGHTNESS_CHANGE_USER_INITIATED);
  }
}

void Daemon::SendBrightnessChangedSignal(double brightness_percent,
                                         BrightnessChangeCause cause,
                                         const std::string& signal_name) {
  dbus_int32_t brightness_percent_int =
      static_cast<dbus_int32_t>(round(brightness_percent));

  dbus_bool_t user_initiated = FALSE;
  switch (cause) {
    case BRIGHTNESS_CHANGE_AUTOMATED:
      user_initiated = FALSE;
      break;
    case BRIGHTNESS_CHANGE_USER_INITIATED:
      user_initiated = TRUE;
      break;
    default:
      NOTREACHED() << "Unhandled brightness change cause " << cause;
  }

  chromeos::dbus::Proxy proxy(chromeos::dbus::GetSystemBusConnection(),
                              kPowerManagerServicePath,
                              kPowerManagerInterface);
  DBusMessage* signal = dbus_message_new_signal(kPowerManagerServicePath,
                                                kPowerManagerInterface,
                                                signal_name.c_str());
  CHECK(signal);
  dbus_message_append_args(signal,
                           DBUS_TYPE_INT32, &brightness_percent_int,
                           DBUS_TYPE_BOOLEAN, &user_initiated,
                           DBUS_TYPE_INVALID);
  dbus_g_proxy_send(proxy.gproxy(), signal, NULL);
  dbus_message_unref(signal);
}

void Daemon::OnBrightnessChanged(double brightness_percent,
                                 BrightnessChangeCause cause,
                                 BacklightController* source) {
  if (source == backlight_controller_) {
    SendBrightnessChangedSignal(brightness_percent, cause,
                                kBrightnessChangedSignal);
  } else if (source == keyboard_controller_ && keyboard_controller_) {
    SendBrightnessChangedSignal(brightness_percent, cause,
                                kKeyboardBrightnessChangedSignal);
  } else {
    NOTREACHED() << "Received a brightness change callback from an unknown "
                 << "backlight controller";
  }
}

void Daemon::HaltPollPowerSupply() {
  if (poll_power_supply_timer_id_ > 0)
    g_source_remove(poll_power_supply_timer_id_);
}

void Daemon::ResumePollPowerSupply() {
  ScheduleShortPollPowerSupply();
  EventPollPowerSupply();
}

void Daemon::MarkPowerStatusStale() {
  is_power_status_stale_ = true;
}

gboolean Daemon::UdevEventHandler(GIOChannel* /* source */,
                                  GIOCondition /* condition */,
                                  gpointer data) {
  Daemon* daemon = static_cast<Daemon*>(data);

  struct udev_device* dev = udev_monitor_receive_device(daemon->udev_monitor_);
  if (dev) {
    LOG(INFO) << "Event on ("
              << udev_device_get_subsystem(dev)
              << ") Action "
              << udev_device_get_action(dev);
    CHECK(std::string(udev_device_get_subsystem(dev)) ==
          kPowerSupplyUdevSubsystem);
    udev_device_unref(dev);

    // Rescheduling the timer to fire 5s from now to make sure that it doesn't
    // get a bogus value from being too close to this event.
    daemon->ResumePollPowerSupply();
  } else {
    LOG(ERROR) << "Can't get receive_device()";
    return FALSE;
  }
  return TRUE;
}

void Daemon::RegisterUdevEventHandler() {
  // Create the udev object.
  udev_ = udev_new();
  if (!udev_)
    LOG(ERROR) << "Can't create udev object.";

  // Create the udev monitor structure.
  udev_monitor_ = udev_monitor_new_from_netlink(udev_, "udev");
  if (!udev_monitor_) {
    LOG(ERROR) << "Can't create udev monitor.";
    udev_unref(udev_);
  }
  udev_monitor_filter_add_match_subsystem_devtype(udev_monitor_,
                                                  kPowerSupplyUdevSubsystem,
                                                  NULL);
  udev_monitor_enable_receiving(udev_monitor_);

  int fd = udev_monitor_get_fd(udev_monitor_);

  GIOChannel* channel = g_io_channel_unix_new(fd);
  g_io_add_watch(channel, G_IO_IN, &(Daemon::UdevEventHandler), this);

  LOG(INFO) << "Udev controller waiting for events on subsystem "
            << kPowerSupplyUdevSubsystem;
}

void Daemon::RegisterDBusMessageHandler() {
  util::RequestDBusServiceName(kPowerManagerServiceName);
  util::SetNameOwnerChangedHandler(&Suspender::NameOwnerChangedHandler,
                                   &suspender_);

  dbus_handler_.AddDBusSignalHandler(
      kPowerManagerInterface,
      kRequestSuspendSignal,
      base::Bind(&Daemon::HandleRequestSuspendSignal, base::Unretained(this)));
  dbus_handler_.AddDBusSignalHandler(
      kPowerManagerInterface,
      kInputEventSignal,
      base::Bind(&Daemon::HandleInputEventSignal, base::Unretained(this)));
  dbus_handler_.AddDBusSignalHandler(
      kPowerManagerInterface,
      kCleanShutdown,
      base::Bind(&Daemon::HandleCleanShutdownSignal, base::Unretained(this)));
  dbus_handler_.AddDBusSignalHandler(
      kPowerManagerInterface,
      kPowerStateChangedSignal,
      base::Bind(&Daemon::HandlePowerStateChangedSignal,
                 base::Unretained(this)));
  dbus_handler_.AddDBusSignalHandler(
      kPowerManagerInterface,
      kSuspendReady,
      base::Bind(&Suspender::SuspendReady,
                 base::Unretained(&suspender_)));
  dbus_handler_.AddDBusSignalHandler(
      login_manager::kSessionManagerInterface,
      login_manager::kSessionManagerSessionStateChanged,
      base::Bind(&Daemon::HandleSessionManagerSessionStateChangedSignal,
                 base::Unretained(this)));
  dbus_handler_.AddDBusSignalHandler(
      login_manager::kSessionManagerInterface,
      login_manager::kScreenIsLockedSignal,
      base::Bind(&Daemon::HandleSessionManagerScreenIsLockedSignal,
                 base::Unretained(this)));
  dbus_handler_.AddDBusSignalHandler(
      login_manager::kSessionManagerInterface,
      login_manager::kScreenIsUnlockedSignal,
      base::Bind(&Daemon::HandleSessionManagerScreenIsUnlockedSignal,
                 base::Unretained(this)));

  dbus_handler_.AddDBusMethodHandler(
      kPowerManagerInterface,
      kRequestShutdownMethod,
      base::Bind(&Daemon::HandleRequestShutdownMethod, base::Unretained(this)));
  dbus_handler_.AddDBusMethodHandler(
      kPowerManagerInterface,
      kRequestRestartMethod,
      base::Bind(&Daemon::HandleRequestRestartMethod, base::Unretained(this)));
  dbus_handler_.AddDBusMethodHandler(
      kPowerManagerInterface,
      kDecreaseScreenBrightness,
      base::Bind(&Daemon::HandleDecreaseScreenBrightnessMethod,
                 base::Unretained(this)));
  dbus_handler_.AddDBusMethodHandler(
      kPowerManagerInterface,
      kIncreaseScreenBrightness,
      base::Bind(&Daemon::HandleIncreaseScreenBrightnessMethod,
                 base::Unretained(this)));
  dbus_handler_.AddDBusMethodHandler(
      kPowerManagerInterface,
      kGetScreenBrightnessPercent,
      base::Bind(&Daemon::HandleGetScreenBrightnessMethod,
                 base::Unretained(this)));
  dbus_handler_.AddDBusMethodHandler(
      kPowerManagerInterface,
      kSetScreenBrightnessPercent,
      base::Bind(&Daemon::HandleSetScreenBrightnessMethod,
                 base::Unretained(this)));
  dbus_handler_.AddDBusMethodHandler(
      kPowerManagerInterface,
      kDecreaseKeyboardBrightness,
      base::Bind(&Daemon::HandleDecreaseKeyboardBrightnessMethod,
                 base::Unretained(this)));
  dbus_handler_.AddDBusMethodHandler(
      kPowerManagerInterface,
      kIncreaseKeyboardBrightness,
      base::Bind(&Daemon::HandleIncreaseKeyboardBrightnessMethod,
                 base::Unretained(this)));
  dbus_handler_.AddDBusMethodHandler(
      kPowerManagerInterface,
      kGetIdleTime,
      base::Bind(&Daemon::HandleGetIdleTimeMethod, base::Unretained(this)));
  dbus_handler_.AddDBusMethodHandler(
      kPowerManagerInterface,
      kRequestIdleNotification,
      base::Bind(&Daemon::HandleRequestIdleNotificationMethod,
                 base::Unretained(this)));
  dbus_handler_.AddDBusMethodHandler(
      kPowerManagerInterface,
      kGetPowerSupplyPropertiesMethod,
      base::Bind(&Daemon::HandleGetPowerSupplyPropertiesMethod,
                 base::Unretained(this)));
  dbus_handler_.AddDBusMethodHandler(
      kPowerManagerInterface,
      kStateOverrideRequest,
      base::Bind(&Daemon::HandleStateOverrideRequestMethod,
                 base::Unretained(this)));
  dbus_handler_.AddDBusMethodHandler(
      kPowerManagerInterface,
      kStateOverrideCancel,
      base::Bind(&Daemon::HandleStateOverrideCancelMethod,
                 base::Unretained(this)));
  dbus_handler_.AddDBusMethodHandler(
      kPowerManagerInterface,
      kHandleVideoActivityMethod,
      base::Bind(&Daemon::HandleVideoActivityMethod, base::Unretained(this)));
  dbus_handler_.AddDBusMethodHandler(
      kPowerManagerInterface,
      kHandleUserActivityMethod,
      base::Bind(&Daemon::HandleUserActivityMethod, base::Unretained(this)));
  dbus_handler_.AddDBusMethodHandler(
      kPowerManagerInterface,
      kSetIsProjectingMethod,
      base::Bind(&Daemon::HandleSetIsProjectingMethod, base::Unretained(this)));
  dbus_handler_.AddDBusMethodHandler(
      kPowerManagerInterface,
      kRegisterSuspendDelay,
      base::Bind(&Suspender::RegisterSuspendDelay,
                 base::Unretained(&suspender_)));
  dbus_handler_.AddDBusMethodHandler(
      kPowerManagerInterface,
      kUnregisterSuspendDelay,
      base::Bind(&Suspender::UnregisterSuspendDelay,
                 base::Unretained(&suspender_)));

  dbus_handler_.Start();
}

bool Daemon::HandleRequestSuspendSignal(DBusMessage*) {  // NOLINT
  Suspend();
  return true;
}

bool Daemon::HandleInputEventSignal(DBusMessage* message) {
  DBusError error;
  dbus_error_init(&error);
  dbus_int32_t type_param = 0;
  dbus_bool_t down = FALSE;
  dbus_int64_t timestamp_internal = 0;
  if (!dbus_message_get_args(message, &error,
                             DBUS_TYPE_INT32, &type_param,
                             DBUS_TYPE_BOOLEAN, &down,
                             DBUS_TYPE_INT64, &timestamp_internal,
                             DBUS_TYPE_INVALID)) {
    LOG(ERROR) << "Unable to process input event: "
               << error.name << " (" << error.message << ")";
    dbus_error_free(&error);
    return true;
  }
  InputType type = static_cast<InputType>(type_param);
  base::TimeTicks timestamp =
      base::TimeTicks::FromInternalValue(timestamp_internal);

  switch (type) {
    case INPUT_LID:
      if (down) {
        SetActive();
        Suspend();
      } else {
        SetActive();
        suspender_.CancelSuspend();
      }
      break;
    case INPUT_POWER_BUTTON:
      OnPowerButtonEvent(down, timestamp);
      break;
    default:
      LOG(ERROR) << "Unhandled input event of type " << type;
  }
  return true;
}

bool Daemon::HandleCleanShutdownSignal(DBusMessage*) {  // NOLINT
  if (clean_shutdown_initiated_) {
    clean_shutdown_initiated_ = false;
    Shutdown();
  } else {
    LOG(WARNING) << "Unrequested " << kCleanShutdown << " signal";
  }
  return true;
}

bool Daemon::HandlePowerStateChangedSignal(DBusMessage* message) {
  const char* state = '\0';
  int32 power_rc = -1;
  DBusError error;
  dbus_error_init(&error);
  if (dbus_message_get_args(message, &error,
                            DBUS_TYPE_STRING, &state,
                            DBUS_TYPE_INT32, &power_rc,
                            DBUS_TYPE_INVALID)) {
    OnPowerStateChange(state);
  } else {
    LOG(WARNING) << "Unable to read " << kPowerStateChanged << " args";
    dbus_error_free(&error);
  }
  return false;
}

bool Daemon::HandleSessionManagerSessionStateChangedSignal(
    DBusMessage* message) {
  DBusError error;
  dbus_error_init(&error);
  const char* state = NULL;
  const char* user = NULL;
  if (dbus_message_get_args(message, &error,
                            DBUS_TYPE_STRING, &state,
                            DBUS_TYPE_STRING, &user,
                            DBUS_TYPE_INVALID)) {
    OnSessionStateChange(state, user);
  } else {
    LOG(WARNING) << "Unable to read "
                 << login_manager::kSessionManagerSessionStateChanged
                 << " args";
    dbus_error_free(&error);
  }
  return false;
}

bool Daemon::HandleSessionManagerScreenIsLockedSignal(
    DBusMessage* message) {
  LOG(INFO) << "HandleSessionManagerScreenIsLockedSignal";
  locker_.set_locked(true);
  suspender_.CheckSuspend();
  return true;
}

bool Daemon::HandleSessionManagerScreenIsUnlockedSignal(
    DBusMessage* message) {
  LOG(INFO) << "HandleSessionManagerScreenIsUnlockedSignal";
  locker_.set_locked(false);
  return true;
}

DBusMessage* Daemon::HandleRequestShutdownMethod(DBusMessage* message) {
  shutdown_reason_ = kShutdownReasonUserRequest;
  OnRequestShutdown();
  return NULL;
}

DBusMessage* Daemon::HandleRequestRestartMethod(DBusMessage* message) {
  OnRequestRestart();
  return NULL;
}

DBusMessage* Daemon::HandleDecreaseScreenBrightnessMethod(
    DBusMessage* message) {
  dbus_bool_t allow_off = false;
  DBusError error;
  dbus_error_init(&error);
  if (dbus_message_get_args(message, &error,
                            DBUS_TYPE_BOOLEAN, &allow_off,
                            DBUS_TYPE_INVALID) == FALSE) {
    LOG(WARNING) << "Unable to read " << kDecreaseScreenBrightness << " args";
    dbus_error_free(&error);
  }
  bool changed = backlight_controller_->DecreaseBrightness(
      allow_off, BRIGHTNESS_CHANGE_USER_INITIATED);
  SendEnumMetricWithPowerState(kMetricBrightnessAdjust,
                               BRIGHTNESS_ADJUST_DOWN,
                               BRIGHTNESS_ADJUST_MAX);
  if (!changed) {
    SendBrightnessChangedSignal(
        backlight_controller_->GetTargetBrightnessPercent(),
        BRIGHTNESS_CHANGE_USER_INITIATED,
        kBrightnessChangedSignal);
  }
  return NULL;
}

DBusMessage* Daemon::HandleIncreaseScreenBrightnessMethod(
    DBusMessage* message) {
  bool changed = backlight_controller_->IncreaseBrightness(
      BRIGHTNESS_CHANGE_USER_INITIATED);
  SendEnumMetricWithPowerState(kMetricBrightnessAdjust,
                               BRIGHTNESS_ADJUST_UP,
                               BRIGHTNESS_ADJUST_MAX);
  if (!changed) {
    SendBrightnessChangedSignal(
        backlight_controller_->GetTargetBrightnessPercent(),
        BRIGHTNESS_CHANGE_USER_INITIATED,
        kBrightnessChangedSignal);
  }
  return NULL;
}

DBusMessage* Daemon::HandleSetScreenBrightnessMethod(DBusMessage* message) {
  double percent;
  int dbus_style;
  DBusError error;
  dbus_error_init(&error);
  if (!dbus_message_get_args(message, &error,
                             DBUS_TYPE_DOUBLE, &percent,
                             DBUS_TYPE_INT32, &dbus_style,
                             DBUS_TYPE_INVALID)) {
    LOG(WARNING) << kSetScreenBrightnessPercent
                << ": Error reading args: " << error.message;
    dbus_error_free(&error);
    return util::CreateDBusErrorReply(message, DBUS_ERROR_INVALID_ARGS,
                                      "Invalid arguments passed to method");
  }
  TransitionStyle style = TRANSITION_FAST;
  switch (dbus_style) {
    case kBrightnessTransitionGradual:
      style = TRANSITION_FAST;
      break;
    case kBrightnessTransitionInstant:
      style = TRANSITION_INSTANT;
      break;
    default:
      LOG(WARNING) << "Invalid transition style passed ( " << dbus_style
                  << " ).  Using default fast transition";
  }
  backlight_controller_->SetCurrentBrightnessPercent(
      percent, BRIGHTNESS_CHANGE_USER_INITIATED, style);
  SendEnumMetricWithPowerState(kMetricBrightnessAdjust,
                               BRIGHTNESS_ADJUST_ABSOLUTE,
                               BRIGHTNESS_ADJUST_MAX);
  return NULL;
}

DBusMessage* Daemon::HandleGetScreenBrightnessMethod(DBusMessage* message) {
  double percent;
  if (!backlight_controller_->GetCurrentBrightnessPercent(&percent)) {
    return util::CreateDBusErrorReply(message, DBUS_ERROR_FAILED,
                                      "Could not fetch Screen Brightness");
  }
  DBusMessage* reply = util::CreateEmptyDBusReply(message);
  CHECK(reply);
  dbus_message_append_args(reply,
                           DBUS_TYPE_DOUBLE, &percent,
                           DBUS_TYPE_INVALID);
  return reply;
}

DBusMessage* Daemon::HandleDecreaseKeyboardBrightnessMethod(
    DBusMessage* message) {
  AdjustKeyboardBrightness(-1);
  // TODO(dianders): metric?
  return NULL;
}

DBusMessage* Daemon::HandleIncreaseKeyboardBrightnessMethod(
    DBusMessage* message) {
  AdjustKeyboardBrightness(1);
  // TODO(dianders): metric?
  return NULL;
}

DBusMessage* Daemon::HandleGetIdleTimeMethod(DBusMessage* message) {
  int64 idle_time_ms = idle_->GetIdleTimeMs();
  DBusMessage* reply = util::CreateEmptyDBusReply(message);
  CHECK(reply);
  dbus_message_append_args(reply,
                           DBUS_TYPE_INT64, &idle_time_ms,
                           DBUS_TYPE_INVALID);
  return reply;
}

DBusMessage* Daemon::HandleRequestIdleNotificationMethod(DBusMessage* message) {
  DBusError error;
  dbus_error_init(&error);
  int64 threshold = 0;
  if (dbus_message_get_args(message, &error,
                            DBUS_TYPE_INT64, &threshold,
                            DBUS_TYPE_INVALID)) {
    AddIdleThreshold(threshold);
  } else {
    LOG(WARNING) << "Unable to read " << kRequestIdleNotification << " args";
    dbus_error_free(&error);
  }
  return NULL;
}

DBusMessage* Daemon::HandleGetPowerSupplyPropertiesMethod(
    DBusMessage* message) {
  if (is_power_status_stale_) {
    // Poll the power supply for status, but don't clear the stale bit. This
    // case is an exceptional one, so we can't guarantee we want to start
    // polling again yet from this context. The stale bit should only be set
    // near the beginning of a session or around Suspend/Resume, so we are
    // assuming that the battery time is untrustworthy, hence the
    // |is_calculating| is true.
    power_supply_.GetPowerStatus(&power_status_, true);
    HandlePollPowerSupply();
    is_power_status_stale_ = true;
  }

  PowerSupplyProperties protobuf;
  PowerStatus* status = &power_status_;

  protobuf.set_line_power_on(status->line_power_on);
  protobuf.set_battery_energy(status->battery_energy);
  protobuf.set_battery_energy_rate(status->battery_energy_rate);
  protobuf.set_battery_voltage(status->battery_voltage);
  protobuf.set_battery_time_to_empty(status->battery_time_to_empty);
  protobuf.set_battery_time_to_full(status->battery_time_to_full);
  UpdateBatteryReportState();
  protobuf.set_battery_percentage(GetDisplayBatteryPercent());
  protobuf.set_battery_is_present(status->battery_is_present);
  protobuf.set_battery_is_charged(status->battery_state ==
                                  BATTERY_STATE_FULLY_CHARGED);
  protobuf.set_is_calculating_battery_time(status->is_calculating_battery_time);
  protobuf.set_averaged_battery_time_to_empty(
      status->averaged_battery_time_to_empty);
  protobuf.set_averaged_battery_time_to_full(
      status->averaged_battery_time_to_full);

  DBusMessage* reply = util::CreateEmptyDBusReply(message);
  CHECK(reply);
  std::string serialized_proto;
  CHECK(protobuf.SerializeToString(&serialized_proto));
  // For array arguments, D-Bus wants the array typecode, the element
  // typecode, the array address, and the number of elements (as opposed to
  // the usual typecode-followed-by-address ordering).
  const char* serial_data = serialized_proto.data();
  dbus_message_append_args(reply,
                           DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE,
                           &serial_data, serialized_proto.size(),
                           DBUS_TYPE_INVALID);
  return reply;
}

DBusMessage* Daemon::HandleStateOverrideRequestMethod(DBusMessage* message) {
  DBusError error;
  dbus_error_init(&error);
  char* data;
  int size;
  if (dbus_message_get_args(message, &error,
                            DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE,
                            &data, &size,
                            DBUS_TYPE_INVALID)) {
    int return_value;
    bool success = state_control_->StateOverrideRequest(
         data, size, &return_value);
    if (success) {
      DBusMessage* reply = util::CreateEmptyDBusReply(message);
      CHECK(reply);
      dbus_message_append_args(reply,
                               DBUS_TYPE_INT32, &return_value,
                               DBUS_TYPE_INVALID);
      return reply;
    }
    return util::CreateDBusErrorReply(message, DBUS_ERROR_FAILED,
                                      "Failed processing request");
  }
  LOG(WARNING) << kStateOverrideRequest << ": Error reading args: " <<
    error.message;
  return util::CreateDBusErrorReply(message, DBUS_ERROR_INVALID_ARGS,
                                    "Invalid arguments passed to method");
}

DBusMessage* Daemon::HandleStateOverrideCancelMethod(DBusMessage* message) {
  DBusError error;
  dbus_error_init(&error);
  int request_id;
  if (dbus_message_get_args(message, &error,
                            DBUS_TYPE_INT32, &request_id,
                            DBUS_TYPE_INVALID)) {
    state_control_->RemoveOverrideAndUpdate(request_id);
  } else {
    LOG(WARNING) << kStateOverrideCancel << ": Error reading args: "
                 << error.message;
    dbus_error_free(&error);
    return util::CreateDBusErrorReply(message, DBUS_ERROR_INVALID_ARGS,
                                      "Invalid arguments passed to method");
  }
  return NULL;
}

DBusMessage* Daemon::HandleVideoActivityMethod(DBusMessage* message) {
  DBusMessage* ret_val = NULL;
  DBusError error;
  dbus_error_init(&error);

  char* serialized_buf = NULL;
  int buf_size = 0;
  VideoActivityUpdate protobuf;
  if (dbus_message_get_args(message, &error,
                             DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE,
                             &serialized_buf, &buf_size,
                             DBUS_TYPE_INVALID)) {
    if (serialized_buf == NULL) {
      LOG(ERROR) << "Received array is NULL!";
      return NULL;
    }
    if (!protobuf.ParseFromArray(serialized_buf, buf_size)) {
      LOG(ERROR) << "Failed to parse protocol buffer from array";
      return ret_val;
    }
    video_detector_->HandleFullscreenChange(protobuf.is_fullscreen());
    video_detector_->HandleActivity(
        base::TimeTicks::FromInternalValue(protobuf.last_activity_time()));
  } else {
    LOG(WARNING) << kHandleVideoActivityMethod
                 << ": Error reading args: " << error.message;
    dbus_error_free(&error);
    ret_val = util::CreateDBusErrorReply(message, DBUS_ERROR_INVALID_ARGS,
                                         "Invalid arguments passed to method");
  }
  return ret_val;
}

DBusMessage* Daemon::HandleUserActivityMethod(DBusMessage* message) {
  DBusError error;
  dbus_error_init(&error);
  int64 last_activity_time_internal = 0;
  if (!dbus_message_get_args(message, &error,
                             DBUS_TYPE_INT64, &last_activity_time_internal,
                             DBUS_TYPE_INVALID)) {
    LOG(WARNING) << kHandleUserActivityMethod
                << ": Error reading args: " << error.message;
    dbus_error_free(&error);
    return util::CreateDBusErrorReply(message, DBUS_ERROR_INVALID_ARGS,
                                      "Invalid arguments passed to method");
  }
  idle_->HandleUserActivity(
      base::TimeTicks::FromInternalValue(last_activity_time_internal));
  return NULL;
}

DBusMessage* Daemon::HandleSetIsProjectingMethod(DBusMessage* message) {
  DBusMessage* reply = NULL;
  DBusError error;
  dbus_error_init(&error);
  bool is_projecting = false;
  dbus_bool_t args_ok =
      dbus_message_get_args(message, &error,
                            DBUS_TYPE_BOOLEAN, &is_projecting,
                            DBUS_TYPE_INVALID);
  if (args_ok) {
    if (is_projecting != is_projecting_) {
      is_projecting_ = is_projecting;
      AdjustIdleTimeoutsForProjection();
    }
  } else {
    // The message was malformed so log this and return an error.
    LOG(WARNING) << kSetIsProjectingMethod << ": Error reading args: "
                 << error.message;
    reply = util::CreateDBusErrorReply(message, DBUS_ERROR_INVALID_ARGS,
                                       "Invalid arguments passed to method");
  }
  return reply;
}

void Daemon::ScheduleShortPollPowerSupply() {
  HaltPollPowerSupply();
  poll_power_supply_timer_id_ = g_timeout_add(battery_poll_short_interval_ms_,
                                              ShortPollPowerSupplyThunk,
                                              this);
}

void Daemon::SchedulePollPowerSupply() {
  HaltPollPowerSupply();
  poll_power_supply_timer_id_ = g_timeout_add(battery_poll_interval_ms_,
                                              PollPowerSupplyThunk,
                                              this);
}

gboolean Daemon::EventPollPowerSupply() {
  power_supply_.GetPowerStatus(&power_status_, true);
  return HandlePollPowerSupply();
}

gboolean Daemon::ShortPollPowerSupply() {
  SchedulePollPowerSupply();
  power_supply_.GetPowerStatus(&power_status_, false);
  HandlePollPowerSupply();
  return false;
}

gboolean Daemon::PollPowerSupply() {
  power_supply_.GetPowerStatus(&power_status_, false);
  return HandlePollPowerSupply();
}

gboolean Daemon::HandlePollPowerSupply() {
  OnPowerEvent(this, power_status_);
  if (!UpdateAveragedTimes(&time_to_empty_average_,
                           &time_to_full_average_)) {
    LOG(ERROR) << "Unable to get averaged times!";
    ScheduleShortPollPowerSupply();
    return FALSE;
  }

  // Send a signal once the power supply status has been obtained.
  DBusMessage* message = dbus_message_new_signal(kPowerManagerServicePath,
                                                 kPowerManagerInterface,
                                                 kPowerSupplyPollSignal);
  CHECK(message != NULL);
  DBusConnection* connection = dbus_g_connection_get_connection(
      chromeos::dbus::GetSystemBusConnection().g_connection());
  if (!dbus_connection_send(connection, message, NULL))
    LOG(WARNING) << "Sending battery poll signal failed.";
  dbus_message_unref(message);
  is_power_status_stale_ = false;
  // Always repeat polling.
  return TRUE;
}

bool Daemon::UpdateAveragedTimes(RollingAverage* empty_average,
                                 RollingAverage* full_average) {
  SendEnumMetric(kMetricBatteryInfoSampleName,
                 BATTERY_INFO_READ,
                 BATTERY_INFO_MAX);
  // Some devices give us bogus values for battery information right after boot
  // or a power event. We attempt to avoid to do sampling at these times, but
  // this guard is to save us when we do sample a bad value. After working out
  // the time values, if we have a negative we know something is bad. If the
  // time we are interested in (to empty or full) is beyond a day then we have a
  // bad value since it is too high. For some devices the value for the
  // uninteresting time, that we are not using, might be bizarre, so we cannot
  // just check both times for overly high values.
  if (power_status_.battery_time_to_empty < 0 ||
      power_status_.battery_time_to_full < 0 ||
      (power_status_.battery_time_to_empty > kBatteryTimeMaxValidSec &&
       !power_status_.line_power_on) ||
      (power_status_.battery_time_to_full > kBatteryTimeMaxValidSec &&
       power_status_.line_power_on)) {
    LOG(ERROR) << "Invalid raw times, time to empty = "
               << power_status_.battery_time_to_empty << ", and time to full = "
               << power_status_.battery_time_to_full;
    power_status_.averaged_battery_time_to_empty = 0;
    power_status_.averaged_battery_time_to_full = 0;
    power_status_.is_calculating_battery_time = true;
    SendEnumMetric(kMetricBatteryInfoSampleName,
                   BATTERY_INFO_BAD,
                   BATTERY_INFO_MAX);
    return false;
  }
  SendEnumMetric(kMetricBatteryInfoSampleName,
                 BATTERY_INFO_GOOD,
                 BATTERY_INFO_MAX);

  int64 battery_time = 0;
  if (power_status_.line_power_on) {
    battery_time = power_status_.battery_time_to_full;
    if (!power_status_.is_calculating_battery_time)
      full_average->AddSample(battery_time);
    empty_average->Clear();
  } else {
    // If the time threshold is set use it, otherwise determine the time
    // equivalent of the percentage threshold
    int64 time_threshold_s = low_battery_shutdown_time_s_ ?
        low_battery_shutdown_time_s_ :
        power_status_.battery_time_to_empty
        * (low_battery_shutdown_percent_
           / power_status_.battery_percentage);
    battery_time = power_status_.battery_time_to_empty - time_threshold_s;
    LOG_IF(WARNING, battery_time < 0)
        << "Calculated invalid negative time to empty value, trimming to 0!";
    battery_time = std::max(static_cast<int64>(0), battery_time);
    if (!power_status_.is_calculating_battery_time)
      empty_average->AddSample(battery_time);
    full_average->Clear();
  }

  if (!power_status_.is_calculating_battery_time) {
    if (!power_status_.line_power_on)
      AdjustWindowSize(battery_time,
                       empty_average,
                       full_average);
    else
      empty_average->ChangeWindowSize(sample_window_max_);
  }
  power_status_.averaged_battery_time_to_full =
      full_average->GetAverage();
  power_status_.averaged_battery_time_to_empty =
      empty_average->GetAverage();
  return true;
}

// For the rolling averages we want the window size to taper off in a linear
// fashion from |sample_window_max| to |sample_window_min| on the battery time
// remaining interval from |taper_time_max_s_| to |taper_time_min_s_|. The two
// point equation for the line is:
//   (x - x0)/(x1 - x0) = (t - t0)/(t1 - t0)
// which solved for x is:
//   x = (t - t0)*(x1 - x0)/(t1 - t0) + x0
// We let x be the size of the window and t be the battery time
// remaining.
void Daemon::AdjustWindowSize(int64 battery_time,
                              RollingAverage* empty_average,
                              RollingAverage* full_average) {
  unsigned int window_size = sample_window_max_;
  if (battery_time >= taper_time_max_s_) {
    window_size = sample_window_max_;
  } else if (battery_time <= taper_time_min_s_) {
    window_size = sample_window_min_;
  } else {
    window_size = (battery_time - taper_time_min_s_);
    window_size *= sample_window_diff_;
    window_size /= taper_time_diff_s_;
    window_size += sample_window_min_;
  }
  empty_average->ChangeWindowSize(window_size);
}

void Daemon::OnLowBattery(int64 time_remaining_s,
                          int64 time_full_s,
                          double battery_percentage) {
  if (!low_battery_shutdown_time_s_ && !low_battery_shutdown_percent_) {
    LOG(INFO) << "Battery time remaining : "
              << time_remaining_s << " seconds";
    low_battery_ = false;
    return;
  }
  if (PLUGGED_STATE_DISCONNECTED == plugged_state_ && !low_battery_ &&
      ((time_remaining_s <= low_battery_shutdown_time_s_ &&
        time_remaining_s > 0) ||
       (battery_percentage <= low_battery_shutdown_percent_ &&
        battery_percentage >= 0))) {
    // Shut the system down when low battery condition is encountered.
    LOG(INFO) << "Time remaining: " << time_remaining_s << " seconds.";
    LOG(INFO) << "Percent remaining: " << battery_percentage << "%.";
    LOG(INFO) << "Low battery condition detected. Shutting down immediately.";
    low_battery_ = true;
    file_tagger_.HandleLowBatteryEvent();
    shutdown_reason_ = kShutdownReasonLowBattery;
    OnRequestShutdown();
  } else if (time_remaining_s < 0) {
    LOG(INFO) << "Battery is at " << time_remaining_s << " seconds remaining, "
              << "may not be fully initialized yet.";
  } else if (PLUGGED_STATE_CONNECTED == plugged_state_ ||
             time_remaining_s > low_battery_shutdown_time_s_) {
    if (PLUGGED_STATE_CONNECTED == plugged_state_) {
      LOG(INFO) << "Battery condition is safe (" << battery_percentage
                << "%).  AC is plugged.  "
                << time_full_s << " seconds to full charge.";
    } else {
      LOG(INFO) << "Battery condition is safe (" << battery_percentage
                << "%).  AC is unplugged.  "
                << time_remaining_s << " seconds remaining.";
    }
    low_battery_ = false;
    file_tagger_.HandleSafeBatteryEvent();
  } else if (time_remaining_s == 0) {
    LOG(INFO) << "Battery is at 0 seconds remaining, either we are charging or "
              << "not fully initialized yet.";
  } else {
    // Either a spurious reading after we have requested suspend, or the user
    // has woken the system up intentionally without rectifying the battery
    // situation (ie. user woke the system without attaching AC.)
    // User is on his own from here until the system dies. We will not try to
    // resuspend.
    LOG(INFO) << "Spurious low battery condition, or user living on the edge.";
    file_tagger_.HandleLowBatteryEvent();
  }
}

gboolean Daemon::CleanShutdownTimedOut() {
  if (clean_shutdown_initiated_) {
    clean_shutdown_initiated_ = false;
    LOG(INFO) << "Timed out waiting for clean shutdown/restart.";
    Shutdown();
  } else {
    LOG(INFO) << "Shutdown already handled. clean_shutdown_initiated_ == false";
  }
  return FALSE;
}

void Daemon::OnPowerStateChange(const char* state) {
  // on == resume via powerd_suspend
  if (g_str_equal(state, "on") == TRUE) {
    LOG(INFO) << "Resuming has commenced";
    HandleResume();
    SetActive();
  } else {
    DLOG(INFO) << "Saw arg:" << state << " for PowerStateChange";
  }
}

void Daemon::OnSessionStateChange(const char* state, const char* user) {
  if (!state || !user) {
    LOG(DFATAL) << "Got session state change with missing state or user";
    return;
  }

  std::string state_string(state);

  if (kValidStates.find(state_string) == kValidStates.end()) {
    LOG(WARNING) << "Changing to unknown session state: " << state;
    return;
  }

  if (state_string == "started") {
    // We always want to take action even if we were already "started", since we
    // want to record when the current session started.  If this warning is
    // appearing it means either we are querying the state of Session Manager
    // when already know it to be "started" or we missed a "stopped"
    // signal. Both of these cases are bad and should be investigated.
    LOG_IF(WARNING, (current_session_state_ == state))
        << "Received message saying session started, when we were already in "
        << "the started state!";

    LOG_IF(ERROR,
           (!GenerateBatteryRemainingAtStartOfSessionMetric(power_status_)))
        << "Start Started: Unable to generate battery remaining metric!";

    if (plugged_state_ == PLUGGED_STATE_DISCONNECTED)
      metrics_store_.IncrementNumOfSessionsPerChargeMetric();

    current_user_ = user;
    session_start_ = base::TimeTicks::Now();

    // Sending up the PowerSupply information, so that the display gets it as
    // soon as possible
    ResumePollPowerSupply();
    DLOG(INFO) << "Session started for "
               << (current_user_.empty() ? "guest" : "non-guest user");
  } else if (current_session_state_ != state) {
    DLOG(INFO) << "Session " << state;
    // For states other then "started" we only want to take action if we have
    // actually changed state, since the code we are calling assumes that we are
    // actually transitioning between states.
    current_user_.clear();
    if (current_session_state_ == "stopped")
      GenerateEndOfSessionMetrics(power_status_,
                                  *backlight_controller_,
                                  base::TimeTicks::Now(),
                                  session_start_);
  }
  current_session_state_ = state;
}

void Daemon::OnPowerButtonEvent(bool down, const base::TimeTicks& timestamp) {
  SendButtonEventSignal(kPowerButtonName, down, timestamp);

  // If the user manually set the brightness to 0, increase it a bit:
  // http://crosbug.com/32570
  if (backlight_controller_->IsBacklightActiveOff()) {
    backlight_controller_->IncreaseBrightness(
        BRIGHTNESS_CHANGE_USER_INITIATED);
  }

  SendPowerButtonMetric(down, timestamp);
  if (down) {
    LOG(INFO) << "Syncing state due to power button down event";
    util::Launch("sync");
  }
}

void Daemon::SendButtonEventSignal(const std::string& button_name,
                                   bool down,
                                   base::TimeTicks timestamp) {
  if (disable_dbus_for_testing_)
    return;

  const gchar* button_name_param = button_name.c_str();
  dbus_bool_t down_param = down;
  dbus_int64_t timestamp_param = timestamp.ToInternalValue();
  chromeos::dbus::Proxy proxy(chromeos::dbus::GetSystemBusConnection(),
                              kPowerManagerServicePath,
                              kPowerManagerInterface);
  DBusMessage* signal = dbus_message_new_signal(kPowerManagerServicePath,
                                                kPowerManagerInterface,
                                                kButtonEventSignal);
  dbus_message_append_args(signal,
                           DBUS_TYPE_STRING, &button_name_param,
                           DBUS_TYPE_BOOLEAN, &down_param,
                           DBUS_TYPE_INT64, &timestamp_param,
                           DBUS_TYPE_INVALID);
  dbus_g_proxy_send(proxy.gproxy(), signal, NULL);
  dbus_message_unref(signal);
}

void Daemon::SendPowerButtonMetric(bool down,
                                   const base::TimeTicks& timestamp) {
  if (down) {
    if (!last_power_button_down_timestamp_.is_null())
      LOG(ERROR) << "Got power-button-down event while button was already down";
    last_power_button_down_timestamp_ = timestamp;
  } else {
    if (last_power_button_down_timestamp_.is_null()) {
      LOG(ERROR) << "Got power-button-up event while button was already up";
      return;
    }
    base::TimeDelta delta = timestamp - last_power_button_down_timestamp_;
    if (delta.InMilliseconds() < 0) {
      LOG(ERROR) << "Negative duration between power button events";
      return;
    }
    last_power_button_down_timestamp_ = base::TimeTicks();
    if (!SendMetric(kMetricPowerButtonDownTimeName,
                    delta.InMilliseconds(),
                    kMetricPowerButtonDownTimeMin,
                    kMetricPowerButtonDownTimeMax,
                    kMetricPowerButtonDownTimeBuckets)) {
      LOG(ERROR) << "Could not send " << kMetricPowerButtonDownTimeName;
    }
  }
}

void Daemon::Shutdown() {
  if (shutdown_state_ == SHUTDOWN_STATE_POWER_OFF) {
    LOG(INFO) << "Shutting down, reason: " << shutdown_reason_;
    util::SendSignalWithStringToPowerM(kShutdownSignal,
                                       shutdown_reason_.c_str());
  } else if (shutdown_state_ == SHUTDOWN_STATE_RESTARTING) {
    LOG(INFO) << "Restarting";
    util::SendSignalToPowerM(kRestartSignal);
  } else {
    LOG(ERROR) << "Shutdown : Improper System State!";
  }
}

void Daemon::Suspend() {
  if (clean_shutdown_initiated_) {
    LOG(INFO) << "Ignoring request for suspend with outstanding shutdown.";
    return;
  }
  if (util::IsSessionStarted()) {
    power_supply_.SetSuspendState(true);
    suspender_.RequestSuspend();
    // When going to suspend, notify the backlight controller so it will know to
    // set the backlight correctly upon resume.
    SetPowerState(BACKLIGHT_SUSPENDED);
  } else {
    if (backlight_controller_->GetPowerState() == BACKLIGHT_SUSPENDED)
      shutdown_reason_ = kShutdownReasonIdle;
    else
      shutdown_reason_ = kShutdownReasonLidClosed;
    LOG(INFO) << "Not logged in. Suspend Request -> Shutting down.";
    OnRequestShutdown();
  }
}

gboolean Daemon::PrefChangeHandler(const char* name,
                                   int,               // watch handle
                                   unsigned int,      // mask
                                   gpointer data) {
  Daemon* daemon = static_cast<Daemon*>(data);
  if (!strcmp(name, kLockOnIdleSuspendPref)) {
    daemon->ReadLockScreenSettings();
    daemon->locker_.Init(daemon->lock_on_idle_suspend_);
    daemon->SetIdleOffset(0, IDLE_STATE_NORMAL);
  }
  if (!strcmp(name, kDisableIdleSuspendPref)) {
    daemon->ReadSuspendSettings();
    daemon->SetIdleOffset(0, IDLE_STATE_NORMAL);
  }
  return TRUE;
}

void Daemon::HandleResume() {
  time_to_empty_average_.Clear();
  time_to_full_average_.Clear();
  ResumePollPowerSupply();
  file_tagger_.HandleResumeEvent();
  power_supply_.SetSuspendState(false);
}

void Daemon::RetrieveSessionState() {
  std::string state;
  std::string user;
  if (!util::GetSessionState(&state, &user))
    return;
  LOG(INFO) << "Retrieved session state of " << state;
  OnSessionStateChange(state.c_str(), user.c_str());
}

void Daemon::AdjustIdleTimeoutsForProjection() {
  plugged_dim_ms_       = base_timeout_values_[kPluggedDimMsPref];
  plugged_off_ms_       = base_timeout_values_[kPluggedOffMsPref];
  plugged_suspend_ms_   = base_timeout_values_[kPluggedSuspendMsPref];
  unplugged_dim_ms_     = base_timeout_values_[kUnpluggedDimMsPref];
  unplugged_off_ms_     = base_timeout_values_[kUnpluggedOffMsPref];
  unplugged_suspend_ms_ = base_timeout_values_[kUnpluggedSuspendMsPref];
  default_lock_ms_      = base_timeout_values_[kLockMsPref];

  if (is_projecting_) {
    LOG(INFO) << "External display projection: multiplying idle times by "
              << kProjectionTimeoutFactor;
    plugged_dim_ms_ *= kProjectionTimeoutFactor;
    plugged_off_ms_ *= kProjectionTimeoutFactor;
    if (plugged_suspend_ms_ != INT64_MAX)
      plugged_suspend_ms_ *= kProjectionTimeoutFactor;
    unplugged_dim_ms_ *= kProjectionTimeoutFactor;
    unplugged_off_ms_ *= kProjectionTimeoutFactor;
    if (unplugged_suspend_ms_ != INT64_MAX)
      unplugged_suspend_ms_ *= kProjectionTimeoutFactor;
    if (default_lock_ms_ != INT64_MAX)
      default_lock_ms_ *= kProjectionTimeoutFactor;
  }
}

bool Daemon::ShouldStayAwakeForHeadphoneJack() {
#ifdef STAY_AWAKE_PLUGGED_DEVICE
  if (cras_client_ &&
      cras_client_output_dev_plugged(cras_client_, STAY_AWAKE_PLUGGED_DEVICE))
    return true;
#endif
  return false;
}

gboolean Daemon::ConnectToCras() {
  if (cras_client_connect(cras_client_) ||
      cras_client_run_thread(cras_client_)) {
    LOG(WARNING) << "Couldn't connect CRAS client, trying again later.";
    return TRUE;
  }
  LOG(INFO) << "CRAS client successfully connected to CRAS server.";
  connected_to_cras_ = true;
  return FALSE;
}

void Daemon::SetPowerState(PowerState state) {
  backlight_controller_->SetPowerState(state);
  if (keyboard_controller_)
    keyboard_controller_->SetPowerState(state);

  power_state_ = state;
}

bool Daemon::IsAudioPlaying() {
  struct timespec last_audio_time;
  if (!connected_to_cras_) {
    LOG(WARNING) << "Not connected to CRAS, assuming no audio playing.";
    return false;
  }
  if (cras_client_get_num_active_streams(cras_client_, &last_audio_time) > 0)
    return true;
  struct timespec time_now;
  if (clock_gettime(CLOCK_MONOTONIC, &time_now)) {
    LOG(WARNING) << "Could not read current clock time.";
    return false;
  }
  int64 delta_seconds = time_now.tv_sec - last_audio_time.tv_sec;
  int64 delta_ns = time_now.tv_nsec - last_audio_time.tv_nsec;
  CHECK(delta_seconds >= 0);
  base::TimeDelta last_audio_time_delta =
      base::TimeDelta::FromSeconds(delta_seconds) +
      base::TimeDelta::FromMicroseconds(
          delta_ns / base::Time::kNanosecondsPerMicrosecond);
  return last_audio_time_delta.InMilliseconds() < kAudioActivityThresholdMs;
}

bool Daemon::USBInputDeviceConnected() const {
  file_util::FileEnumerator enumerator(
      FilePath(sysfs_input_path_for_testing_.empty() ?
               kSysClassInputPath : sysfs_input_path_for_testing_),
      false,
      static_cast<file_util::FileEnumerator::FileType>(
          file_util::FileEnumerator::FILES |
          file_util::FileEnumerator::SHOW_SYM_LINKS),
      kInputMatchPattern);
  for (FilePath path = enumerator.Next();
       !path.empty();
       path = enumerator.Next()) {
    FilePath symlink_path;
    if (!file_util::ReadSymbolicLink(path, &symlink_path))
      continue;
    const std::string& path_string = symlink_path.value();
    // Skip bluetooth devices, which may be identified as USB devices.
    if (path_string.find(kBluetoothMatchString) != std::string::npos)
      continue;
    // Now look for the USB devices that are not bluetooth.
    size_t position = path_string.find(kUsbMatchString);
    if (position == std::string::npos)
      continue;
    // Now that the string "usb" has been found, make sure it is a whole word
    // and not just part of another word like "busbreaker".
    bool usb_at_word_head =
        position == 0 || !IsAsciiAlpha(path_string.at(position - 1));
    bool usb_at_word_tail =
        position + strlen(kUsbMatchString) == path_string.size() ||
        !IsAsciiAlpha(path_string.at(position + strlen(kUsbMatchString)));
    if (usb_at_word_head && usb_at_word_tail)
      return true;
  }
  return false;
}

void Daemon::UpdateBatteryReportState() {
  switch (power_status_.battery_state) {
    case BATTERY_STATE_FULLY_CHARGED:
      battery_report_state_ = BATTERY_REPORT_FULL;
      break;
    case BATTERY_STATE_DISCHARGING:
      switch (battery_report_state_) {
        case BATTERY_REPORT_FULL:
          battery_report_state_ = BATTERY_REPORT_PINNED;
          battery_report_pinned_start_ = base::TimeTicks::Now();
          break;
        case BATTERY_REPORT_TAPERED:
          {
            int64 tapered_delta_ms =
                (base::TimeTicks::Now() -
                 battery_report_tapered_start_).InMilliseconds();
            if (tapered_delta_ms >= kBatteryPercentTaperMs)
              battery_report_state_ = BATTERY_REPORT_ADJUSTED;
          }
          break;
        case BATTERY_REPORT_PINNED:
          if ((base::TimeTicks::Now() -
               battery_report_pinned_start_).InMilliseconds()
              >= kBatteryPercentPinMs) {
            battery_report_state_ = BATTERY_REPORT_TAPERED;
            battery_report_tapered_start_ = base::TimeTicks::Now();
          }
          break;
        default:
          break;
      }
      break;
    default:
      battery_report_state_ = BATTERY_REPORT_ADJUSTED;
      break;
  }
}

double Daemon::GetDisplayBatteryPercent() const {
  double battery_percentage = GetUsableBatteryPercent();
  switch (power_status_.battery_state) {
    case BATTERY_STATE_FULLY_CHARGED:
      battery_percentage = 100.0;
      break;
    case BATTERY_STATE_DISCHARGING:
      switch (battery_report_state_) {
        case BATTERY_REPORT_FULL:
        case BATTERY_REPORT_PINNED:
          battery_percentage = 100.0;
          break;
        case BATTERY_REPORT_TAPERED:
          {
            int64 tapered_delta_ms =
                (base::TimeTicks::Now() -
                 battery_report_tapered_start_).InMilliseconds();
            double elapsed_fraction =
                std::min(1.0, (static_cast<double>(tapered_delta_ms) /
                               kBatteryPercentTaperMs));
            battery_percentage = battery_percentage + (1.0 - elapsed_fraction) *
                (100.0 - battery_percentage);
          }
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }
  return battery_percentage;
}

double Daemon::GetUsableBatteryPercent() const {
  // If we are using a percentage based threshold adjust the reported percentage
  // to account for the bit being trimmed off. If we are using a time-based
  // threshold don't adjust the reported percentage. Adjusting the percentage
  // due to a time threshold might break the monoticity of percentages since the
  // time to empty/full is not guaranteed to be monotonic.
  if (power_status_.battery_percentage <= low_battery_shutdown_percent_) {
    return  0.0;
  } else if (power_status_.battery_percentage > 100.0) {
    LOG(WARNING) << "Before adjustment battery percentage was over 100%";
    return 100.0;
  } else if (low_battery_shutdown_time_s_) {
    return power_status_.battery_percentage;
  } else {  // Using percentage threshold
    // x = current percentage
    // y = adjusted percentage
    // t = threshold percentage
    // y = 100 *(x-t)/(100 - t)
    double battery_percentage = 100.0 * (power_status_.battery_percentage
                                         - low_battery_shutdown_percent_);
    battery_percentage /= 100.0 - low_battery_shutdown_percent_;
    return battery_percentage;
  }
}

}  // namespace power_manager
