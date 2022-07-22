// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "federated/scheduler.h"

#include <optional>
#include <utility>

#include <base/bind.h>
#include <base/strings/stringprintf.h>
#include <base/threading/sequenced_task_runner_handle.h>
#include <base/time/time.h>
#include <brillo/errors/error.h>
#include <dbus/scoped_dbus_error.h>
#include <dlcservice/dbus-proxies.h>

#include "federated/device_status_monitor.h"
#include "federated/federated_library.h"
#include "federated/federated_metadata.h"
#include "federated/storage_manager.h"

namespace federated {
namespace {

constexpr char kServiceUri[] = "https://127.0.0.1:8791";
constexpr char kApiKey[] = "";
constexpr char kDlcId[] = "fcp";
constexpr char kFederatedComputationLibraryName[] = "libfcp.so";

void OnDBusSignalConnected(const std::string& interface,
                           const std::string& signal,
                           const bool success) {
  if (!success) {
    LOG(ERROR) << "Could not connect to signal " << signal << " on interface "
               << interface;
  }
}

}  // namespace

Scheduler::Scheduler(StorageManager* storage_manager,
                     std::unique_ptr<DeviceStatusMonitor> device_status_monitor,
                     dbus::Bus* bus)
    : storage_manager_(storage_manager),
      device_status_monitor_(std::move(device_status_monitor)),
      dlcservice_client_(
          std::make_unique<org::chromium::DlcServiceInterfaceProxy>(bus)),
      task_runner_(base::SequencedTaskRunnerHandle::Get()),
      weak_ptr_factory_(this) {}

void Scheduler::Schedule() {
  dlcservice::DlcState dlc_state;
  brillo::ErrorPtr error;
  // Gets current dlc state.
  if (!dlcservice_client_->GetDlcState(kDlcId, &dlc_state, &error)) {
    if (error != nullptr) {
      LOG(ERROR) << "Error calling dlcservice (code=" << error->GetCode()
                 << "): " << error->GetMessage();
    } else {
      LOG(ERROR) << "Error calling dlcservice: unknown";
    }
    return;
  }

  // If installed, calls `Schedule()` instantly, otherwise triggers dlc install
  // and waits for DlcStateChanged signals.
  if (dlc_state.state() == dlcservice::DlcState::INSTALLED) {
    DVLOG(1) << "dlc fcp is already installed, root path is "
             << dlc_state.root_path();
    ScheduleInternal(dlc_state.root_path());
  } else {
    DVLOG(1) << "dlc fcp isn't installed, call dlc service to install it";
    dlcservice_client_->RegisterDlcStateChangedSignalHandler(
        base::BindRepeating(&Scheduler::OnDlcStateChanged,
                            weak_ptr_factory_.GetWeakPtr()),
        base::BindOnce(&OnDBusSignalConnected));

    error.reset();
    if (!dlcservice_client_->InstallDlc(kDlcId, &error)) {
      if (error != nullptr) {
        LOG(ERROR) << "Error calling dlcservice (code=" << error->GetCode()
                   << "): " << error->GetMessage();
      } else {
        LOG(ERROR) << "Error calling dlcservice: unknown";
      }
    }
  }
}

void Scheduler::ScheduleInternal(const std::string& dlc_root_path) {
  DCHECK(!dlc_root_path.empty()) << "dlc_root_path is empty.";
  DCHECK(sessions_.empty()) << "Sessions are already scheduled.";

  const std::string lib_path = base::StringPrintf(
      "%s/%s", dlc_root_path.c_str(), kFederatedComputationLibraryName);

  DVLOG(1) << "lib_path is " << lib_path;
  auto* const federated_library = FederatedLibrary::GetInstance(lib_path);
  if (!federated_library->GetStatus().ok()) {
    LOG(ERROR) << "FederatedLibrary failed to initialized with error "
               << federated_library->GetStatus();
    return;
  }

  auto client_configs = GetClientConfig();

  // Pointers to elements of `sessions_` are passed to
  // KeepSchedulingJobForSession, which can be invalid if the capacity of
  // `sessions_` needs to be increased. Reserves the necessary capacity upfront.
  sessions_.reserve(client_configs.size());

  for (const auto& kv : client_configs) {
    sessions_.push_back(federated_library->CreateSession(
        kServiceUri, kApiKey, kv.second, device_status_monitor_.get()));
    KeepSchedulingJobForSession(&sessions_.back());
  }
}

void Scheduler::OnDlcStateChanged(const dlcservice::DlcState& dlc_state) {
  DVLOG(1) << "OnDlcStateChanged, dlc_state.id = " << dlc_state.id()
           << ", state = " << dlc_state.state();
  if (!sessions_.empty() || dlc_state.id() != kDlcId ||
      dlc_state.state() != dlcservice::DlcState::INSTALLED)
    return;

  DVLOG(1) << "dlc fcp is now installed, root path is "
           << dlc_state.root_path();
  ScheduleInternal(dlc_state.root_path());
}

void Scheduler::KeepSchedulingJobForSession(
    FederatedSession* const federated_session) {
  task_runner_->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&Scheduler::TryToStartJobForSession,
                     base::Unretained(this), federated_session),
      federated_session->next_retry_delay());
}

void Scheduler::TryToStartJobForSession(
    FederatedSession* const federated_session) {
  DVLOG(1) << "In TryToStartJobForSession, session name is "
           << federated_session->GetSessionName();
  federated_session->ResetRetryDelay();
  if (!device_status_monitor_->TrainingConditionsSatisfied()) {
    DVLOG(1) << "Device is not in a good condition for training now.";
    KeepSchedulingJobForSession(federated_session);
    return;
  }

  std::optional<ExampleDatabase::Iterator> example_iterator =
      storage_manager_->GetExampleIterator(federated_session->GetSessionName());
  if (!example_iterator.has_value()) {
    DVLOG(1) << "Client " << federated_session->GetSessionName()
             << " failed to prepare examples.";
    KeepSchedulingJobForSession(federated_session);
    return;
  }

  federated_session->RunPlan(std::move(example_iterator.value()));

  // Posts next task.
  KeepSchedulingJobForSession(federated_session);
}

}  // namespace federated
