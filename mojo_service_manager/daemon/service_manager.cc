// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo_service_manager/daemon/service_manager.h"

#include <string>
#include <utility>

namespace chromeos {
namespace mojo_service_manager {

ServiceManager::ServiceManager(Configuration configuration,
                               ServicePolicyMap policy_map)
    : configuration_(std::move(configuration)) {
  for (auto& item : policy_map) {
    auto& [service_name, policy] = item;
    service_map_[service_name] = ServiceState{.policy = std::move(policy)};
  }
}

ServiceManager::~ServiceManager() = default;

void ServiceManager::AddReceiver(
    mojom::ProcessIdentityPtr process_identity,
    mojo::PendingReceiver<mojom::ServiceManager> receiver) {
  receiver_set_.Add(this, std::move(receiver), std::move(process_identity));
}

void ServiceManager::Register(
    const std::string& service_name,
    mojo::PendingRemote<mojom::ServiceProvider> service_provider) {
  NOTIMPLEMENTED();
}

void ServiceManager::Request(const std::string& service_name,
                             absl::optional<base::TimeDelta> timeout,
                             mojo::ScopedMessagePipeHandle receiver) {
  NOTIMPLEMENTED();
}

void ServiceManager::Query(const std::string& service_name,
                           QueryCallback callback) {
  auto it = service_map_.find(service_name);
  if (it == service_map_.end()) {
    std::move(callback).Run(mojom::ErrorOrServiceState::NewError(
        mojom::Error::New(mojom::ErrorCode::kServiceNotFound,
                          "Cannot find service: " + service_name)));
    return;
  }

  const ServiceState& service_state = it->second;
  const mojom::ProcessIdentityPtr& identity = receiver_set_.current_context();
  if (!configuration_.is_permissive &&
      !service_state.policy.IsRequester(identity->security_context)) {
    std::move(callback).Run(
        mojom::ErrorOrServiceState::NewError(mojom::Error::New(
            mojom::ErrorCode::kPermissionDenied,
            "The security context: " + identity->security_context +
                " is not allowed to request the service: " + service_name)));
    return;
  }
  std::move(callback).Run(
      mojom::ErrorOrServiceState::NewState(mojom::ServiceState::New(
          /*is_registered=*/!service_state.owner.is_null(),
          /*owner=*/service_state.owner.Clone())));
}

void ServiceManager::AddServiceObserver(
    mojo::PendingRemote<mojom::ServiceObserver> observer) {
  NOTIMPLEMENTED();
}

}  // namespace mojo_service_manager
}  // namespace chromeos
