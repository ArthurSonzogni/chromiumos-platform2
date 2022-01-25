// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_METHOD_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_METHOD_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <memory>

#include <base/files/file_path.h>

#include "rmad/utils/cr50_utils.h"

namespace rmad {

class WriteProtectDisableMethodStateHandler : public BaseStateHandler {
 public:
  explicit WriteProtectDisableMethodStateHandler(
      scoped_refptr<JsonStore> json_store);
  // Used to inject mock |cr50_utils_| for testing.
  WriteProtectDisableMethodStateHandler(scoped_refptr<JsonStore> json_store,
                                        std::unique_ptr<Cr50Utils> cr50_utils);

  ASSIGN_STATE(RmadState::StateCase::kWpDisableMethod);
  SET_REPEATABLE;

  RmadErrorCode InitializeState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

 protected:
  ~WriteProtectDisableMethodStateHandler() override = default;

 private:
  bool CheckVarsInStateFile() const;

  std::unique_ptr<Cr50Utils> cr50_utils_;
};

namespace fake {

// Nothing needs to be faked.
class FakeWriteProtectDisableMethodStateHandler
    : public WriteProtectDisableMethodStateHandler {
 public:
  explicit FakeWriteProtectDisableMethodStateHandler(
      scoped_refptr<JsonStore> json_store,
      const base::FilePath& working_dir_path);

 protected:
  ~FakeWriteProtectDisableMethodStateHandler() override = default;
};

}  // namespace fake

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_WRITE_PROTECT_DISABLE_METHOD_STATE_HANDLER_H_
