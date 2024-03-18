// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEARTD_DAEMON_MOJO_SERVICE_H_
#define HEARTD_DAEMON_MOJO_SERVICE_H_

#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>
#include <mojo_service_manager/lib/simple_mojo_service_provider.h>

#include "heartd/daemon/action_runner.h"
#include "heartd/daemon/heartbeat_manager.h"
#include "heartd/mojom/heartd.mojom.h"

namespace heartd {

// Implements the Mojo interface exposed by the heartd daemon.
// See the API definition at //heartd/mojom/heartd.mojom.
class HeartdMojoService final : public ash::heartd::mojom::HeartbeatService,
                                public ash::heartd::mojom::HeartdControl {
 public:
  explicit HeartdMojoService(HeartbeatManager* heartbeat_manager,
                             ActionRunner* action_runner);
  HeartdMojoService(const HeartdMojoService&) = delete;
  HeartdMojoService& operator=(const HeartdMojoService&) = delete;
  ~HeartdMojoService() override;

  // ash::heartd::mojom::HeartbeatService overrides:
  void Register(ash::heartd::mojom::ServiceName name,
                ash::heartd::mojom::HeartbeatServiceArgumentPtr argument,
                mojo::PendingReceiver<ash::heartd::mojom::Pacemaker> receiver,
                RegisterCallback callback) override;

  // ash::heartd::mojom::HeartdControl overrides:
  void EnableNormalRebootAction() override;
  void EnableForceRebootAction() override;
  void RunAction(ash::heartd::mojom::ActionType action,
                 RunActionCallback callback) override;

 private:
  // Mojo remote to mojo service manager, used to register mojo interface.
  mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>
      service_manager_;
  // Mojo service providers to provide heartbeat services interface to mojo
  // service manager.
  chromeos::mojo_service_manager::SimpleMojoServiceProvider<
      ash::heartd::mojom::HeartbeatService>
      heartbeat_service_provider_{this};
  // Mojo service providers to provide heartd control interface to mojo service
  // manager.
  chromeos::mojo_service_manager::SimpleMojoServiceProvider<
      ash::heartd::mojom::HeartdControl>
      heartd_control_provider_{this};
  // Unowned pointer. Should outlive this instance.
  // It is used to register new heartbeat tracker.
  HeartbeatManager* const heartbeat_manager_;
  // Unowned pointer. Should outlive this instance.
  // It is used to configure the actions.
  ActionRunner* const action_runner_;
};

}  // namespace heartd

#endif  // HEARTD_DAEMON_MOJO_SERVICE_H_
