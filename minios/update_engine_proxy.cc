// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/update_engine_proxy.h"

#include <base/logging.h>
#include <brillo/message_loops/message_loop.h>

namespace minios {

namespace {
// Delay reboot after showing screen so user knows recovery has completed.
constexpr int kTimeTillReboot = 10;
}  // namespace

void UpdateEngineProxy::Init() {
  update_engine_proxy_.get()->RegisterStatusUpdateAdvancedSignalHandler(
      base::BindRepeating(&UpdateEngineProxy::OnStatusUpdateAdvancedSignal,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(
          &UpdateEngineProxy::OnStatusUpdateAdvancedSignalConnected,
          weak_ptr_factory_.GetWeakPtr()));
  return;
}

void UpdateEngineProxy::OnStatusUpdateAdvancedSignal(
    const update_engine::StatusResult& status_result) {
  if (!delegate_) {
    LOG(ERROR) << "Delegate not initialized, cannot show screens.";
    return;
  }
  delegate_->OnProgressChanged(status_result);
}

void UpdateEngineProxy::OnStatusUpdateAdvancedSignalConnected(
    const std::string& interface_name,
    const std::string& signal_name,
    bool success) {
  if (!success) {
    LOG(ERROR) << "OnStatusUpdateAdvancedSignalConnected not successful";
  }
}

void UpdateEngineProxy::TriggerReboot() {
  brillo::MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&UpdateEngineProxy::Reboot,
                     weak_ptr_factory_.GetWeakPtr()),
      base::TimeDelta::FromSeconds(kTimeTillReboot));
}

void UpdateEngineProxy::Reboot() {
  brillo::ErrorPtr error;
  if (!update_engine_proxy_->RebootIfNeeded(&error)) {
    LOG(ERROR) << "Could not reboot. ErrorCode=" << error->GetCode()
               << " ErrorMessage=" << error->GetMessage();
  }
}

bool UpdateEngineProxy::StartUpdate() {
  brillo::ErrorPtr error;
  if (!update_engine_proxy_.get()->AttemptUpdate("ForcedUpdate", "", &error)) {
    LOG(ERROR) << "Could not initiate forced update. "
               << "ErrorCode=" << error->GetCode()
               << " ErrorMessage=" << error->GetMessage();
    return false;
  }
  return true;
}

}  // namespace minios
