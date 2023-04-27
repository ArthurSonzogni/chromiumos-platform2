// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/utils/dlc_manager.h"

#include <cstdlib>
#include <utility>

#include <base/files/file_path.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
// NOLINTNEXTLINE(build/include_alpha) dbus-proxies.h needs dlcservice.pb.h
#include <dlcservice/dbus-proxies.h>

#include "diagnostics/cros_healthd/utils/dbus_utils.h"

namespace diagnostics {

DlcManager::DlcManager(
    org::chromium::DlcServiceInterfaceProxyInterface* dlcservice_proxy)
    : dlcservice_proxy_(dlcservice_proxy) {}

void DlcManager::GetBinaryRootPath(
    const std::string& dlc_id,
    base::OnceCallback<void(std::optional<std::string>)> root_path_cb) const {
  dlcservice_proxy_->GetObjectProxy()->WaitForServiceToBeAvailable(
      base::BindOnce(&DlcManager::HandleDlcServiceAvailableResponse,
                     weak_factory_.GetWeakPtr(), dlc_id,
                     std::move(root_path_cb)));
}

void DlcManager::HandleDlcServiceAvailableResponse(
    const std::string& dlc_id,
    base::OnceCallback<void(std::optional<std::string>)> root_path_cb,
    bool service_is_available) const {
  if (!service_is_available) {
    LOG(ERROR) << "DLC service is not available";
    std::move(root_path_cb).Run(std::nullopt);
    return;
  }

  auto [on_success, on_error] = SplitDbusCallback(base::BindOnce(
      &DlcManager::HandleDlcInstallResponse, weak_factory_.GetWeakPtr(), dlc_id,
      std::move(root_path_cb)));

  // The installation will complete immediately if the DLC is already installed.
  dlcservice_proxy_->InstallDlcAsync(dlc_id, std::move(on_success),
                                     std::move(on_error));
}

void DlcManager::HandleDlcInstallResponse(
    const std::string& dlc_id,
    base::OnceCallback<void(std::optional<std::string>)> root_path_cb,
    brillo::Error* err) const {
  if (err) {
    LOG(ERROR) << dlc_id << " install error: " << err->GetCode() + ", message: "
               << err->GetMessage();
    std::move(root_path_cb).Run(std::nullopt);
    return;
  }

  auto [on_success, on_error] = SplitDbusCallback(base::BindOnce(
      &DlcManager::HandleDlcStateResponse, weak_factory_.GetWeakPtr(), dlc_id,
      std::move(root_path_cb)));
  dlcservice_proxy_->GetDlcStateAsync(dlc_id, std::move(on_success),
                                      std::move(on_error));
}

void DlcManager::HandleDlcStateResponse(
    const std::string& dlc_id,
    base::OnceCallback<void(std::optional<std::string>)> root_path_cb,
    brillo::Error* err,
    const dlcservice::DlcState& state) const {
  if (err) {
    LOG(ERROR) << dlc_id << " access error: " << err->GetCode()
               << ", message: " << err->GetMessage();
    std::move(root_path_cb).Run(std::nullopt);
    return;
  }
  if (!state.is_verified()) {
    LOG(ERROR) << dlc_id << " verification error, last error: "
               << state.last_error_code();
    std::move(root_path_cb).Run(std::nullopt);
    return;
  }
  std::move(root_path_cb).Run(state.root_path());
}

}  // namespace diagnostics
