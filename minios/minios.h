// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_MINIOS_H_
#define MINIOS_MINIOS_H_

#include "minios/minios_interface.h"
#include "minios/screens.h"

namespace minios {

extern const char kDebugConsole[];
extern const char kLogFile[];

class MiniOs : public MiniOsInterface {
 public:
  MiniOs() = default;
  ~MiniOs() = default;

  // Runs the miniOS flow.
  virtual int Run();

  // MiniOsInterface overrides.
  bool GetState(State* state_out, brillo::ErrorPtr* error) override;

 private:
  MiniOs(const MiniOs&) = delete;
  MiniOs& operator=(const MiniOs&) = delete;

  // The current state of MiniOs.
  State state_;

  ProcessManager process_manager_;
  screens::Screens screens_{&process_manager_};
};

}  // namespace minios

#endif  // MINIOS_MINIOS_H__
