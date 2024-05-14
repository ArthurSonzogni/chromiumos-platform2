// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/mojo_service.h"

#include <memory>
#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <chromeos/mojo/service_constants.h>
#include <mojo_service_manager/lib/connect.h>

#include "heartd/daemon/action_runner.h"
#include "heartd/daemon/sheriffs/sheriff.h"
#include "heartd/daemon/utils/mojo_output.h"
#include "heartd/mojom/heartd.mojom.h"

namespace heartd {

namespace {

namespace mojom = ::ash::heartd::mojom;

}  // namespace

HeartdMojoService::HeartdMojoService(HeartbeatManager* heartbeat_manager,
                                     ActionRunner* action_runner,
                                     TopSheriff* top_sheriff)
    : heartbeat_manager_(heartbeat_manager),
      action_runner_(action_runner),
      top_sheriff_(top_sheriff) {
  auto pending_remote =
      chromeos::mojo_service_manager::ConnectToMojoServiceManager();
  CHECK(pending_remote) << "Failed to connect to mojo service manager.";

  service_manager_.Bind(std::move(pending_remote));
  service_manager_.set_disconnect_with_reason_handler(
      base::BindOnce([](uint32_t error, const std::string& message) {
        LOG(INFO) << "Disconnected from mojo service manager (the mojo broker "
                     "process). Error: "
                  << error << ", message: " << message
                  << ". Shutdown and wait for respawn.";
      }));

  heartbeat_service_provider_.Register(
      service_manager_.get(), chromeos::mojo_services::kHeartdHeartbeatService);
  heartd_control_provider_.Register(service_manager_.get(),
                                    chromeos::mojo_services::kHeartdControl);
  heartbeat_verifier_ = new HeartbeatVerifier(heartbeat_manager_);
  top_sheriff_->AddSheriff(std::unique_ptr<Sheriff>(heartbeat_verifier_));
}

HeartdMojoService::~HeartdMojoService() = default;

void HeartdMojoService::Register(
    mojom::ServiceName name,
    mojom::HeartbeatServiceArgumentPtr argument,
    mojo::PendingReceiver<mojom::Pacemaker> receiver,
    RegisterCallback callback) {
  if (heartbeat_manager_->IsPacemakerBound(name)) {
    LOG(ERROR) << "Repeated registration: " << ToStr(name);
    std::move(callback).Run(false);
    return;
  }

  heartbeat_manager_->EstablishHeartbeatTracker(name, std::move(receiver),
                                                std::move(argument));
  heartbeat_verifier_->GetToWork();
  std::move(callback).Run(true);
}

void HeartdMojoService::EnableNormalRebootAction() {
  LOG(INFO) << "Heartbeat service enables normal reboot action";
  action_runner_->EnableNormalRebootAction();
}

void HeartdMojoService::EnableForceRebootAction() {
  LOG(INFO) << "Heartbeat service enables force reboot action";
  action_runner_->EnableForceRebootAction();
}

void HeartdMojoService::RunAction(mojom::ActionType action,
                                  RunActionCallback callback) {
  LOG(INFO) << "Heartbeat service runs action: " << ToStr(action);
  // Just use kKiosk as the service name, since this interface is used by test.
  action_runner_->Run(mojom::ServiceName::kKiosk, action);
  std::move(callback).Run(true);
}

}  // namespace heartd
