// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/welcome_screen_state_handler.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/strings/string_util.h>
#include <base/task/sequenced_task_runner.h>

#include "rmad/constants.h"
#include "rmad/logs/logs_utils.h"
#include "rmad/metrics/metrics_utils.h"
#include "rmad/system/hardware_verifier_client_impl.h"
#include "rmad/utils/rmad_config_utils_impl.h"
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
      vpd_utils_(std::make_unique<VpdUtilsImpl>()),
      rmad_config_utils_(std::make_unique<RmadConfigUtilsImpl>()) {}

WelcomeScreenStateHandler::WelcomeScreenStateHandler(
    scoped_refptr<JsonStore> json_store,
    scoped_refptr<DaemonCallback> daemon_callback,
    const base::FilePath& working_dir_path,
    std::unique_ptr<HardwareVerifierClient> hardware_verifier_client,
    std::unique_ptr<VpdUtils> vpd_utils,
    std::unique_ptr<RmadConfigUtils> rmad_config_utils)
    : BaseStateHandler(json_store, daemon_callback),
      working_dir_path_(working_dir_path),
      hardware_verifier_client_(std::move(hardware_verifier_client)),
      vpd_utils_(std::move(vpd_utils)),
      rmad_config_utils_(std::move(rmad_config_utils)) {}

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
      if (IsSpareMlb()) {
        json_store_->SetValue(kMlbRepair, true);
        json_store_->SetValue(kSameOwner, false);
        json_store_->SetValue(kWpDisableRequired, true);
        json_store_->SetValue(kWipeDevice, true);
        json_store_->SetValue(kCcdBlocked, false);
        MetricsUtils::SetMetricsValue(
            json_store_, kMetricsReturningOwner,
            ReturningOwner_Name(
                ReturningOwner::RMAD_RETURNING_OWNER_DIFFERENT_OWNER));
        MetricsUtils::SetMetricsValue(
            json_store_, kMetricsMlbReplacement,
            MainboardReplacement_Name(
                MainboardReplacement::RMAD_MLB_REPLACEMENT_REPLACED));
        return NextStateCaseWrapper(RmadState::StateCase::kWpDisablePhysical);
      }
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

  if (ShouldSkipHardwareVerification()) {
    HardwareVerificationResult result;
    result.set_is_skipped(true);
    result.set_error_str("");
    LOG(INFO) << "Component compliance check bypassed.";
    daemon_callback_->GetHardwareVerificationSignalCallback().Run(result);
  } else if (hardware_verifier_client_->GetHardwareVerificationResult(
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
  } else {
    LOG(ERROR) << "Failed to get hardware verification result";
  }
}

bool WelcomeScreenStateHandler::ShouldSkipHardwareVerification() const {
  uint64_t shimless_mode;
  auto rmad_config = rmad_config_utils_->GetConfig();
  bool shimless_mode_skipped =
      vpd_utils_->GetShimlessMode(&shimless_mode) &&
      (shimless_mode & kShimlessModeFlagsRaccResultBypass);
  bool rmad_config_skipped =
      rmad_config.has_value() && rmad_config->skip_hardware_verification();
  bool racc_disable =
      base::PathExists(working_dir_path_.AppendASCII(kDisableRaccFilePath));

  return shimless_mode_skipped || rmad_config_skipped || racc_disable;
}

bool WelcomeScreenStateHandler::IsSpareMlb() const {
  bool spare_mlb = false;
  return json_store_->GetValue(kSpareMlb, &spare_mlb) && spare_mlb;
}

}  // namespace rmad
