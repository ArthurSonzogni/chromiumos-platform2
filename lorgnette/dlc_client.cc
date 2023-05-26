// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/dlc_client.h"

#include <base/functional/bind.h>
#include <brillo/errors/error.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>

#include <string>

#include "base/files/file_path.h"
#include "base/strings/strcat.h"
#include "base/task/single_thread_task_runner.h"

namespace lorgnette {
constexpr char kDlcId[] = "sane-backends-extras-dlc";

void DlcClient::Init(
    std::unique_ptr<org::chromium::DlcServiceInterfaceProxyInterface>
        dlcservice_client) {
  dlcservice_client_ = std::move(dlcservice_client);
  base::WeakPtr<DlcClient> weak_ptr = weak_factory_.GetWeakPtr();
  LOG(INFO) << "Setting up DlcClient";
  dlcservice_client_->RegisterDlcStateChangedSignalHandler(
      base::BindRepeating(&DlcClient::OnDlcStateChanged, weak_ptr),
      base::BindOnce(&DlcClient::OnDlcStateChangedConnect, weak_ptr));
}

void DlcClient::SetCallbacks(
    base::OnceCallback<void(const base::FilePath&)> success_cb,
    base::OnceCallback<void(const std::string&)> failure_cb) {
  success_cb_ = std::move(success_cb);
  failure_cb_ = std::move(failure_cb);
}

void DlcClient::OnDlcStateChanged(const dlcservice::DlcState& dlc_state) {
  if (dlc_state.id() != kDlcId) {
    return;
  }

  switch (dlc_state.state()) {
    case dlcservice::DlcState::INSTALLED:
      InvokeSuccessCb(dlc_state.root_path());
      break;
    case dlcservice::DlcState::INSTALLING:
      break;
    case dlcservice::DlcState::NOT_INSTALLED: {
      std::string err = base::StrCat({"Failed to install DLC: ", kDlcId,
                                      " Error: ", dlc_state.last_error_code()});
      InvokeErrorCb(err);
      break;
    }
    default:
      std::string err = base::StrCat({"Unknown error when installing: ", kDlcId,
                                      " Error: ", dlc_state.last_error_code()});
      InvokeErrorCb(err);
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
    InvokeErrorCb(err);
  }
}

void DlcClient::InstallDlc() {
  brillo::ErrorPtr error;
  dlcservice::InstallRequest install_request;
  install_request.set_id(kDlcId);
  dlcservice_client_->InstallAsync(
      install_request,
      base::BindRepeating(&DlcClient::OnDlcInstall, base::Unretained(this)),
      base::BindRepeating(&DlcClient::OnDlcInstallError,
                          base::Unretained(this)));
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

void DlcClient::InvokeSuccessCb(std::string dlc_root_path) {
  if (success_cb_) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(success_cb_), base::FilePath(dlc_root_path)));
  }
}

void DlcClient::OnDlcInstallError(brillo::Error* error) {
  InvokeErrorCb(error->GetMessage());
}

void DlcClient::InvokeErrorCb(const std::string& error_msg) {
  if (failure_cb_) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(failure_cb_), error_msg));
  }
}

}  // namespace lorgnette
