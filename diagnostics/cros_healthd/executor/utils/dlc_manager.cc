// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/utils/dlc_manager.h"

#include <cstdlib>
#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
// NOLINTNEXTLINE(build/include_alpha) dbus-proxies.h needs dlcservice.pb.h
#include <dlcservice/dbus-proxies.h>
#include <mojo/public/cpp/bindings/callback_helpers.h>

namespace diagnostics {

DlcManager::DlcManager(
    org::chromium::DlcServiceInterfaceProxyInterface* dlcservice_proxy)
    : dlcservice_proxy_(dlcservice_proxy) {}

void DlcManager::Initialize() {
  if (initialize_state_ != InitializeState::kNotInitialized) {
    LOG(ERROR) << "DLC service is initializing or initialized";
    return;
  }

  initialize_state_ = InitializeState::kInitializing;
  dlcservice_proxy_->GetObjectProxy()->WaitForServiceToBeAvailable(
      base::BindOnce(&DlcManager::RegisterDlcStateChangedEvents,
                     weak_factory_.GetWeakPtr()));
}

void DlcManager::RegisterDlcStateChangedEvents(bool service_is_available) {
  if (!service_is_available) {
    LOG(ERROR) << "DLC service is not available";
    initialize_state_ = InitializeState::kNotInitialized;
    pending_initialized_callbacks_.clear();
    return;
  }

  dlcservice_proxy_->RegisterDlcStateChangedSignalHandler(
      base::BindRepeating(&DlcManager::OnDlcStateChanged,
                          weak_factory_.GetWeakPtr()),
      base::BindOnce(&DlcManager::HandleRegisterDlcStateChangedResponse,
                     weak_factory_.GetWeakPtr()));
}

void DlcManager::HandleRegisterDlcStateChangedResponse(
    const std::string& interface,
    const std::string& signal,
    const bool success) {
  if (!success) {
    LOG(ERROR) << "Failed to register DLC state changed signal ("
               << interface << ":" << signal << ")";
    initialize_state_ = InitializeState::kNotInitialized;
    pending_initialized_callbacks_.clear();
    return;
  }

  initialize_state_ = InitializeState::kInitialized;
  for (auto& cb : pending_initialized_callbacks_) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(cb)));
  }
  pending_initialized_callbacks_.clear();
}

void DlcManager::GetBinaryRootPath(const std::string& dlc_id,
                                   DlcRootPathCallback root_path_cb) {
  auto timeout_cb = std::make_unique<base::CancelableOnceClosure>(
      base::BindOnce(&DlcManager::HandleDlcRootPathCallbackTimeout,
                     weak_factory_.GetWeakPtr(), dlc_id));
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, timeout_cb->callback(), kGetDlcRootPathTimeout);

  // The |timeout_cb| will be canceled when the |root_path_cb| is invoked.
  pending_callbacks_map_[dlc_id].emplace_back(std::move(root_path_cb),
                                              std::move(timeout_cb));
  WaitForInitialized(mojo::WrapCallbackWithDefaultInvokeIfNotRun(base::BindOnce(
      &DlcManager::InstallDlc, weak_factory_.GetWeakPtr(), dlc_id)));
}

void DlcManager::WaitForInitialized(base::OnceClosure on_initialized) {
  switch (initialize_state_) {
    case InitializeState::kNotInitialized:
      pending_initialized_callbacks_.push_back(std::move(on_initialized));
      Initialize();
      break;
    case InitializeState::kInitializing:
      pending_initialized_callbacks_.push_back(std::move(on_initialized));
      break;
    case InitializeState::kInitialized:
      std::move(on_initialized).Run();
      break;
  }
}

void DlcManager::InstallDlc(const std::string& dlc_id) {
  if (initialize_state_ != InitializeState::kInitialized) {
    InvokeRootPathCallbacks(dlc_id, std::nullopt);
    return;
  }

  // Even if the DLC is installed, we can receive a state change event after the
  // installation is complete.
  dlcservice::InstallRequest install_request;
  install_request.set_id(dlc_id);
  dlcservice_proxy_->InstallAsync(
      install_request, /*success_callback=*/base::DoNothing(),
      base::BindOnce(&DlcManager::HandleDlcInstallError,
                     weak_factory_.GetWeakPtr(), dlc_id));
}

void DlcManager::HandleDlcInstallError(const std::string& dlc_id,
                                       brillo::Error* err) {
  if (err) {
    LOG(ERROR) << "DLC installation error (" << dlc_id
               << "): " << err->GetCode() + ", message: " << err->GetMessage();
  }
  InvokeRootPathCallbacks(dlc_id, std::nullopt);
}

void DlcManager::OnDlcStateChanged(const dlcservice::DlcState& state) {
  // Skipped state changed if there are no pending callbacks.
  if (!pending_callbacks_map_.contains(state.id())) {
    return;
  }

  switch (state.state()) {
    case dlcservice::DlcState::INSTALLED:
      InvokeRootPathCallbacks(state.id(), base::FilePath(state.root_path()));
      break;
    case dlcservice::DlcState::INSTALLING:
      break;
    case dlcservice::DlcState::NOT_INSTALLED:
    default:
      LOG(ERROR) << "DLC installation error (" << state.id()
                 << "), error: " << state.last_error_code();
      InvokeRootPathCallbacks(state.id(), std::nullopt);
      break;
  }
}

void DlcManager::InvokeRootPathCallbacks(
    const std::string& dlc_id, std::optional<base::FilePath> root_path) {
  const auto iter = pending_callbacks_map_.find(dlc_id);
  if (iter == pending_callbacks_map_.end()) {
    return;
  }

  // Invokes all root path callbacks and cancels all timeout callbacks when the
  // installation completes.
  for (auto& [root_path_cb, timeout_cb] : iter->second) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(root_path_cb), root_path));
    timeout_cb->Cancel();
  }
  pending_callbacks_map_.erase(iter);
}

void DlcManager::HandleDlcRootPathCallbackTimeout(const std::string& dlc_id) {
  // Timeout callback should be existing when the timeout function is triggered.
  // We can find non-empty callbacks with the key |dlc_id| in
  // |pending_callbacks_map_|.
  const auto iter = pending_callbacks_map_.find(dlc_id);
  CHECK(iter != pending_callbacks_map_.end());
  auto& pending_callbacks = iter->second;
  CHECK(!pending_callbacks.empty());

  LOG(ERROR) << "DLC timeout error (" << dlc_id << ")";
  // Invokes the earliest root path callback with null result, which is the
  // first one in |pending_callbacks|.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(std::move(pending_callbacks[0].first), std::nullopt));
  pending_callbacks.erase(pending_callbacks.begin());

  if (pending_callbacks.empty()) {
    pending_callbacks_map_.erase(iter);
  }
}

}  // namespace diagnostics
