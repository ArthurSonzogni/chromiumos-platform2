// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEARTD_DAEMON_SCAVENGER_H_
#define HEARTD_DAEMON_SCAVENGER_H_

#include <base/timer/timer.h>

#include "heartd/daemon/heartbeat_manager.h"

namespace heartd {

constexpr base::TimeDelta kScavengerPeriod = base::Hours(1);

class Scavenger {
 public:
  explicit Scavenger(base::OnceCallback<void()> quit_heartd_job,
                     HeartbeatManager* heartbeat_manager);
  Scavenger(const Scavenger&) = delete;
  Scavenger& operator=(const Scavenger&) = delete;
  ~Scavenger();

  // Start the scavenger task.
  void Start();

 private:
  // Cleanup task.
  void Cleanup();

 private:
  // Quit the heartd daemon.
  base::OnceCallback<void()> quit_heartd_job_;
  // Unowned pointer. Should outlive this instance.
  HeartbeatManager* const heartbeat_manager_;
  // The timer to run the periodic task.
  base::RepeatingTimer timer_;
};

}  // namespace heartd

#endif  // HEARTD_DAEMON_SCAVENGER_H_
