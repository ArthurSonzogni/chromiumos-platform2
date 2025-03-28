// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/welcome_screen_state_handler.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/strings/string_util.h>
#include <base/task/sequenced_task_runner.h>

#include "rmad/constants.h"
#include "rmad/logs/logs_utils.h"
#include "rmad/system/hardware_verifier_client_impl.h"
#include "rmad/utils/vpd_utils_impl.h"

namespace {

constexpr char kNewlineSeparator[] = "\n";
constexpr char kCommaSeparator[] = ", ";

}  // namespace

namespace rmad {

WelcomeScreenStateHandler::WelcomeScreenStateHandler(
    scoped_refptr<JsonStore> json_store,
    scoped_refptr<DaemonCallback> daemon_callback)
    : BaseStateHandler(json_store, daemon_callback),
      working_dir_path_(kDefaultWorkingDirPath),
      hardware_verifier_client_(std::make_unique<HardwareVerifierClientImpl>()),
      vpd_utils_(std::make_unique<VpdUtilsImpl>()) {}

WelcomeScreenStateHandler::WelcomeScreenStateHandler(
    scoped_refptr<JsonStore> json_store,
    scoped_refptr<DaemonCallback> daemon_callback,
    const base::FilePath& working_dir_path,
    std::unique_ptr<HardwareVerifierClient> hardware_verifier_client,
    std::unique_ptr<VpdUtils> vpd_utils)
    : BaseStateHandler(json_store, daemon_callback),
      working_dir_path_(working_dir_path),
      hardware_verifier_client_(std::move(hardware_verifier_client)),
      vpd_utils_(std::move(vpd_utils)) {}

RmadErrorCode WelcomeScreenStateHandler::InitializeState() {
  if (!state_.has_welcome()) {
    state_.set_allocated_welcome(new WelcomeState);
  }

  return RMAD_ERROR_OK;
}

void WelcomeScreenStateHandler::OnGetStateTask() const {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WelcomeScreenStateHandler::RunHardwareVerifier,
                                base::Unretained(this)));
}

BaseStateHandler::GetNextStateCaseReply
WelcomeScreenStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_welcome()) {
    LOG(ERROR) << "RmadState missing |welcome| state.";
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_INVALID);
  }

  switch (state.welcome().choice()) {
    case WelcomeState::RMAD_CHOICE_UNKNOWN:
      return NextStateCaseWrapper(RMAD_ERROR_REQUEST_ARGS_MISSING);
    case WelcomeState::RMAD_CHOICE_FINALIZE_REPAIR:
      return NextStateCaseWrapper(RmadState::StateCase::kComponentsRepair);
    default:
      break;
  }
  NOTREACHED_IN_MIGRATION();
  return NextStateCaseWrapper(RmadState::StateCase::STATE_NOT_SET,
                              RMAD_ERROR_NOT_SET,
                              RMAD_ADDITIONAL_ACTIVITY_NOTHING);
}

void WelcomeScreenStateHandler::RunHardwareVerifier() const {
  bool is_compliant;
  std::vector<std::string> error_strings;

  if (hardware_verifier_client_->GetHardwareVerificationResult(
          &is_compliant, &error_strings)) {
    // Use multi-line error string for UX.
    HardwareVerificationResult result;
    result.set_is_compliant(is_compliant);
    result.set_error_str(base::JoinString(error_strings, kNewlineSeparator));
    daemon_callback_->GetHardwareVerificationSignalCallback().Run(result);
    // Use single-line error string for logs.
    RecordUnqualifiedComponentsToLogs(
        json_store_, is_compliant,
        base::JoinString(error_strings, kCommaSeparator));
  } else if (uint64_t shimless_mode;
             (vpd_utils_->GetShimlessMode(&shimless_mode) &&
              (shimless_mode & kShimlessModeFlagsRaccResultBypass)) ||
             IsRaccResultBypassed(working_dir_path_)) {
    HardwareVerificationResult result;
    result.set_is_compliant(true);
    result.set_error_str("");
    LOG(INFO) << "Component compliance check bypassed.";
    daemon_callback_->GetHardwareVerificationSignalCallback().Run(result);
  } else {
    LOG(ERROR) << "Failed to get hardware verification result";
  }
}

}  // namespace rmad
