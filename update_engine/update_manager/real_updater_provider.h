// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_REAL_UPDATER_PROVIDER_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_REAL_UPDATER_PROVIDER_H_

#include <memory>
#include <string>

#include "update_engine/update_manager/generic_variables.h"
#include "update_engine/update_manager/updater_provider.h"

namespace chromeos_update_manager {

// A concrete UpdaterProvider implementation using local (in-process) bindings.
class RealUpdaterProvider : public UpdaterProvider {
 public:
  // We assume that any other object handle we get from the system state is
  // "volatile", and so must be re-acquired whenever access is needed; this
  // guarantees that parts of the system state can be mocked out at any time
  // during testing. We further assume that, by the time Init() is called, the
  // system state object is fully populated and usable.
  RealUpdaterProvider();
  RealUpdaterProvider(const RealUpdaterProvider&) = delete;
  RealUpdaterProvider& operator=(const RealUpdaterProvider&) = delete;

  // Initializes the provider and returns whether it succeeded.
  bool Init() { return true; }

  Variable<base::Time>* var_updater_started_time() override {
    return &var_updater_started_time_;
  }

  Variable<base::Time>* var_last_checked_time() override {
    return var_last_checked_time_.get();
  }

  Variable<base::Time>* var_update_completed_time() override {
    return var_update_completed_time_.get();
  }

  Variable<double>* var_progress() override { return var_progress_.get(); }

  Variable<Stage>* var_stage() override { return var_stage_.get(); }

  Variable<std::string>* var_new_version() override {
    return var_new_version_.get();
  }

  Variable<uint64_t>* var_payload_size() override {
    return var_payload_size_.get();
  }

  Variable<std::string>* var_curr_channel() override {
    return var_curr_channel_.get();
  }

  Variable<std::string>* var_new_channel() override {
    return var_new_channel_.get();
  }

  Variable<bool>* var_p2p_enabled() override { return var_p2p_enabled_.get(); }

  Variable<bool>* var_cellular_enabled() override {
    return var_cellular_enabled_.get();
  }

  Variable<bool>* var_market_segment_disabled() override {
    return var_market_segment_disabled_.get();
  }

  Variable<unsigned int>* var_consecutive_failed_update_checks() override {
    return var_consecutive_failed_update_checks_.get();
  }

  Variable<unsigned int>* var_server_dictated_poll_interval() override {
    return var_server_dictated_poll_interval_.get();
  }

  Variable<UpdateRequestStatus>* var_forced_update_requested() override {
    return var_forced_update_requested_.get();
  }

  Variable<int64_t>* var_test_update_check_interval_timeout() override {
    return var_test_update_check_interval_timeout_.get();
  }

  Variable<bool>* var_consumer_auto_update_disabled() override {
    return var_consumer_auto_update_disabled_.get();
  }

 private:
  // Variable implementations.
  ConstCopyVariable<base::Time> var_updater_started_time_;
  std::unique_ptr<Variable<base::Time>> var_last_checked_time_;
  std::unique_ptr<Variable<base::Time>> var_update_completed_time_;
  std::unique_ptr<Variable<double>> var_progress_;
  std::unique_ptr<Variable<Stage>> var_stage_;
  std::unique_ptr<Variable<std::string>> var_new_version_;
  std::unique_ptr<Variable<uint64_t>> var_payload_size_;
  std::unique_ptr<Variable<std::string>> var_curr_channel_;
  std::unique_ptr<Variable<std::string>> var_new_channel_;
  std::unique_ptr<Variable<bool>> var_p2p_enabled_;
  std::unique_ptr<Variable<bool>> var_cellular_enabled_;
  std::unique_ptr<Variable<bool>> var_market_segment_disabled_;
  std::unique_ptr<Variable<unsigned int>> var_consecutive_failed_update_checks_;
  std::unique_ptr<Variable<unsigned int>> var_server_dictated_poll_interval_;
  std::unique_ptr<Variable<UpdateRequestStatus>> var_forced_update_requested_;
  std::unique_ptr<Variable<int64_t>> var_test_update_check_interval_timeout_;
  std::unique_ptr<Variable<bool>> var_consumer_auto_update_disabled_;
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_REAL_UPDATER_PROVIDER_H_
