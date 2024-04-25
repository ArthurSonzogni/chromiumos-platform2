// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/dlc_client.h"

#include <base/functional/bind.h>
#include <brillo/errors/error.h>
#include <chromeos/constants/lorgnette_dlc.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>

#include <string>

#include "base/files/file_path.h"
#include "base/strings/strcat.h"
#include "base/task/single_thread_task_runner.h"

namespace lorgnette {

void DlcClient::Init(
    std::unique_ptr<org::chromium::DlcServiceInterfaceProxyInterface>
        dlcservice_client) {
  dlcservice_client_ = std::move(dlcservice_client);
  base::WeakPtr<DlcClient> weak_ptr = weak_factory_.GetWeakPtr();
  LOG(INFO) << "Setting up DlcClient";
  dlcservice_client_->RegisterDlcStateChangedSignalHandler(
      base::BindRepeating(&DlcClient::OnDlcStateChanged, weak_ptr),
      base::BindOnce(&DlcClient::OnDlcStateChangedConnect, weak_ptr));
  supported_dlc_ids_ = std::set<std::string>({kSaneBackendsPfuDlcId});
}

void DlcClient::SetCallbacks(
    base::RepeatingCallback<void(const std::string&, const base::FilePath&)>
        success_cb,
    base::RepeatingCallback<void(const std::string&, const std::string&)>
        failure_cb) {
  success_cb_ = std::move(success_cb);
  failure_cb_ = std::move(failure_cb);
}

void DlcClient::OnDlcStateChanged(const dlcservice::DlcState& dlc_state) {
  if (supported_dlc_ids_.find(dlc_state.id()) == supported_dlc_ids_.end()) {
    return;
  }

  switch (dlc_state.state()) {
    case dlcservice::DlcState::INSTALLED:
      InvokeSuccessCb(dlc_state.id(), dlc_state.root_path());
      break;
    case dlcservice::DlcState::INSTALLING:
      break;
    case dlcservice::DlcState::NOT_INSTALLED: {
      std::string err = base::StrCat({"Failed to install DLC: ", dlc_state.id(),
                                      " Error: ", dlc_state.last_error_code()});
      InvokeErrorCb(dlc_state.id(), err);
      break;
    }
    default:
      std::string err =
          base::StrCat({"Unknown error when installing: ", dlc_state.id(),
                        " Error: ", dlc_state.last_error_code()});
      InvokeErrorCb(dlc_state.id(), err);
      break;
  }
}

void DlcClient::OnDlcStateChangedConnect(const std::string& interface,
                                         const std::string& signal,
                                         bool success) {
  LOG(INFO) << "OnDlcStateChangedConnect (" << interface << ":" << signal
            << "): " << (success ? "true" : "false");
  if (!success) {
    std::string err =
        base::StrCat({"Error connecting ", interface, ". ", signal});
    InvokeErrorCb(/*dlc_id=*/"", err);
  }
}

void DlcClient::InstallDlc(const std::set<std::string>& dlc_ids) {
  for (const std::string& id : dlc_ids) {
    dlcservice::InstallRequest install_request;
    install_request.set_id(id);
    dlcservice_client_->InstallAsync(
        install_request,
        base::BindRepeating(&DlcClient::OnDlcInstall, base::Unretained(this)),
        base::BindRepeating(&DlcClient::OnDlcInstallError,
                            base::Unretained(this), install_request.id()));
  }
}

std::optional<std::string> DlcClient::GetRootPath(const std::string& in_id,
                                                  std::string* out_error) {
  DCHECK(out_error);
  dlcservice::DlcState state;
  brillo::ErrorPtr error;

  if (!dlcservice_client_->GetDlcState(in_id, &state, &error)) {
    if (error) {
      *out_error = "Error calling dlcservice (code=" + error->GetCode() +
                   "): " + error->GetMessage();
    } else {
      *out_error = "Error calling dlcservice: unknown";
    }
    return std::nullopt;
  }
  if (state.state() != dlcservice::DlcState::INSTALLED) {
    *out_error = base::StrCat({in_id, " was not installed, its state is: ",
                               std::to_string(state.state()),
                               " with last error: ", state.last_error_code()});
    return std::nullopt;
  }
  return state.root_path();
}

void DlcClient::OnDlcInstall() {
  return;
}

void DlcClient::InvokeSuccessCb(const std::string& dlc_id,
                                std::string dlc_root_path) {
  if (success_cb_) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(success_cb_, dlc_id, base::FilePath(dlc_root_path)));
  }
}

void DlcClient::OnDlcInstallError(const std::string& dlc_id,
                                  brillo::Error* error) {
  InvokeErrorCb(dlc_id, error->GetMessage());
}

void DlcClient::InvokeErrorCb(const std::string& dlc_id,
                              const std::string& error_msg) {
  if (failure_cb_) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(failure_cb_, dlc_id, error_msg));
  }
}

}  // namespace lorgnette
