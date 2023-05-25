// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/network_applier.h"

#include <base/memory/ptr_util.h>

#include "shill/ipconfig.h"
#include "shill/network/network_priority.h"

namespace shill {

NetworkApplier::NetworkApplier() : resolver_(Resolver::GetInstance()) {}

NetworkApplier::~NetworkApplier() = default;

// static
NetworkApplier* NetworkApplier::GetInstance() {
  static base::NoDestructor<NetworkApplier> instance;
  return instance.get();
}

// static
std::unique_ptr<NetworkApplier> NetworkApplier::CreateForTesting(
    Resolver* resolver) {
  // Using `new` to access a non-public constructor.
  auto ptr = base::WrapUnique(new NetworkApplier());
  ptr->resolver_ = resolver;
  return ptr;
}

void NetworkApplier::ApplyDNS(NetworkPriority priority,
                              IPConfig::Properties properties) {
  if (!priority.is_primary_for_dns) {
    return;
  }

  auto domain_search = properties.domain_search;
  if (domain_search.empty() && !properties.domain_name.empty()) {
    domain_search.push_back(properties.domain_name + ".");
  }
  resolver_->SetDNSFromLists(properties.dns_servers, domain_search);
}
}  // namespace shill
