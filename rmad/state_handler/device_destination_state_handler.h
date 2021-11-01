// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_DEVICE_DESTINATION_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_DEVICE_DESTINATION_STATE_HANDLER_H_

#include <memory>

#include "rmad/state_handler/base_state_handler.h"
#include "rmad/utils/cr50_utils.h"

namespace rmad {

class DeviceDestinationStateHandler : public BaseStateHandler {
 public:
  explicit DeviceDestinationStateHandler(scoped_refptr<JsonStore> json_store);
  // Used to inject mock |cr50_utils_| for testing.
  DeviceDestinationStateHandler(scoped_refptr<JsonStore> json_store,
                                std::unique_ptr<Cr50Utils> cr50_utils);

  ASSIGN_STATE(RmadState::StateCase::kDeviceDestination);
  SET_REPEATABLE;

  RmadErrorCode InitializeState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

 protected:
  ~DeviceDestinationStateHandler() override = default;

 private:
  // Store variables that can be used by other state handlers to make decisions.
  bool StoreVars() const;
  bool CanSkipHwwp() const;

  std::unique_ptr<Cr50Utils> cr50_utils_;
};

namespace fake {

class FakeDeviceDestinationStateHandler : public DeviceDestinationStateHandler {
 public:
  FakeDeviceDestinationStateHandler(scoped_refptr<JsonStore> json_store,
                                    const base::FilePath& working_dir_path);

 protected:
  ~FakeDeviceDestinationStateHandler() override = default;
};

}  // namespace fake

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_DEVICE_DESTINATION_STATE_HANDLER_H_
