// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_VERIFY_RSU_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_VERIFY_RSU_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <memory>

namespace rmad {

class Cr50Utils;
class CrosSystemUtils;

class VerifyRsuStateHandler : public BaseStateHandler {
 public:
  explicit VerifyRsuStateHandler(scoped_refptr<JsonStore> json_store);
  // Used to inject mock |cr50_utils_| for testing.
  VerifyRsuStateHandler(scoped_refptr<JsonStore> json_store,
                        std::unique_ptr<Cr50Utils> cr50_utils,
                        std::unique_ptr<CrosSystemUtils> crossystem_utils);

  ASSIGN_STATE(RmadState::StateCase::kVerifyRsu);
  SET_REPEATABLE;

  RmadErrorCode InitializeState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

 protected:
  ~VerifyRsuStateHandler() override = default;

 private:
  bool VerifyFactoryModeEnabled() const;

  std::unique_ptr<Cr50Utils> cr50_utils_;
  std::unique_ptr<CrosSystemUtils> crossystem_utils_;
};

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_VERIFY_RSU_STATE_HANDLER_H_
