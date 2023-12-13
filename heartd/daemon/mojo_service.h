// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEARTD_DAEMON_MOJO_SERVICE_H_
#define HEARTD_DAEMON_MOJO_SERVICE_H_

#include <mojo/public/cpp/bindings/remote.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

#include "heartd/daemon/utils/mojo_service_provider.h"
#include "heartd/mojom/heartd.mojom.h"

namespace heartd {

// Implements the Mojo interface exposed by the heartd daemon.
// See the API definition at //heartd/mojom/heartd.mojom.
class HeartdMojoService final : public ash::heartd::mojom::HeartbeatService,
                                public ash::heartd::mojom::HeartdControl {
 public:
  HeartdMojoService();
  HeartdMojoService(const HeartdMojoService&) = delete;
  HeartdMojoService& operator=(const HeartdMojoService&) = delete;
  ~HeartdMojoService() override;

 private:
  // Mojo remote to mojo service manager, used to register mojo interface.
  mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>
      service_manager_;
  // Mojo service providers to provide heartbeat services interface to mojo
  // service manager.
  MojoServiceProvider<ash::heartd::mojom::HeartbeatService>
      heartbeat_service_provider_{this};
  // Mojo service providers to provide heartd control interface to mojo service
  // manager.
  MojoServiceProvider<ash::heartd::mojom::HeartdControl>
      heartd_control_provider_{this};
};

}  // namespace heartd

#endif  // HEARTD_DAEMON_MOJO_SERVICE_H_
