// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_MINIOS_H_
#define MINIOS_MINIOS_H_

#include <memory>

#include "minios/minios_interface.h"
#include "minios/network_manager_interface.h"
#include "minios/process_manager.h"
#include "minios/screen_controller.h"
#include "minios/update_engine_proxy.h"

namespace minios {

extern const char kDebugConsole[];
extern const char kLogFile[];

class MiniOs : public MiniOsInterface {
 public:
  explicit MiniOs(std::shared_ptr<UpdateEngineProxy> update_engine_proxy,
                  std::shared_ptr<NetworkManagerInterface> network_manager);
  virtual ~MiniOs() = default;

  // Runs the miniOS flow.
  virtual int Run();

  // `MiniOsInterface` overrides.
  bool GetState(State* state_out, brillo::ErrorPtr* error) override;

 private:
  MiniOs(const MiniOs&) = delete;
  MiniOs& operator=(const MiniOs&) = delete;

  // The current state of MiniOs.
  State state_;

  std::shared_ptr<UpdateEngineProxy> update_engine_proxy_;
  std::shared_ptr<NetworkManagerInterface> network_manager_;

  ProcessManager process_manager_;
  std::shared_ptr<DrawInterface> draw_utils_;
  ScreenController screens_controller_;
};

}  // namespace minios

#endif  // MINIOS_MINIOS_H__
