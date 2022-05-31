// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/write_protect_disable_complete_state_handler.h"

#include <memory>
#include <string>
#include <utility>

#include <base/logging.h>
#include <base/notreached.h>

#include "rmad/common/types.h"
#include "rmad/constants.h"
#include "rmad/metrics/metrics_utils.h"
#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/utils/fake_flashrom_utils.h"
#include "rmad/utils/flashrom_utils_impl.h"

namespace rmad {

namespace fake {

FakeWriteProtectDisableCompleteStateHandler::
    FakeWriteProtectDisableCompleteStateHandler(
        scoped_refptr<JsonStore> json_store)
    : WriteProtectDisableCompleteStateHandler(
          json_store, std::make_unique<FakeFlashromUtils>()) {}

}  // namespace fake

WriteProtectDisableCompleteStateHandler::
    WriteProtectDisableCompleteStateHandler(scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  flashrom_utils_ = std::make_unique<FlashromUtilsImpl>();
}

WriteProtectDisableCompleteStateHandler::
    WriteProtectDisableCompleteStateHandler(
        scoped_refptr<JsonStore> json_store,
        std::unique_ptr<FlashromUtils> flashrom_utils)
    : BaseStateHandler(json_store),
      flashrom_utils_(std::move(flashrom_utils)) {}

RmadErrorCode WriteProtectDisableCompleteStateHandler::InitializeState() {
  WpDisableMethod wp_disable_method;
  if (std::string wp_disable_method_name;
      !MetricsUtils::GetMetricsValue(json_store_, kWpDisableMethod,
                                     &wp_disable_method_name) ||
      !WpDisableMethod_Parse(wp_disable_method_name, &wp_disable_method)) {
    LOG(ERROR) << "Failed to get |wp_disable_method|";
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }

  switch (wp_disable_method) {
    case WpDisableMethod::UNKNOWN:
      // This should not happen.
      LOG(ERROR) << "WP disable method should not be UNKNOWN";
      return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
    case WpDisableMethod::SKIPPED:
      state_.mutable_wp_disable_complete()->set_action(
          WriteProtectDisableCompleteState::RMAD_WP_DISABLE_COMPLETE_NO_OP);
      break;

    case WpDisableMethod::RSU:
      state_.mutable_wp_disable_complete()->set_action(
          WriteProtectDisableCompleteState::RMAD_WP_DISABLE_COMPLETE_NO_OP);
      break;
    case WpDisableMethod::PHYSICAL_ASSEMBLE_DEVICE:
      state_.mutable_wp_disable_complete()->set_action(
          WriteProtectDisableCompleteState::
              RMAD_WP_DISABLE_COMPLETE_ASSEMBLE_DEVICE);
      break;
    case WpDisableMethod::PHYSICAL_KEEP_DEVICE_OPEN:
      state_.mutable_wp_disable_complete()->set_action(
          WriteProtectDisableCompleteState::
              RMAD_WP_DISABLE_COMPLETE_KEEP_DEVICE_OPEN);
      break;
    default:
      // We already enumerated all the enums.
      NOTREACHED();
  }

  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
WriteProtectDisableCompleteStateHandler::GetNextStateCase(
    const RmadState& state) {
  if (!state.has_wp_disable_complete()) {
    LOG(ERROR) << "RmadState missing |WP disable complete| state.";
    return NextStateCaseWrapper(RMAD_ERROR_REQUEST_INVALID);
  }

  if (!flashrom_utils_->DisableSoftwareWriteProtection()) {
    LOG(ERROR) << "Failed to disable software write protect";
    return NextStateCaseWrapper(RMAD_ERROR_WP_ENABLED);
  }

  return NextStateCaseWrapper(RmadState::StateCase::kUpdateRoFirmware);
}

}  // namespace rmad
