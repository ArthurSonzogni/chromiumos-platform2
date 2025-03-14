// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_manager/real_updater_provider.h"

#include <inttypes.h>

#include <algorithm>
#include <string>

#include <base/functional/bind.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <update_engine/dbus-constants.h>

#include "update_engine/client_library/include/update_engine/update_status.h"
#include "update_engine/common/prefs.h"
#include "update_engine/common/system_state.h"
#include "update_engine/cros/omaha_request_params.h"
#include "update_engine/cros/update_attempter.h"
#include "update_engine/update_status_utils.h"

using base::StringPrintf;
using base::Time;
using base::TimeDelta;
using chromeos_update_engine::OmahaRequestParams;
using chromeos_update_engine::SystemState;
using std::string;
using update_engine::UpdateAttemptFlags;
using update_engine::UpdateEngineStatus;

namespace chromeos_update_manager {

// A templated base class for all update related variables. Provides uniform
// construction and a system state handle.
template <typename T>
class UpdaterVariableBase : public Variable<T> {
 public:
  UpdaterVariableBase(const string& name, VariableMode mode)
      : Variable<T>(name, mode) {}
};

// Helper class for issuing a GetStatus() to the UpdateAttempter.
class GetStatusHelper {
 public:
  explicit GetStatusHelper(string* errmsg) {
    is_success_ = SystemState::Get()->update_attempter()->GetStatus(
        &update_engine_status_);
    if (!is_success_ && errmsg) {
      *errmsg = "Failed to get a status update from the update engine";
    }
  }

  inline bool is_success() { return is_success_; }
  inline int64_t last_checked_time() {
    return update_engine_status_.last_checked_time;
  }
  inline double progress() { return update_engine_status_.progress; }
  inline const string update_status() {
    return chromeos_update_engine::UpdateStatusToString(
        update_engine_status_.status);
  }
  inline const string& new_version() {
    return update_engine_status_.new_version;
  }
  inline uint64_t payload_size() {
    return update_engine_status_.new_size_bytes;
  }

 private:
  bool is_success_;
  UpdateEngineStatus update_engine_status_;
};

// A variable reporting the time when a last update check was issued.
class LastCheckedTimeVariable : public UpdaterVariableBase<Time> {
 public:
  explicit LastCheckedTimeVariable(const string& name)
      : UpdaterVariableBase<Time>(name, kVariableModePoll) {}
  LastCheckedTimeVariable(const LastCheckedTimeVariable&) = delete;
  LastCheckedTimeVariable& operator=(const LastCheckedTimeVariable&) = delete;

 private:
  const Time* GetValue(TimeDelta /* timeout */, string* errmsg) override {
    GetStatusHelper raw(errmsg);
    if (!raw.is_success()) {
      return nullptr;
    }

    return new Time(Time::FromTimeT(raw.last_checked_time()));
  }
};

// A variable reporting the update (download) progress as a decimal fraction
// between 0.0 and 1.0.
class ProgressVariable : public UpdaterVariableBase<double> {
 public:
  explicit ProgressVariable(const string& name)
      : UpdaterVariableBase<double>(name, kVariableModePoll) {}
  ProgressVariable(const ProgressVariable&) = delete;
  ProgressVariable& operator=(const ProgressVariable&) = delete;

 private:
  const double* GetValue(TimeDelta /* timeout */, string* errmsg) override {
    GetStatusHelper raw(errmsg);
    if (!raw.is_success()) {
      return nullptr;
    }

    if (raw.progress() < 0.0 || raw.progress() > 1.0) {
      if (errmsg) {
        *errmsg =
            StringPrintf("Invalid progress value received: %f", raw.progress());
      }
      return nullptr;
    }

    return new double(raw.progress());
  }
};

// A variable reporting the stage in which the update process is.
class StageVariable : public UpdaterVariableBase<Stage> {
 public:
  explicit StageVariable(const string& name)
      : UpdaterVariableBase<Stage>(name, kVariableModePoll) {}
  StageVariable(const StageVariable&) = delete;
  StageVariable& operator=(const StageVariable&) = delete;

 private:
  struct CurrOpStrToStage {
    const char* str;
    Stage stage;
  };
  static const CurrOpStrToStage curr_op_str_to_stage[];

