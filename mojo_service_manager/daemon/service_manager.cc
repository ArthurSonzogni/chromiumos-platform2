// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo_service_manager/daemon/service_manager.h"

#include <map>
#include <memory>
#include <string>
#include <utility>

#include <base/notreached.h>

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
  NOTIMPLEMENTED();
}

void ServiceManager::AddServiceObserver(
    mojo::PendingRemote<mojom::ServiceObserver> observer) {
  NOTIMPLEMENTED();
}

}  // namespace mojo_service_manager
}  // namespace chromeos
