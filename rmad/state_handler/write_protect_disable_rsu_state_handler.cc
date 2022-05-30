// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/write_protect_disable_rsu_state_handler.h"

#include <cstdio>
#include <memory>
#include <string>
#include <utility>

#include <base/strings/string_util.h>

#include "rmad/common/types.h"
#include "rmad/constants.h"
#include "rmad/system/fake_power_manager_client.h"
#include "rmad/system/power_manager_client_impl.h"
#include "rmad/utils/cr50_utils_impl.h"
#include "rmad/utils/crossystem_utils_impl.h"
#include "rmad/utils/dbus_utils.h"
#include "rmad/utils/fake_cr50_utils.h"
#include "rmad/utils/fake_crossystem_utils.h"

#include <base/logging.h>

namespace {

// RSU server URL.
constexpr char kRsuUrlFormat[] =
    "https://www.google.com/chromeos/partner/console/"
    "cr50reset?challenge=%s&hwid=%s";

}  // namespace

namespace rmad {

namespace fake {

FakeWriteProtectDisableRsuStateHandler::FakeWriteProtectDisableRsuStateHandler(
    scoped_refptr<JsonStore> json_store, const base::FilePath& working_dir_path)
    : WriteProtectDisableRsuStateHandler(
          json_store,
          working_dir_path,
          std::make_unique<FakeCr50Utils>(working_dir_path),
          std::make_unique<FakeCrosSystemUtils>(working_dir_path),
          std::make_unique<FakePowerManagerClient>(working_dir_path)) {}

}  // namespace fake

WriteProtectDisableRsuStateHandler::WriteProtectDisableRsuStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store),
      working_dir_path_(kDefaultWorkingDirPath),
      reboot_scheduled_(false) {
  cr50_utils_ = std::make_unique<Cr50UtilsImpl>();
  crossystem_utils_ = std::make_unique<CrosSystemUtilsImpl>();
  power_manager_client_ =
      std::make_unique<PowerManagerClientImpl>(GetSystemBus());
}

WriteProtectDisableRsuStateHandler::WriteProtectDisableRsuStateHandler(
    scoped_refptr<JsonStore> json_store,
    const base::FilePath& working_dir_path,
    std::unique_ptr<Cr50Utils> cr50_utils,
    std::unique_ptr<CrosSystemUtils> crossystem_utils,
    std::unique_ptr<PowerManagerClient> power_manager_client)
    : BaseStateHandler(json_store),
      working_dir_path_(working_dir_path),
      cr50_utils_(std::move(cr50_utils)),
      crossystem_utils_(std::move(crossystem_utils)),
      power_manager_client_(std::move(power_manager_client)),
      reboot_scheduled_(false) {}

RmadErrorCode WriteProtectDisableRsuStateHandler::InitializeState() {
  // No need to store state. The challenge code will be different every time
  // the daemon restarts.
  if (!state_.has_wp_disable_rsu()) {
    auto wp_disable_rsu = std::make_unique<WriteProtectDisableRsuState>();

    wp_disable_rsu->set_rsu_done(IsFactoryModeEnabled());

    std::string challenge_code;
    if (cr50_utils_->GetRsuChallengeCode(&challenge_code)) {
      wp_disable_rsu->set_challenge_code(challenge_code);
    } else {
      return RMAD_ERROR_WRITE_PROTECT_DISABLE_RSU_NO_CHALLENGE;
    }

    // Allow unknown HWID as the field might be corrupted.
    // This is fine since HWID is only used for server side logging. It doesn't
    // affect RSU functionality.
    std::string hwid = "";
    crossystem_utils_->GetHwid(&hwid);
    wp_disable_rsu->set_hwid(hwid);

    // 256 is enough for the URL.
    char url[256];
    CHECK_GT(std::snprintf(url, sizeof(url), kRsuUrlFormat,
                           challenge_code.c_str(), hwid.c_str()),
             0);

    // Replace space with underscore.
    std::string url_string;
    base::ReplaceChars(url, " ", "_", &url_string);

    wp_disable_rsu->set_challenge_url(url_string);
    state_.set_allocated_wp_disable_rsu(wp_disable_rsu.release());
  }
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
WriteProtectDisableRsuStateHandler::GetNextStateCase(const RmadState& state) {
  if (!state.has_wp_disable_rsu()) {
    LOG(ERROR) << "RmadState missing |RSU| state.";
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_INVALID);
  }
  if (reboot_scheduled_) {
    return NextStateCaseWrapper(RMAD_ERROR_EXPECT_REBOOT);
  }

  // If factory mode is already enabled, we can transition to the next state
  // immediately.
  if (IsFactoryModeEnabled()) {
    json_store_->SetValue(kWpDisableMethod,
                          WpDisableMethod_Name(WpDisableMethod::RSU));
    // TODO(chenghan): Remove this.
    json_store_->SetValue(kWriteProtectDisableMethod,
                          static_cast<int>(WriteProtectDisableMethod::RSU));
    return NextStateCaseWrapper(RmadState::StateCase::kWpDisableComplete);
  }

  // Do RSU. If RSU succeeds, cr50 will cut off its connection with AP until the
  // next boot, so we need a reboot here for factory mode to take effect.
  if (!cr50_utils_->PerformRsu(state.wp_disable_rsu().unlock_code())) {
    LOG(ERROR) << "Incorrect unlock code.";
    return NextStateCaseWrapper(
        RMAD_ERROR_WRITE_PROTECT_DISABLE_RSU_CODE_INVALID);
  }

  // Inject rma-mode powerwash if it is not disabled.
  if (!IsPowerwashDisabled(working_dir_path_) &&
      !RequestPowerwash(working_dir_path_)) {
    LOG(ERROR) << "Failed to request powerwash";
    return NextStateCaseWrapper(RMAD_ERROR_POWERWASH_FAILED);
  }

  // Schedule a reboot after |kRebootDelay| seconds and return.
  timer_.Start(FROM_HERE, kRebootDelay, this,
               &WriteProtectDisableRsuStateHandler::Reboot);
  reboot_scheduled_ = true;
  return NextStateCaseWrapper(GetStateCase(), RMAD_ERROR_EXPECT_REBOOT,
                              AdditionalActivity::REBOOT);
}

bool WriteProtectDisableRsuStateHandler::IsFactoryModeEnabled() const {
  bool factory_mode_enabled = cr50_utils_->IsFactoryModeEnabled();
  int hwwp_status = 1;
  crossystem_utils_->GetHwwpStatus(&hwwp_status);
  VLOG(3) << "WriteProtectDisableRsuState: Cr50 factory mode: "
          << (factory_mode_enabled ? "enabled" : "disabled");
  VLOG(3) << "WriteProtectDisableRsuState: Hardware write protect"
          << hwwp_status;
  // Factory mode enabled should imply that HWWP is off. Check both just to be
  // extra sure.
  return factory_mode_enabled && (hwwp_status == 0);
}

void WriteProtectDisableRsuStateHandler::Reboot() {
  LOG(INFO) << "Rebooting after RSU";
  if (!power_manager_client_->Restart()) {
    LOG(ERROR) << "Failed to reboot";
  }
}

}  // namespace rmad
