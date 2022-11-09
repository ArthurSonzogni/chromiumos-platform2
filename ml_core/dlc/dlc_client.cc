// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml_core/dlc/dlc_client.h"

#include <memory>
#include <utility>

#include <base/strings/strcat.h>
#include <dbus/bus.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <dlcservice/dbus-proxies.h>

namespace {
constexpr char kDlcId[] = "ml-core-internal";

class DlcClientImpl : public cros::DlcClient {
 public:
  ~DlcClientImpl() override = default;

  DlcClientImpl() : weak_factory_(this) {}

  void Initialize(
      base::OnceCallback<void(const base::FilePath&)> dlc_root_path_cb,
      base::OnceCallback<void(const std::string&)> error_cb) {
    dlc_root_path_cb_ = std::move(dlc_root_path_cb);
    error_cb_ = std::move(error_cb);
    LOG(INFO) << "Setting up DlcClient";

    dbus::Bus::Options opts;
    opts.bus_type = dbus::Bus::SYSTEM;
    bus_ = new dbus::Bus(std::move(opts));
    if (!bus_->Connect()) {
      LOG(ERROR) << "Failed to connect to system bus";
    }
    LOG(INFO) << "Connected to system bus";

    dlcservice_client_ =
        std::make_unique<org::chromium::DlcServiceInterfaceProxy>(bus_);

    base::WeakPtr<DlcClientImpl> weak_this = weak_factory_.GetWeakPtr();
    dlcservice_client_->RegisterDlcStateChangedSignalHandler(
        base::BindRepeating(&DlcClientImpl::OnDlcStateChanged, weak_this),
        base::BindOnce(&DlcClientImpl::OnDlcStateChangedConnect, weak_this));

    LOG(INFO) << "DlcClient setup complete";
  }

  void OnDlcStateChanged(const dlcservice::DlcState& dlc_state) {
    LOG(INFO) << "OnDlcStateChanged";

    if (dlc_state.id() != kDlcId) {
      return;
    }

    switch (dlc_state.state()) {
      case dlcservice::DlcState::INSTALLED:
        LOG(INFO) << "Successfully installed DLC " << kDlcId << " at "
                  << dlc_state.root_path();
        InvokeSuccessCb(base::FilePath(dlc_state.root_path()));
        break;
      case dlcservice::DlcState::INSTALLING:
        LOG(INFO) << static_cast<int>(dlc_state.progress() * 100)
                  << "% installing DLC: " << kDlcId;
        break;
      case dlcservice::DlcState::NOT_INSTALLED: {
        InvokeErrorCb(base::StrCat({"Failed to install DLC: ", kDlcId,
                                    " Error: ", dlc_state.last_error_code()}));
        break;
      }
      default:
        InvokeErrorCb(base::StrCat({"Unknown error when installing: ", kDlcId,
                                    " Error: ", dlc_state.last_error_code()}));
        break;
    }
  }

  void OnDlcStateChangedConnect(const std::string& interface,
                                const std::string& signal,
                                const bool success) {
    LOG(INFO) << "OnDlcStateChangedConnect";
    if (!success) {
      InvokeErrorCb(
          base::StrCat({"Error connecting ", interface, ". ", signal}));
    }
  }

  void InstallDlc() override {
    if (!bus_->IsConnected()) {
      InvokeErrorCb("Error calling dlcservice: DBus not connected");
      return;
    }

    dlcservice::DlcState dlc_state;
    brillo::ErrorPtr error;

    // Gets current dlc state.
    if (!dlcservice_client_->GetDlcState(kDlcId, &dlc_state, &error)) {
      if (error != nullptr) {
        InvokeErrorCb(
            base::StrCat({"Error calling dlcservice (code=", error->GetCode(),
                          "): ", error->GetMessage()}));
      } else {
        InvokeErrorCb("Error calling dlcservice: unknown");
      }
      return;
    }

    if (dlc_state.state() == dlcservice::DlcState::INSTALLED) {
      LOG(INFO) << "dlc " << kDlcId << " already installed at "
                << dlc_state.root_path();
      InvokeSuccessCb(base::FilePath(dlc_state.root_path()));
    } else {
      LOG(INFO) << "dlc " << kDlcId
                << " isn't installed, call dlc service to install it";

      if (!dlcservice_client_->InstallDlc(kDlcId, &error)) {
        if (error != nullptr) {
          InvokeErrorCb(
              base::StrCat({"Error calling dlcservice (code=", error->GetCode(),
                            "): ", error->GetMessage()}));
        } else {
          InvokeErrorCb("Error calling dlcservice: unknown");
        }
        // Return now in case more code follows later.
        return;
      }
    }
  }

  void InvokeSuccessCb(const base::FilePath& dlc_root_path) {
    if (dlc_root_path_cb_)
      std::move(dlc_root_path_cb_).Run(dlc_root_path);
  }

  void InvokeErrorCb(const std::string& error_msg) {
    if (error_cb_)
      std::move(error_cb_).Run(error_msg);
  }

  std::unique_ptr<org::chromium::DlcServiceInterfaceProxyInterface>
      dlcservice_client_;
  scoped_refptr<dbus::Bus> bus_;
  base::OnceCallback<void(const base::FilePath&)> dlc_root_path_cb_;
  base::OnceCallback<void(const std::string&)> error_cb_;
  base::WeakPtrFactory<DlcClientImpl> weak_factory_;
};

}  // namespace

namespace cros {

std::unique_ptr<DlcClient> DlcClient::Create(
    base::OnceCallback<void(const base::FilePath&)> dlc_root_path_cb,
    base::OnceCallback<void(const std::string&)> error_cb) {
  auto client = std::make_unique<DlcClientImpl>();
  client->Initialize(std::move(dlc_root_path_cb), std::move(error_cb));
  return client;
}

}  // namespace cros
