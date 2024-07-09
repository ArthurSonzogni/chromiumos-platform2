// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml_core/dlc/dlc_client.h"

#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/location.h>
#include <base/sequence_checker.h>
#include <base/strings/strcat.h>
#include <dbus/bus.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <dlcservice/dbus-constants.h>
#include <dlcservice/dbus-proxies.h>

#include "ml_core/dlc/dlc_ids.h"
#include "ml_core/dlc/dlc_metrics.h"

namespace {

// The first install attempt will take place at t=0.
// The nth retry will take place at t=kBaseDelay*2^n.
// E.g. {0, 2*kBaseDelay, 4*kBaseDelay, 8*kBaseDelay, ...}
constexpr base::TimeDelta kBaseDelay = base::Seconds(1);
constexpr int kMaxInstallAttempts = 8;

// Timeout for each install attempt.
constexpr int kDlcInstallTimeout = 50000;

class DlcClientImpl : public cros::DlcClient {
 public:
  explicit DlcClientImpl(const std::string& dlc_id)
      : dlc_id_(dlc_id),
        task_runner_(base::SequencedTaskRunner::GetCurrentDefault()) {}

  ~DlcClientImpl() override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (bus_) {
      // Post the shutdown task to the task runner to prevent connection error
      // for the remaining D-Bus tasks in the queue.
      task_runner_->PostTask(
          FROM_HERE, base::BindOnce(&dbus::Bus::ShutdownAndBlock, bus_));
    }
  }

  bool Initialize(
      base::OnceCallback<void(const base::FilePath&)> dlc_root_path_cb,
      base::OnceCallback<void(const std::string&)> error_cb,
      base::RepeatingCallback<void(double)> progress_cb) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    dlc_root_path_cb_ = std::move(dlc_root_path_cb);
    progress_cb_ = std::move(progress_cb);
    error_cb_ = std::move(error_cb);
    LOG(INFO) << "Setting up DlcClient";

    dbus::Bus::Options opts;
    opts.bus_type = dbus::Bus::SYSTEM;
    bus_ = base::MakeRefCounted<dbus::Bus>(std::move(opts));
    if (!bus_->Connect()) {
      LOG(ERROR) << "Failed to connect to system bus";
      return false;
    }
    LOG(INFO) << "Connected to system bus";

    dlcservice_client_ =
        std::make_unique<org::chromium::DlcServiceInterfaceProxy>(bus_);

    base::WeakPtr<DlcClientImpl> weak_this = weak_factory_.GetWeakPtr();
    dlcservice_client_->RegisterDlcStateChangedSignalHandler(
        base::BindRepeating(&DlcClientImpl::OnDlcStateChanged, weak_this),
        base::BindOnce(&DlcClientImpl::OnDlcStateChangedConnect, weak_this));

