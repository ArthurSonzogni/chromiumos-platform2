// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_WIPE_SELECTION_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_WIPE_SELECTION_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <memory>

#include <base/files/file_path.h>

#include "rmad/utils/cr50_utils.h"

namespace rmad {

class WipeSelectionStateHandler : public BaseStateHandler {
 public:
  explicit WipeSelectionStateHandler(scoped_refptr<JsonStore> json_store);
  // Used to inject mock |cr50_utils_| for testing.
  WipeSelectionStateHandler(scoped_refptr<JsonStore> json_store,
                            std::unique_ptr<Cr50Utils> cr50_utils);

  ASSIGN_STATE(RmadState::StateCase::kWipeSelection);
  SET_REPEATABLE;

  RmadErrorCode InitializeState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

  // Disable transition at boot.
  GetNextStateCaseReply TryGetNextStateCaseAtBoot() override {
    return NextStateCaseWrapper(RMAD_ERROR_TRANSITION_FAILED);
  }

 protected:
  ~WipeSelectionStateHandler() override = default;

 private:
  bool InitializeVarsFromStateFile();

  std::unique_ptr<Cr50Utils> cr50_utils_;

  bool wp_disable_required_;
  bool ccd_blocked_;
};

namespace fake {

// Nothing needs to be faked.
class FakeWipeSelectionStateHandler : public WipeSelectionStateHandler {
 public:
  FakeWipeSelectionStateHandler(scoped_refptr<JsonStore> json_store,
                                const base::FilePath& working_dir_path);

 protected:
  ~FakeWipeSelectionStateHandler() override = default;
};

}  // namespace fake

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_WIPE_SELECTION_STATE_HANDLER_H_
