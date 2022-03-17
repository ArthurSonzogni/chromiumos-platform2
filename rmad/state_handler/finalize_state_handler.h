// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_FINALIZE_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_FINALIZE_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <memory>
#include <utility>

#include <base/files/file_util.h>
#include <base/memory/scoped_refptr.h>
#include <base/sequenced_task_runner.h>
#include <base/timer/timer.h>

#include "rmad/utils/cr50_utils.h"
#include "rmad/utils/flashrom_utils.h"

namespace rmad {

class FinalizeStateHandler : public BaseStateHandler {
 public:
  // Report status every second.
  static constexpr base::TimeDelta kReportStatusInterval = base::Seconds(1);

  explicit FinalizeStateHandler(scoped_refptr<JsonStore> json_store);
  // Used to inject mock |cr50_utils_| and |flashrom_utils_| for testing.
  FinalizeStateHandler(scoped_refptr<JsonStore> json_store,
                       std::unique_ptr<Cr50Utils> cr50_utils,
                       std::unique_ptr<FlashromUtils> flashrom_utils);

  ASSIGN_STATE(RmadState::StateCase::kFinalize);
  SET_UNREPEATABLE;

  void RegisterSignalSender(FinalizeSignalCallback callback) override {
    finalize_signal_sender_ = callback;
  }

  RmadErrorCode InitializeState() override;
  void CleanUpState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

  void StartTasks();

 protected:
  ~FinalizeStateHandler() override = default;

 private:
  void SendStatusSignal();
  void StartStatusTimer();
  void StopStatusTimer();

  void StartFinalize();
  void FinalizeTask();

  FinalizeStatus status_;
  FinalizeSignalCallback finalize_signal_sender_;
  std::unique_ptr<Cr50Utils> cr50_utils_;
  std::unique_ptr<FlashromUtils> flashrom_utils_;
  base::RepeatingTimer status_timer_;
  scoped_refptr<base::SequencedTaskRunner> task_runner_;
};

namespace fake {

class FakeFinalizeStateHandler : public FinalizeStateHandler {
 public:
  FakeFinalizeStateHandler(scoped_refptr<JsonStore> json_store,
                           const base::FilePath& working_dir_path);

 protected:
  ~FakeFinalizeStateHandler() override = default;
};

}  // namespace fake

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_FINALIZE_STATE_HANDLER_H_
