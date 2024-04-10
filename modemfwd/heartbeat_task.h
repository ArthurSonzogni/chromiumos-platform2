// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_HEARTBEAT_TASK_H_
#define MODEMFWD_HEARTBEAT_TASK_H_

#include <memory>

#include <base/memory/weak_ptr.h>
#include <base/timer/timer.h>

#include "modemfwd/metrics.h"
#include "modemfwd/modem.h"
#include "modemfwd/modem_helper.h"

namespace modemfwd {

class Delegate;

class HeartbeatTask {
 public:
  HeartbeatTask(Delegate* delegate,
                Modem* modem,
                Metrics* metrics,
                HeartbeatConfig config);

  virtual ~HeartbeatTask() = default;

  void Start();
  void Stop();

 private:
  void DoHealthCheck();

  Delegate* delegate_;
  Modem* modem_;
  Metrics* metrics_;
  HeartbeatConfig config_;

  int consecutive_heartbeat_failures_ = 0;

  base::RepeatingTimer timer_;

  base::WeakPtrFactory<HeartbeatTask> weak_ptr_factory_{this};
};

}  // namespace modemfwd

#endif  // MODEMFWD_HEARTBEAT_TASK_H_