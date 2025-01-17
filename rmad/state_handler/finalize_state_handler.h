// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_FINALIZE_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_FINALIZE_STATE_HANDLER_H_

#include <memory>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/memory/scoped_refptr.h>
#include <base/task/sequenced_task_runner.h>
#include <base/timer/timer.h>

#include "rmad/state_handler/base_state_handler.h"
#include "rmad/utils/cros_config_utils.h"
#include "rmad/utils/gsc_utils.h"
#include "rmad/utils/vpd_utils.h"
#include "rmad/utils/write_protect_utils.h"

namespace rmad {

class FinalizeStateHandler : public BaseStateHandler {
 public:
  // Report status every second.
  static constexpr base::TimeDelta kReportStatusInterval = base::Seconds(1);

  explicit FinalizeStateHandler(scoped_refptr<JsonStore> json_store,
                                scoped_refptr<DaemonCallback> daemon_callback);
  // Used to inject |working_dir_path_|,  |cros_config_utils_|, |gsc_utils_|,
  // |write_protect_utils_|, and |vpd_utils_| for testing.
  explicit FinalizeStateHandler(
      scoped_refptr<JsonStore> json_store,
      scoped_refptr<DaemonCallback> daemon_callback,
      const base::FilePath& working_dir_path,
      std::unique_ptr<CrosConfigUtils> cros_config_utils,
      std::unique_ptr<GscUtils> gsc_utils,
      std::unique_ptr<WriteProtectUtils> write_protect_utils,
      std::unique_ptr<VpdUtils> vpd_utils);

  ASSIGN_STATE(RmadState::StateCase::kFinalize);
  SET_UNREPEATABLE;

  RmadErrorCode InitializeState() override;
  void RunState() override;
  void CleanUpState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

 protected:
  ~FinalizeStateHandler() override = default;

 private:
  void SendStatusSignal();
  void StartStatusTimer();
  void StopStatusTimer();

  void StartFinalize();
  void FinalizeTask();
  void ResetFpmcuEntropyCallback(bool success);
  void OnResetFpmcuEntropySucceed();

  bool IsFingerprintSupported() const;
  bool IsBoardIdCheckBypassed() const;

  base::FilePath working_dir_path_;
  FinalizeStatus status_;

  std::unique_ptr<CrosConfigUtils> cros_config_utils_;
  std::unique_ptr<GscUtils> gsc_utils_;
  std::unique_ptr<WriteProtectUtils> write_protect_utils_;
  std::unique_ptr<VpdUtils> vpd_utils_;
  base::RepeatingTimer status_timer_;

  scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner_;
};

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_FINALIZE_STATE_HANDLER_H_
