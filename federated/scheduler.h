// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_SCHEDULER_H_
#define FEDERATED_SCHEDULER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/sequenced_task_runner.h>
#include <base/memory/scoped_refptr.h>
#include <base/time/time.h>

#include "federated/device_status_monitor.h"
#include "federated/federated_session.h"

namespace federated {
class StorageManager;

class Scheduler {
 public:
  Scheduler(StorageManager* storage_manager, dbus::Bus* bus);
  Scheduler(const Scheduler&) = delete;
  Scheduler& operator=(const Scheduler&) = delete;
  ~Scheduler();

  // Creates a federated session and schedules job for each client.
  void Schedule();

 private:
  // Posts the TryToStartJobForSession task for the given session.
  void KeepSchedulingJobForSession(FederatedSession* const federated_session);

  // Tries to check-in the server and starts a federated task if training
  // conditions are satisfied, updates the session object if receiving response
  // from server and posts next try to task_runner_ with the updated session.
  void TryToStartJobForSession(FederatedSession* const federated_session);

  // Not owned
  StorageManager* const storage_manager_;

  // Device status monitor that answers whether training conditions are
  // satisfied.
  DeviceStatusMonitor device_status_monitor_;

  // Registered client sessions.
  std::vector<FederatedSession> sessions_;

  scoped_refptr<base::SequencedTaskRunner> task_runner_;
};
}  // namespace federated

#endif  // FEDERATED_SCHEDULER_H_
