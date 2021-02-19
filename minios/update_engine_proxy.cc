// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/update_engine_proxy.h"

void UpdateEngineProxy::Init() {
  update_engine_proxy_.get()->RegisterStatusUpdateAdvancedSignalHandler(
      base::Bind(&UpdateEngineProxy::OnStatusUpdateAdvancedSignal,
                 weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&UpdateEngineProxy::OnStatusUpdateAdvancedSignalConnected,
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