    LOG(INFO) << "DlcClient setup complete";
    return true;
  }

  void SetMetricsBaseName(const std::string& metrics_base_name) override {
    metrics_.SetMetricsBaseName(metrics_base_name);
  }

  void InstallDlc() override {
    task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&DlcClientImpl::Install, weak_factory_.GetWeakPtr(), 1));
  }

 private:
  void Install(int attempt) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    LOG(INFO) << "InstallDlc called for " << dlc_id_
              << ", attempt: " << attempt;
    if (!bus_->IsConnected()) {
      metrics_.RecordBeginInstallResult(
          cros::DlcBeginInstallResult::kDBusNotConnected);
      InvokeErrorCb("Error calling dlcservice: DBus not connected");
      return;
    }

    metrics_.RecordInstallAttemptCount(attempt, kMaxInstallAttempts);

    brillo::ErrorPtr error;
    auto handle_error = [&]() {
      if (error == nullptr) {
        metrics_.RecordBeginInstallResult(
            cros::DlcBeginInstallResult::kUnknownDlcServiceFailure);
        InvokeErrorCb("Error calling dlcservice: unknown");
        return;
      }

      metrics_.RecordBeginInstallDlcServiceError(
          cros::DlcErrorCodeEnumFromString(error->GetCode()));
      LOG(ERROR) << "Error code: " << error->GetCode()
                 << " msg: " << error->GetMessage();

      if (error->GetCode() == dlcservice::kErrorBusy) {
        attempt++;
        if (attempt > kMaxInstallAttempts) {
          metrics_.RecordBeginInstallResult(
              cros::DlcBeginInstallResult::kDlcServiceBusyWillAbort);
          auto err = base::StrCat(
              {"Install attempts for ", dlc_id_, " exhausted, aborting."});
          LOG(ERROR) << err;
          InvokeErrorCb(err);
          return;
        }

        metrics_.RecordBeginInstallResult(
            cros::DlcBeginInstallResult::kDlcServiceBusyWillRetry);
        auto retry_delay = kBaseDelay * std::exp2(attempt - 1);
        LOG(ERROR) << "dlcservice is busy. Retrying in " << retry_delay;

        task_runner_->PostDelayedTask(
            FROM_HERE,
            base::BindOnce(&DlcClientImpl::Install, weak_factory_.GetWeakPtr(),
                           attempt),
            retry_delay);
        return;
      } else {
        metrics_.RecordBeginInstallResult(
            cros::DlcBeginInstallResult::kOtherDlcServiceError);
      }

      InvokeErrorCb(
          base::StrCat({"Error calling dlcservice (code=", error->GetCode(),
                        "): ", error->GetMessage()}));
    };

    if (dlc_id_ == cros::dlc_client::kMlCoreDlcId) {
      dlcservice::DlcState dlc_state;
      if (!dlcservice_client_->GetDlcState(dlc_id_, &dlc_state, &error)) {
        LOG(ERROR) << "Error calling dlcservice_client_->GetDlcState for "
                   << dlc_id_;
        handle_error();
        return;
      }

      if (!dlc_state.is_verified()) {
        uninstalling = true;
        // Uninstall an older version of the DLC if available. This ensures to
        // remove the existing logical volume for the DLC to accommodate changes
        // in DLC_PREALLOC_BLOCKS.
        if (!dlcservice_client_->Uninstall(dlc_id_, &error)) {
          LOG(ERROR) << "Error calling dlcservice_client_->Uninstall for "
                     << dlc_id_;
          handle_error();
          return;
        }
      }
    }

    dlcservice::InstallRequest install_request;
    install_request.set_id(dlc_id_);

    if (!dlcservice_client_->Install(install_request, &error,
                                     kDlcInstallTimeout)) {
      LOG(ERROR) << "Error calling dlcservice_client_->Install for " << dlc_id_;
      handle_error();
      return;
    }

    metrics_.RecordBeginInstallResult(cros::DlcBeginInstallResult::kSuccess);
    LOG(INFO) << "InstallDlc successfully initiated for " << dlc_id_;
  }

  void OnDlcStateChanged(const dlcservice::DlcState& dlc_state) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (dlc_state.id() != dlc_id_) {
      return;
    }

    if (uninstalling &&
        dlc_state.state() == dlcservice::DlcState::NOT_INSTALLED) {
      uninstalling = false;
      return;
    }

    LOG(INFO) << "OnDlcStateChanged (" << dlc_state.id()
              << "): " << dlcservice::DlcState::State_Name(dlc_state.state());

    switch (dlc_state.state()) {
      case dlcservice::DlcState::INSTALLED:
        metrics_.RecordFinalInstallResult(
            cros::DlcFinalInstallResult::kSuccess);
        LOG(INFO) << "Successfully installed DLC " << dlc_id_ << " at "
                  << dlc_state.root_path();
        InvokeSuccessCb(base::FilePath(dlc_state.root_path()));
        break;
      case dlcservice::DlcState::INSTALLING:
        LOG(INFO) << static_cast<int>(dlc_state.progress() * 100)
                  << "% installing DLC: " << dlc_id_;
        if (progress_cb_) {
          progress_cb_.Run(dlc_state.progress());
        }
        break;
      case dlcservice::DlcState::NOT_INSTALLED: {
        metrics_.RecordFinalInstallDlcServiceError(
            cros::DlcErrorCodeEnumFromString(dlc_state.last_error_code()));
        // "BUSY" error code is not considered an installation failure.
        if (dlc_state.last_error_code() != dlcservice::kErrorBusy) {
          metrics_.RecordFinalInstallResult(
              cros::DlcFinalInstallResult::kDlcServiceError);
          InvokeErrorCb(
              base::StrCat({"Failed to install DLC: ", dlc_id_,
                            " Error: ", dlc_state.last_error_code()}));
        }
        break;
      }
      default:
        InvokeErrorCb(base::StrCat({"Unknown error when installing: ", dlc_id_,
                                    " Error: ", dlc_state.last_error_code()}));
        break;
    }
  }

  void OnDlcStateChangedConnect(const std::string& interface,
                                const std::string& signal,
                                const bool success) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    LOG(INFO) << "OnDlcStateChangedConnect (" << interface << ":" << signal
              << "): " << (success ? "true" : "false");
    if (!success) {
      InvokeErrorCb(
          base::StrCat({"Error connecting ", interface, ". ", signal}));
    }
  }

  void InvokeSuccessCb(const base::FilePath& dlc_root_path) {
    if (!dlc_root_path_cb_) {
      return;
    }

    if (progress_cb_) {
      progress_cb_.Run(1.0);
    }

    error_cb_.Reset();
    progress_cb_.Reset();
    base::OnceCallback<void(const base::FilePath&)> dlc_root_path_cb =
        std::move(dlc_root_path_cb_);

    // Don't use any member function or variable after this line, because the
    // DlcClient may be destroyed inside the callback.
    std::move(dlc_root_path_cb).Run(dlc_root_path);
  }

  void InvokeErrorCb(const std::string& error_msg) {
    if (!error_cb_) {
      return;
    }

    dlc_root_path_cb_.Reset();
    progress_cb_.Reset();
    base::OnceCallback<void(const std::string&)> error_cb =
        std::move(error_cb_);

    // Don't use any member function or variable after this line, because the
    // DlcClient may be destroyed inside the callback.
    std::move(error_cb).Run(error_msg);
  }

  const std::string dlc_id_;
  bool uninstalling = false;
  cros::DlcMetrics metrics_;
  std::string metrics_base_name_;
  std::unique_ptr<org::chromium::DlcServiceInterfaceProxyInterface>
      dlcservice_client_;
  scoped_refptr<dbus::Bus> bus_;
  base::OnceCallback<void(const base::FilePath&)> dlc_root_path_cb_;
  base::OnceCallback<void(const std::string&)> error_cb_;
  base::RepeatingCallback<void(double)> progress_cb_;
  scoped_refptr<base::SequencedTaskRunner> task_runner_;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<DlcClientImpl> weak_factory_{this};
};

