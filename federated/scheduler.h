// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_SCHEDULER_H_
#define FEDERATED_SCHEDULER_H_

#include <memory>
#include <string>
#include <unordered_set>

#include <base/sequenced_task_runner.h>
#include <base/memory/scoped_refptr.h>
#include <base/time/time.h>

#include "federated/device_status_monitor.h"

namespace federated {
class StorageManager;

class Scheduler {
 public:
  Scheduler(StorageManager* storage_manager, dbus::Bus* bus);
  Scheduler(const Scheduler&) = delete;
  Scheduler& operator=(const Scheduler&) = delete;

  virtual ~Scheduler() = default;

  // For each client, posts a delayed task with default retry window.
  void Schedule();

 private:
  // Posts the TryToStartJobForClient task for client with delay.
  void PostDelayedTask(const std::string& client_name,
                       const base::TimeDelta& delay);

  // Tries to check-in the server and starts a federated task if training
  // conditions are satisfied, uses the retry-window from the server as the next
  // delay and posts the next delayed try task. Otherwise posts the next delayed
  // try task with the default retry window.
  void TryToStartJobForClient(const std::string& client_name);

  // Obtained from daemon.cc, should not delete it.
  StorageManager* storage_manager_;

  // Device status monitor that answers whether training conditions are
  // satisfied.
  DeviceStatusMonitor device_status_monitor_;

  // Registered clients.
  // TODO(alanlxl): need to be a map from client to its config.
  std::unordered_set<std::string> registered_clients_;

  scoped_refptr<base::SequencedTaskRunner> task_runner_;
};
}  // namespace federated

#endif  // FEDERATED_SCHEDULER_H_
