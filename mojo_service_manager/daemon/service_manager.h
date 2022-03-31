// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICE_MANAGER_DAEMON_SERVICE_MANAGER_H_
#define MOJO_SERVICE_MANAGER_DAEMON_SERVICE_MANAGER_H_

#include <map>
#include <string>

#include <mojo/public/cpp/bindings/receiver_set.h>

#include "mojo_service_manager/daemon/configuration.h"
#include "mojo_service_manager/daemon/service_policy.h"
#include "mojo_service_manager/daemon/service_request_queue.h"
#include "mojo_service_manager/lib/mojom/service_manager.mojom.h"

namespace chromeos {
namespace mojo_service_manager {

// Implements mojom::ServiceManager.
class ServiceManager : public mojom::ServiceManager {
 public:
  ServiceManager(Configuration configuration, ServicePolicyMap policy_map);
  ServiceManager(const ServiceManager&) = delete;
  ServiceManager& operator=(const ServiceManager&) = delete;
  ~ServiceManager() override;

  // Adds a receiver of mojom::ServiceManager to the receiver set. A process
  // identity will be bound to this receiver.
  void AddReceiver(mojom::ProcessIdentityPtr process_identity,
                   mojo::PendingReceiver<mojom::ServiceManager> receiver);

 private:
  // Keeps all the objects related to a mojo service.
  struct ServiceState {
    // The policy applied to this mojo service.
    ServicePolicy policy;
    // The identity of the current owner process.
    mojom::ProcessIdentityPtr owner;
    // The queue to keep the service request before the service is available.
    // Note that this is not moveable.
    ServiceRequestQueue request_queue;
  };

  // mojom::ServiceManager overrides.
  void Register(
      const std::string& service_name,
      mojo::PendingRemote<mojom::ServiceProvider> service_provider) override;
  void Request(const std::string& service_name,
               absl::optional<base::TimeDelta> timeout,
               mojo::ScopedMessagePipeHandle receiver) override;
  void Query(const std::string& service_name, QueryCallback callback) override;
  void AddServiceObserver(
      mojo::PendingRemote<mojom::ServiceObserver> observer) override;

  // The service manager configuration.
  const Configuration configuration_;
  // Maps each service name to a ServiceState.
  std::map<std::string, ServiceState> service_map_;
  // The receivers of mojom::ServiceManager. The context type of the
  // mojo::ReceiverSet is set to mojom::ProcessIdentity so it can be used when
  // handle the requests.
  mojo::ReceiverSet<mojom::ServiceManager, mojom::ProcessIdentityPtr>
      receiver_set_;
};

}  // namespace mojo_service_manager
}  // namespace chromeos

#endif  // MOJO_SERVICE_MANAGER_DAEMON_SERVICE_MANAGER_H_