  // Note: the method is defined outside the class so arraysize can work.
  const Stage* GetValue(TimeDelta /* timeout */, string* errmsg) override;
};

const StageVariable::CurrOpStrToStage StageVariable::curr_op_str_to_stage[] = {
    {update_engine::kUpdateStatusIdle, Stage::kIdle},
    {update_engine::kUpdateStatusCheckingForUpdate, Stage::kCheckingForUpdate},
    {update_engine::kUpdateStatusUpdateAvailable, Stage::kUpdateAvailable},
    {update_engine::kUpdateStatusDownloading, Stage::kDownloading},
    {update_engine::kUpdateStatusVerifying, Stage::kVerifying},
    {update_engine::kUpdateStatusFinalizing, Stage::kFinalizing},
    {update_engine::kUpdateStatusUpdatedNeedReboot, Stage::kUpdatedNeedReboot},
    {update_engine::kUpdateStatusReportingErrorEvent,
     Stage::kReportingErrorEvent},
    {update_engine::kUpdateStatusAttemptingRollback,
     Stage::kAttemptingRollback},
    {update_engine::kUpdateStatusCleanupPreviousUpdate,
     Stage::kCleanupPreviousUpdate},
};

const Stage* StageVariable::GetValue(TimeDelta /* timeout */, string* errmsg) {
  GetStatusHelper raw(errmsg);
  if (!raw.is_success()) {
    return nullptr;
  }

  for (auto& key_val : curr_op_str_to_stage) {
    if (raw.update_status() == key_val.str) {
      return new Stage(key_val.stage);
    }
  }

  if (errmsg) {
    *errmsg = string("Unknown update status: ") + raw.update_status();
  }
  return nullptr;
}

// A variable reporting the version number that an update is updating to.
class NewVersionVariable : public UpdaterVariableBase<string> {
 public:
  explicit NewVersionVariable(const string& name)
      : UpdaterVariableBase<string>(name, kVariableModePoll) {}
  NewVersionVariable(const NewVersionVariable&) = delete;
  NewVersionVariable& operator=(const NewVersionVariable&) = delete;

 private:
  const string* GetValue(TimeDelta /* timeout */, string* errmsg) override {
    GetStatusHelper raw(errmsg);
    if (!raw.is_success()) {
      return nullptr;
    }

    return new string(raw.new_version());
  }
};

// A variable reporting the size of the update being processed in bytes.
class PayloadSizeVariable : public UpdaterVariableBase<uint64_t> {
 public:
  explicit PayloadSizeVariable(const string& name)
      : UpdaterVariableBase<uint64_t>(name, kVariableModePoll) {}
  PayloadSizeVariable(const PayloadSizeVariable&) = delete;
  PayloadSizeVariable& operator=(const PayloadSizeVariable&) = delete;

 private:
  const uint64_t* GetValue(TimeDelta /* timeout */, string* errmsg) override {
    GetStatusHelper raw(errmsg);
    if (!raw.is_success()) {
      return nullptr;
    }

    return new uint64_t(raw.payload_size());
  }
};

// A variable reporting the point in time an update last completed in the
// current boot cycle.
//
// TODO(garnold) In general, both the current boottime and wallclock time
// readings should come from the time provider and be moderated by the
// evaluation context, so that they are uniform throughout the evaluation of a
// policy request.
class UpdateCompletedTimeVariable : public UpdaterVariableBase<Time> {
 public:
  explicit UpdateCompletedTimeVariable(const string& name)
      : UpdaterVariableBase<Time>(name, kVariableModePoll) {}
  UpdateCompletedTimeVariable(const UpdateCompletedTimeVariable&) = delete;
  UpdateCompletedTimeVariable& operator=(const UpdateCompletedTimeVariable&) =
      delete;

 private:
  const Time* GetValue(TimeDelta /* timeout */, string* errmsg) override {
    Time update_boottime;
    if (!SystemState::Get()->update_attempter()->GetBootTimeAtUpdate(
            &update_boottime)) {
      if (errmsg) {
        *errmsg = "Update completed time could not be read";
      }
      return nullptr;
    }

    const auto* clock = SystemState::Get()->clock();
    Time curr_boottime = clock->GetBootTime();
    if (curr_boottime < update_boottime) {
      if (errmsg) {
        *errmsg = "Update completed time more recent than current time";
      }
      return nullptr;
    }
    TimeDelta duration_since_update = curr_boottime - update_boottime;
    return new Time(clock->GetWallclockTime() - duration_since_update);
  }
};

// Variables reporting the current image channel.
class CurrChannelVariable : public UpdaterVariableBase<string> {
 public:
  explicit CurrChannelVariable(const string& name)
      : UpdaterVariableBase<string>(name, kVariableModePoll) {}
  CurrChannelVariable(const CurrChannelVariable&) = delete;
  CurrChannelVariable& operator=(const CurrChannelVariable&) = delete;

