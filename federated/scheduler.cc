// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "federated/scheduler.h"

#include <base/bind.h>
#include <base/threading/sequenced_task_runner_handle.h>

#include "federated/device_status_monitor.h"
#include "federated/federated_metadata.h"
#include "federated/storage_manager.h"

namespace federated {
namespace {

// TODO(alanlxl): discussion required about the default window.
constexpr base::TimeDelta kDefaultRetryWindow =
    base::TimeDelta::FromSeconds(60 * 5);

}  // namespace

Scheduler::Scheduler(StorageManager* storage_manager, dbus::Bus* bus)
    : storage_manager_(storage_manager),
      device_status_monitor_(bus),
      registered_clients_(GetClientNames()),
      task_runner_(base::SequencedTaskRunnerHandle::Get()) {}

// TODO(alanlxl): create a destructor or finalize method that deletes examples
//                from the database.

void Scheduler::Schedule() {
  for (const auto& client_name : registered_clients_) {
    PostDelayedTask(client_name, kDefaultRetryWindow);
  }
}

void Scheduler::PostDelayedTask(const std::string& client_name,
                                const base::TimeDelta& delay) {
  task_runner_->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&Scheduler::TryToStartJobForClient, base::Unretained(this),
                     client_name),
      delay);
}

void Scheduler::TryToStartJobForClient(const std::string& client_name) {
  base::TimeDelta next_retry_delay = kDefaultRetryWindow;
  if (!device_status_monitor_.TrainingConditionsSatisfied()) {
    DVLOG(1) << "Device is not in a good condition for training now.";
    PostDelayedTask(client_name, next_retry_delay);
    return;
  }

  const base::Optional<ExampleDatabase::Iterator> examples =
      storage_manager_->GetExampleIterator(client_name);
  if (!examples.has_value()) {
    DVLOG(1) << "Client " << client_name << " failed to prepare examples.";
    PostDelayedTask(client_name, next_retry_delay);
    return;
  }

  // TODO(alanlxl): the real federated task happens here.
  // `next_retry_delay` should be updated according to the response from the
  // server.

  // Posts next task.
  PostDelayedTask(client_name, next_retry_delay);
}

}  // namespace federated
