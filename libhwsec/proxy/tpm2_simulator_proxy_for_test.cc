// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/time/time.h>
#include <brillo/dbus/dbus_connection.h>
#include <libhwsec-foundation/tpm/tpm_version.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>
#include <tpm2-simulator/tpm_executor_tpm2_impl.h>
#include <trunks/trunks_dbus_proxy.h>
#include <trunks/trunks_factory_impl.h>

#include "libhwsec/proxy/tpm2_simulator_proxy_for_test.h"

namespace {

constexpr char kOwnerPassword[] = "owner_password";
constexpr char kEndorsementPassword[] = "endorsement_password";
constexpr char kLockoutPassword[] = "lockout_password";

class ScopedChdir {
 public:
  explicit ScopedChdir(const base::FilePath& dir) {
    CHECK(GetCurrentDirectory(&previous_dir));
    CHECK(SetCurrentDirectory(dir));
  }
  ScopedChdir(const ScopedChdir&) = delete;
  ScopedChdir& operator=(const ScopedChdir&) = delete;
  ~ScopedChdir() { CHECK(SetCurrentDirectory(previous_dir)); }

 private:
  base::FilePath previous_dir;
};

class Tpm2SimulatorCommandTransceiver : public trunks::CommandTransceiver {
 public:
  explicit Tpm2SimulatorCommandTransceiver(
      const base::FilePath& simulator_state_directory)
      : simulator_state_directory_(simulator_state_directory) {}
  ~Tpm2SimulatorCommandTransceiver() override = default;

  bool Init() override {
    ScopedChdir dir(simulator_state_directory_);
    tpm_executor_.InitializeVTPM();
    return true;
  }

  void SendCommand(const std::string& command,
                   ResponseCallback callback) override {
    std::move(callback).Run(SendCommandAndWait(command));
  }

  std::string SendCommandAndWait(const std::string& command) override {
    ScopedChdir dir(simulator_state_directory_);
    return tpm_executor_.RunCommand(command);
  }

 private:
  base::FilePath simulator_state_directory_;
  tpm2_simulator::TpmExecutorTpm2Impl tpm_executor_;
};

}  // namespace

namespace hwsec {

Tpm2SimulatorProxyForTest::Tpm2SimulatorProxyForTest() {}

Tpm2SimulatorProxyForTest::~Tpm2SimulatorProxyForTest() {
  if (initialized_) {
    low_level_factory_->GetTpmUtility()->Shutdown();
  }
}

bool Tpm2SimulatorProxyForTest::Init() {
  if (initialized_) {
    return true;
  }

  if (!tmp_tpm_dir_.CreateUniqueTempDir()) {
    LOG(ERROR) << "Failed to create unique temp dir.";
    return false;
  }

  low_level_transceiver_ =
      std::make_unique<Tpm2SimulatorCommandTransceiver>(tmp_tpm_dir_.GetPath());
  if (!low_level_transceiver_->Init()) {
    LOG(ERROR) << "Failed to init simulator command transceiver.";
    return false;
  }

  low_level_factory_ =
      std::make_unique<trunks::TrunksFactoryImpl>(low_level_transceiver_.get());
  if (!low_level_factory_->Initialize()) {
    LOG(ERROR) << "Failed to init low level factory.";
    return false;
  }

  if (low_level_factory_->GetTpmUtility()->Startup()) {
    LOG(ERROR) << "Failed to startup TPM.";
    return false;
  }

  if (low_level_factory_->GetTpmUtility()->InitializeTpm()) {
    LOG(ERROR) << "Failed to init TPM.";
    return false;
  }

  const std::string owner_password = kOwnerPassword;
  const std::string endorsement_password = kEndorsementPassword;
  const std::string lockout_password = kLockoutPassword;
  if (low_level_factory_->GetTpmUtility()->TakeOwnership(
          owner_password, endorsement_password, lockout_password)) {
    LOG(ERROR) << "Failed to init TPM.";
    return false;
  }

  resource_manager_ = std::make_unique<trunks::ResourceManager>(
      *low_level_factory_, low_level_transceiver_.get());
  resource_manager_->Initialize();

  trunks_factory_ =
      std::make_unique<trunks::TrunksFactoryImpl>(resource_manager_.get());
  if (!trunks_factory_->Initialize()) {
    LOG(ERROR) << "Failed to init trunks factory.";
    return false;
  }

  Proxy::SetTrunksCommandTransceiver(low_level_transceiver_.get());
  Proxy::SetTrunksFactory(trunks_factory_.get());
  Proxy::SetTpmManager(&tpm_manager_);
  Proxy::SetTpmNvram(&tpm_nvram_);

  initialized_ = true;
  return true;
}

}  // namespace hwsec
