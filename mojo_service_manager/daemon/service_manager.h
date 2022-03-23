// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICE_MANAGER_DAEMON_SERVICE_MANAGER_H_
#define MOJO_SERVICE_MANAGER_DAEMON_SERVICE_MANAGER_H_

#include <map>
#include <memory>
#include <string>

#include "mojo_service_manager/daemon/service_policy.h"
#include "mojo_service_manager/lib/mojom/service_manager.mojom.h"

namespace chromeos {
namespace mojo_service_manager {

// Implements mojom::ServiceManager.
class ServiceManager : public mojom::ServiceManager {
 public:
  explicit ServiceManager(ServicePolicyMap policy_map);
  ServiceManager(const ServiceManager&) = delete;
  ServiceManager& operator=(const ServiceManager&) = delete;
  ~ServiceManager() override;

 private:
  // Keeps all the objects related to a mojo service.
  struct ServiceState {
    // The policy applied to this mojo service.
    ServicePolicy policy;
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

  // Maps each service name to a ServiceState.
  std::map<std::string, ServiceState> service_map_;
};

}  // namespace mojo_service_manager
}  // namespace chromeos

#endif  // MOJO_SERVICE_MANAGER_DAEMON_SERVICE_MANAGER_H_
