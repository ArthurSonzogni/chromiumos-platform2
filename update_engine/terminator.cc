// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/terminator.h"

#include <cstdlib>

namespace chromeos_update_engine {

volatile sig_atomic_t Terminator::exit_status_ = 1;  // default exit status
volatile sig_atomic_t Terminator::exit_blocked_ = 0;
volatile sig_atomic_t Terminator::exit_requested_ = 0;

void Terminator::Init() {
  exit_blocked_ = 0;
  exit_requested_ = 0;
  signal(SIGTERM, HandleSignal);
}

void Terminator::Init(int exit_status) {
  exit_status_ = exit_status;
  Init();
}

void Terminator::Exit() {
  exit(exit_status_);
}

void Terminator::HandleSignal(int signum) {
  if (exit_blocked_ == 0) {
    Exit();
  }
  exit_requested_ = 1;
}

ScopedTerminatorExitUnblocker::~ScopedTerminatorExitUnblocker() {
  Terminator::set_exit_blocked(false);
  if (Terminator::exit_requested()) {
    Terminator::Exit();
  }
}

}  // namespace chromeos_update_engine