 private:
  const string* GetValue(TimeDelta /* timeout */, string* errmsg) override {
    OmahaRequestParams* request_params = SystemState::Get()->request_params();
    string channel = request_params->current_channel();
    if (channel.empty()) {
      if (errmsg) {
        *errmsg = "No current channel";
      }
      return nullptr;
    }
    return new string(channel);
  }
};

// Variables reporting the new image channel.
class NewChannelVariable : public UpdaterVariableBase<string> {
 public:
  explicit NewChannelVariable(const string& name)
      : UpdaterVariableBase<string>(name, kVariableModePoll) {}
  NewChannelVariable(const NewChannelVariable&) = delete;
  NewChannelVariable& operator=(const NewChannelVariable&) = delete;

 private:
  const string* GetValue(TimeDelta /* timeout */, string* errmsg) override {
    OmahaRequestParams* request_params = SystemState::Get()->request_params();
    string channel = request_params->target_channel();
    if (channel.empty()) {
      if (errmsg) {
        *errmsg = "No new channel";
      }
      return nullptr;
    }
    return new string(channel);
  }
};

// A variable class for reading Boolean prefs values.
class BooleanPrefVariable
    : public AsyncCopyVariable<bool>,
      public chromeos_update_engine::PrefsInterface::ObserverInterface {
 public:
  BooleanPrefVariable(const string& name, const char* key, bool default_val)
      : AsyncCopyVariable<bool>(name), key_(key), default_val_(default_val) {
    SystemState::Get()->prefs()->AddObserver(key, this);
    OnPrefSet(key);
  }
  BooleanPrefVariable(const BooleanPrefVariable&) = delete;
  BooleanPrefVariable& operator=(const BooleanPrefVariable&) = delete;

  ~BooleanPrefVariable() {
    SystemState::Get()->prefs()->RemoveObserver(key_, this);
  }

 private:
  // Reads the actual value from the Prefs instance and updates the Variable
  // value.
  void OnPrefSet(const string& key) override {
    bool result = default_val_;
    auto* prefs = SystemState::Get()->prefs();
    if (prefs->Exists(key_) && !prefs->GetBoolean(key_, &result)) {
      result = default_val_;
    }
    // AsyncCopyVariable will take care of values that didn't change.
    SetValue(result);
  }

  void OnPrefDeleted(const string& key) override { SetValue(default_val_); }

  // The Boolean preference key and default value.
  const char* const key_;
  const bool default_val_;
};

// A variable returning the number of consecutive failed update checks.
class ConsecutiveFailedUpdateChecksVariable
    : public UpdaterVariableBase<unsigned int> {
 public:
  explicit ConsecutiveFailedUpdateChecksVariable(const string& name)
      : UpdaterVariableBase<unsigned int>(name, kVariableModePoll) {}
  ConsecutiveFailedUpdateChecksVariable(
      const ConsecutiveFailedUpdateChecksVariable&) = delete;
  ConsecutiveFailedUpdateChecksVariable& operator=(
      const ConsecutiveFailedUpdateChecksVariable&) = delete;

 private:
  const unsigned int* GetValue(TimeDelta /* timeout */,
                               string* /* errmsg */) override {
    // NOLINTNEXTLINE(readability/casting)
    return new unsigned int(SystemState::Get()
                                ->update_attempter()
                                ->consecutive_failed_update_checks());
  }
};

// A variable returning the server-dictated poll interval.
class ServerDictatedPollIntervalVariable
    : public UpdaterVariableBase<unsigned int> {
 public:
  explicit ServerDictatedPollIntervalVariable(const string& name)
      : UpdaterVariableBase<unsigned int>(name, kVariableModePoll) {}
  ServerDictatedPollIntervalVariable(
      const ServerDictatedPollIntervalVariable&) = delete;
  ServerDictatedPollIntervalVariable& operator=(
      const ServerDictatedPollIntervalVariable&) = delete;

 private:
  const unsigned int* GetValue(TimeDelta /* timeout */,
                               string* /* errmsg */) override {
    // NOLINTNEXTLINE(readability/casting)
    return new unsigned int(SystemState::Get()
                                ->update_attempter()
                                ->server_dictated_poll_interval());
  }
};

// An async variable that tracks changes to forced update requests.
class ForcedUpdateRequestedVariable
    : public UpdaterVariableBase<UpdateRequestStatus> {
 public:
  explicit ForcedUpdateRequestedVariable(const string& name)
      : UpdaterVariableBase<UpdateRequestStatus>::UpdaterVariableBase(
            name, kVariableModeAsync) {
    SystemState::Get()->update_attempter()->set_forced_update_pending_callback(
        new base::RepeatingCallback<void(
            bool, bool)>(  // NOLINT(readability/function)
            base::BindRepeating(&ForcedUpdateRequestedVariable::Reset,
                                base::Unretained(this))));
  }
  ForcedUpdateRequestedVariable(const ForcedUpdateRequestedVariable&) = delete;
  ForcedUpdateRequestedVariable& operator=(
      const ForcedUpdateRequestedVariable&) = delete;

 private:
  const UpdateRequestStatus* GetValue(TimeDelta /* timeout */,
                                      string* /* errmsg */) override {
    return new UpdateRequestStatus(update_request_status_);
  }

  void Reset(bool forced_update_requested, bool interactive) {
    UpdateRequestStatus new_value = UpdateRequestStatus::kNone;
    if (forced_update_requested) {
      new_value = (interactive ? UpdateRequestStatus::kInteractive
                               : UpdateRequestStatus::kPeriodic);
    }
    if (update_request_status_ != new_value) {
      update_request_status_ = new_value;
      NotifyValueChanged();
    }
  }

  UpdateRequestStatus update_request_status_ = UpdateRequestStatus::kNone;
};

// A variable class for reading timeout interval prefs value.
class TestUpdateCheckIntervalTimeoutVariable : public Variable<int64_t> {
 public:
  explicit TestUpdateCheckIntervalTimeoutVariable(const string& name)
      : Variable<int64_t>(name, kVariableModePoll), read_count_(0) {
    SetMissingOk();
  }
  TestUpdateCheckIntervalTimeoutVariable(
      const TestUpdateCheckIntervalTimeoutVariable&) = delete;
  TestUpdateCheckIntervalTimeoutVariable& operator=(
      const TestUpdateCheckIntervalTimeoutVariable&) = delete;

