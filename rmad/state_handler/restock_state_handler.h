// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_RESTOCK_STATE_HANDLER_H_
#define RMAD_STATE_HANDLER_RESTOCK_STATE_HANDLER_H_

#include "rmad/state_handler/base_state_handler.h"

#include <memory>

#include <base/files/file_path.h>
#include <base/timer/timer.h>

#include "rmad/system/power_manager_client.h"

namespace rmad {

class RestockStateHandler : public BaseStateHandler {
 public:
  // Wait for 5 seconds before shutting down.
  static constexpr base::TimeDelta kShutdownDelay =
      base::TimeDelta::FromSeconds(5);

  explicit RestockStateHandler(scoped_refptr<JsonStore> json_store);
  // Used to inject mocked |power_manager_client_| for testing.
  RestockStateHandler(scoped_refptr<JsonStore> json_store,
                      std::unique_ptr<PowerManagerClient> power_manager_client);

  ASSIGN_STATE(RmadState::StateCase::kRestock);
  SET_REPEATABLE;

  RmadErrorCode InitializeState() override;
  GetNextStateCaseReply GetNextStateCase(const RmadState& state) override;

 protected:
  ~RestockStateHandler() override = default;

 private:
  void Shutdown();

  std::unique_ptr<PowerManagerClient> power_manager_client_;
  base::OneShotTimer timer_;
};

namespace fake {

class FakeRestockStateHandler : public RestockStateHandler {
 public:
  FakeRestockStateHandler(scoped_refptr<JsonStore> json_store,
                          const base::FilePath& working_dir_path);

 protected:
  ~FakeRestockStateHandler() override = default;
};

}  // namespace fake

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_RESTOCK_STATE_HANDLER_H_
