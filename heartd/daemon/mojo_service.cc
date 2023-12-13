// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/mojo_service.h"

#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <chromeos/mojo/service_constants.h>
#include <mojo_service_manager/lib/connect.h>

namespace heartd {

HeartdMojoService::HeartdMojoService() {
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
}

HeartdMojoService::~HeartdMojoService() = default;

}  // namespace heartd
