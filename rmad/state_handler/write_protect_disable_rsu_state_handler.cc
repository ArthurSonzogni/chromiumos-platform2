// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/write_protect_disable_rsu_state_handler.h"

#include <cstdio>
#include <memory>
#include <string>
#include <utility>

#include <base/strings/string_util.h>

#include "rmad/system/power_manager_client_impl.h"
#include "rmad/utils/cr50_utils_impl.h"
#include "rmad/utils/crossystem_utils_impl.h"
#include "rmad/utils/dbus_utils.h"

#include <base/logging.h>

namespace rmad {

namespace {

// crossystem HWID property name.
constexpr char kHwidProperty[] = "hwid";
// crossystem HWWP property name.
constexpr char kHwwpProperty[] = "wpsw_cur";

// RSU server URL.
constexpr char kRsuUrlFormat[] =
    "https://www.google.com/chromeos/partner/console/"
    "cr50reset?challenge=%s&hwid=%s";

}  // namespace

WriteProtectDisableRsuStateHandler::WriteProtectDisableRsuStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  cr50_utils_ = std::make_unique<Cr50UtilsImpl>();
  crossystem_utils_ = std::make_unique<CrosSystemUtilsImpl>();
  power_manager_client_ =
      std::make_unique<PowerManagerClientImpl>(GetSystemBus());
}

WriteProtectDisableRsuStateHandler::WriteProtectDisableRsuStateHandler(
    scoped_refptr<JsonStore> json_store,
    std::unique_ptr<Cr50Utils> cr50_utils,
    std::unique_ptr<CrosSystemUtils> crossystem_utils,
    std::unique_ptr<PowerManagerClient> power_manager_client)
    : BaseStateHandler(json_store),
      cr50_utils_(std::move(cr50_utils)),
      crossystem_utils_(std::move(crossystem_utils)),
      power_manager_client_(std::move(power_manager_client)) {}

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
    crossystem_utils_->GetString(kHwidProperty, &hwid);
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
    return {.error = RMAD_ERROR_REQUEST_INVALID, .state_case = GetStateCase()};
  }

  // If factory mode is already enabled, we can transition to the next state
  // immediately.
  if (IsFactoryModeEnabled()) {
    return {.error = RMAD_ERROR_OK,
            .state_case = RmadState::StateCase::kWpDisableComplete};
  }

  // Do RSU. If RSU succeeds, cr50 will cut off its connection with AP until the
  // next boot, so we need a reboot here for factory mode to take effect.
  if (!cr50_utils_->PerformRsu(state.wp_disable_rsu().unlock_code())) {
    LOG(ERROR) << "Incorrect unlock code.";
    return {.error = RMAD_ERROR_WRITE_PROTECT_DISABLE_RSU_CODE_INVALID,
            .state_case = GetStateCase()};
  }

  // Schedule a reboot after |kRebootDelay| seconds and return.
  timer_.Start(FROM_HERE, kRebootDelay, this,
               &WriteProtectDisableRsuStateHandler::Reboot);
  return {.error = RMAD_ERROR_EXPECT_REBOOT, .state_case = GetStateCase()};
}

bool WriteProtectDisableRsuStateHandler::IsFactoryModeEnabled() const {
  bool factory_mode_enabled = cr50_utils_->IsFactoryModeEnabled();
  int hwwp_status = 1;
  crossystem_utils_->GetInt(kHwwpProperty, &hwwp_status);
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
