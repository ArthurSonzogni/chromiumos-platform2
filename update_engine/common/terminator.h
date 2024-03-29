// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_TERMINATOR_H_
#define UPDATE_ENGINE_COMMON_TERMINATOR_H_

#include <signal.h>

#include <gtest/gtest_prod.h>  // for FRIEND_TEST

namespace chromeos_update_engine {

// A class allowing graceful delayed exit.
class Terminator {
 public:
  // Initializes the terminator and sets up signal handlers.
  static void Init();
  static void Init(int exit_status);

  // Terminates the current process.
  static void Exit();

  // Set to true if the terminator should block termination requests in an
  // attempt to block exiting.
  static void set_exit_blocked(bool block) { exit_blocked_ = block ? 1 : 0; }
  static bool exit_blocked() { return exit_blocked_ != 0; }

  // Returns true if the system is trying to terminate the process, false
  // otherwise. Returns true only if exit was blocked when the termination
  // request arrived.
  static bool exit_requested() { return exit_requested_ != 0; }

 private:
  FRIEND_TEST(TerminatorTest, HandleSignalTest);
  FRIEND_TEST(TerminatorDeathTest, ScopedTerminatorExitUnblockerExitTest);

  // The signal handler.
  static void HandleSignal(int signum);

  static volatile sig_atomic_t exit_status_;
  static volatile sig_atomic_t exit_blocked_;
  static volatile sig_atomic_t exit_requested_;
};

class ScopedTerminatorExitUnblocker {
 public:
  ~ScopedTerminatorExitUnblocker();
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_TERMINATOR_H_