  ~TestUpdateCheckIntervalTimeoutVariable() = default;

 private:
  const int64_t* GetValue(TimeDelta /* timeout */,
                          string* /* errmsg */) override {
    auto key = chromeos_update_engine::kPrefsTestUpdateCheckIntervalTimeout;
    auto* prefs = SystemState::Get()->prefs();
    int64_t result;
    if (prefs->Exists(key) && prefs->GetInt64(key, &result)) {
      // This specific value is used for testing only. So it should not be kept
      // around and should be deleted after a few reads.
      if (++read_count_ > 5) {
        prefs->Delete(key);
      }

      // Limit the timeout interval to 10 minutes so it is not abused if it is
      // seen on official images.
      return new int64_t(std::min(result, static_cast<int64_t>(10 * 60)));
    }
    return nullptr;
  }

  // Counts how many times this variable is read. This is used to delete the
  // underlying file defining the variable after a certain number of reads in
  // order to prevent any abuse of this variable.
  int read_count_;
};

// RealUpdaterProvider methods.

RealUpdaterProvider::RealUpdaterProvider()
    : var_updater_started_time_(
          "updater_started_time",
          SystemState::Get()->clock()->GetWallclockTime()),
      var_last_checked_time_(new LastCheckedTimeVariable("last_checked_time")),
      var_update_completed_time_(
          new UpdateCompletedTimeVariable("update_completed_time")),
      var_progress_(new ProgressVariable("progress")),
      var_stage_(new StageVariable("stage")),
      var_new_version_(new NewVersionVariable("new_version")),
      var_payload_size_(new PayloadSizeVariable("payload_size")),
      var_curr_channel_(new CurrChannelVariable("curr_channel")),
      var_new_channel_(new NewChannelVariable("new_channel")),
      var_p2p_enabled_(new BooleanPrefVariable(
          "p2p_enabled", chromeos_update_engine::kPrefsP2PEnabled, false)),
      var_cellular_enabled_(new BooleanPrefVariable(
          "cellular_enabled",
          chromeos_update_engine::kPrefsUpdateOverCellularPermission,
          false)),
      var_market_segment_disabled_(new BooleanPrefVariable(
          "market_segment_disabled",
          chromeos_update_engine::kPrefsMarketSegmentDisabled,
          false)),
      var_consecutive_failed_update_checks_(
          new ConsecutiveFailedUpdateChecksVariable(
              "consecutive_failed_update_checks")),
      var_server_dictated_poll_interval_(new ServerDictatedPollIntervalVariable(
          "server_dictated_poll_interval")),
      var_forced_update_requested_(
          new ForcedUpdateRequestedVariable("forced_update_requested")),
      var_test_update_check_interval_timeout_(
          new TestUpdateCheckIntervalTimeoutVariable(
              "test_update_check_interval_timeout")),
      var_consumer_auto_update_disabled_(new BooleanPrefVariable(
          "consumer_auto_update_disabled",
          chromeos_update_engine::kPrefsConsumerAutoUpdateDisabled,
          false)) {}
}  // namespace chromeos_update_manager
