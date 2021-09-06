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
#include <dlcservice/proto_bindings/dlcservice.pb.h>

#include "federated/device_status_monitor.h"
#include "federated/federated_session.h"

namespace org {
namespace chromium {
class DlcServiceInterfaceProxyInterface;
}  // namespace chromium
}  // namespace org

namespace federated {
class StorageManager;

class Scheduler {
 public:
  Scheduler(StorageManager* storage_manager, dbus::Bus* bus);
  Scheduler(const Scheduler&) = delete;
  Scheduler& operator=(const Scheduler&) = delete;
  ~Scheduler();

  // Tries to schedule tasks if the library dlc is already installed, otherwise
  // triggers dlc install and schedules tasks when it receives a DlcStateChanged
  // signal indicating the library dlc is installed.
  void Schedule();

 private:
  // Loads federated library from the given `dlc_root_path`, then for each
  // client, creates a federated session and schedules recurring jobs.
  void ScheduleInternal(const std::string& dlc_root_path);

  // Handles DlcStateChanged signals.
  void OnDlcStateChanged(const dlcservice::DlcState& dlc_state);

  // Posts the TryToStartJobForSession task for the given session.
  void KeepSchedulingJobForSession(FederatedSession* const federated_session);

  // Tries to check-in the server and starts a federated task if training
  // conditions are satisfied, updates the session object if receiving response
  // from server and posts next try to task_runner_ with the updated session.
  void TryToStartJobForSession(FederatedSession* const federated_session);

  // Registered client sessions.
  std::vector<FederatedSession> sessions_;

  // Not owned
  StorageManager* const storage_manager_;

  // Device status monitor that answers whether training conditions are
  // satisfied.
  DeviceStatusMonitor device_status_monitor_;

  std::unique_ptr<org::chromium::DlcServiceInterfaceProxyInterface>
      dlcservice_client_;

  scoped_refptr<base::SequencedTaskRunner> task_runner_;

  const base::WeakPtrFactory<Scheduler> weak_ptr_factory_;
};
}  // namespace federated

#endif  // FEDERATED_SCHEDULER_H_
