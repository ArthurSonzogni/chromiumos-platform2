// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_COMPLETE_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_COMPLETE_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <memory>

#include "rmad/utils/flashrom_utils.h"

namespace rmad {

class WriteProtectDisableCompleteStateHandler : public BaseStateHandler {
 public:
  explicit WriteProtectDisableCompleteStateHandler(
      scoped_refptr<JsonStore> json_store);
  // Used to inject mock |flashrom_utils_| for testing.
  explicit WriteProtectDisableCompleteStateHandler(
      scoped_refptr<JsonStore> json_store,
      std::unique_ptr<FlashromUtils> flashrom_utils);

  ASSIGN_STATE(RmadState::StateCase::kWpDisableComplete);
  SET_UNREPEATABLE;

  RmadErrorCode InitializeState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

 protected:
  ~WriteProtectDisableCompleteStateHandler() override = default;

 private:
  std::unique_ptr<FlashromUtils> flashrom_utils_;
};

namespace fake {

class FakeWriteProtectDisableCompleteStateHandler
    : public WriteProtectDisableCompleteStateHandler {
 public:
  FakeWriteProtectDisableCompleteStateHandler(
      scoped_refptr<JsonStore> json_store);

 protected:
  ~FakeWriteProtectDisableCompleteStateHandler() override = default;
};

}  // namespace fake

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_COMPLETE_STATE_HANDLER_H_