class DlcClientForTest : public cros::DlcClient {
 public:
  DlcClientForTest(
      base::OnceCallback<void(const base::FilePath&)> dlc_root_path_cb,
      base::OnceCallback<void(const std::string&)> error_cb,
      base::RepeatingCallback<void(double)> progress_cb,
      const base::FilePath& path)
      : dlc_root_path_cb_(std::move(dlc_root_path_cb)),
        error_cb_(std::move(error_cb)),
        progress_cb_(std::move(progress_cb)),
        path_(path) {}

  // Metrics not emitted in DlcClientForTest.
  void SetMetricsBaseName(const std::string& /*unused*/) override {}

  void InstallDlc() override {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&DlcClientForTest::InvokeSuccessCb,
                                  base::Unretained(this)));
  }

  void InvokeSuccessCb() {
    if (progress_cb_)
      progress_cb_.Run(1.0);
    if (dlc_root_path_cb_)
      std::move(dlc_root_path_cb_).Run(path_);
  }

  base::OnceCallback<void(const base::FilePath&)> dlc_root_path_cb_;
  base::OnceCallback<void(const std::string&)> error_cb_;
  base::RepeatingCallback<void(double)> progress_cb_;
  const base::FilePath path_;
};

}  // namespace

namespace {
base::FilePath path_for_test;
}  // namespace

namespace cros {

std::unique_ptr<DlcClient> DlcClient::Create(
    const std::string& dlc_id,
    base::OnceCallback<void(const base::FilePath&)> dlc_root_path_cb,
    base::OnceCallback<void(const std::string&)> error_cb,
    base::RepeatingCallback<void(double)> progress_cb) {
  if (!path_for_test.empty()) {
    LOG(INFO) << "Using predefined path " << path_for_test << " for DLC "
              << dlc_id;
    auto client = std::make_unique<DlcClientForTest>(
        std::move(dlc_root_path_cb), std::move(error_cb),
        std::move(progress_cb), path_for_test);
    return client;
  } else {
    auto client = std::make_unique<DlcClientImpl>(dlc_id);
    if (client->Initialize(std::move(dlc_root_path_cb), std::move(error_cb),
                           std::move(progress_cb))) {
      return client;
    }
    return nullptr;
  }
}

void DlcClient::SetDlcPathForTest(const base::FilePath* path) {
  if (path) {
    path_for_test = *path;
  } else {
    path_for_test.clear();
  }
}

}  // namespace cros
