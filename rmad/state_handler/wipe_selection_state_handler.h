// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_WIPE_SELECTION_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_WIPE_SELECTION_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <memory>

#include <base/files/file_path.h>

#include "rmad/utils/cr50_utils.h"
#include "rmad/utils/crossystem_utils.h"

namespace rmad {

class WipeSelectionStateHandler : public BaseStateHandler {
 public:
  explicit WipeSelectionStateHandler(
      scoped_refptr<JsonStore> json_store,
      scoped_refptr<DaemonCallback> daemon_callback);
  // Used to inject mock |cr50_utils_| for testing.
  WipeSelectionStateHandler(scoped_refptr<JsonStore> json_store,
                            scoped_refptr<DaemonCallback> daemon_callback,
                            std::unique_ptr<Cr50Utils> cr50_utils,
                            std::unique_ptr<CrosSystemUtils> crossystem_utils);

  ASSIGN_STATE(RmadState::StateCase::kWipeSelection);
  SET_REPEATABLE;

  RmadErrorCode InitializeState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

 protected:
  ~WipeSelectionStateHandler() override = default;

 private:
  bool InitializeVarsFromStateFile();

  std::unique_ptr<Cr50Utils> cr50_utils_;
  std::unique_ptr<CrosSystemUtils> crossystem_utils_;

  bool wp_disable_required_;
  bool ccd_blocked_;
};

namespace fake {

// Nothing needs to be faked.
class FakeWipeSelectionStateHandler : public WipeSelectionStateHandler {
 public:
  FakeWipeSelectionStateHandler(scoped_refptr<JsonStore> json_store,
                                scoped_refptr<DaemonCallback> daemon_callback,
                                const base::FilePath& working_dir_path);

 protected:
  ~FakeWipeSelectionStateHandler() override = default;
};

}  // namespace fake

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_WIPE_SELECTION_STATE_HANDLER_H_
