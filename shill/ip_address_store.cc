// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/ip_address_store.h"

#include <iterator>

#include <stdlib.h>
#include <time.h>

using std::advance;

namespace shill {

// This is a less than comparison so that IPAddress can be stored in a set.
// We do not care about a semantically meaningful comparison. This is
// deterministic, and that's all that matters.
bool IPAddressLTIgnorePrefix::operator () (const IPAddress &lhs,
                                           const IPAddress &rhs) const {
  return lhs.ToString() < rhs.ToString();
}

IPAddressStore::IPAddressStore() : random_engine_(time(nullptr)) {
}

IPAddressStore::~IPAddressStore() {}

void IPAddressStore::AddUnique(const IPAddress &ip) {
  ip_addresses_.insert(ip);
}

void IPAddressStore::Clear() {
  ip_addresses_.clear();
}

size_t IPAddressStore::Count() const {
  return ip_addresses_.size();
}

bool IPAddressStore::Empty() const {
  return ip_addresses_.empty();
}

IPAddress IPAddressStore::GetRandomIP() {
  if (ip_addresses_.empty())
    return IPAddress(IPAddress::kFamilyUnknown);
  std::uniform_int_distribution<int> uniform_rand(0, ip_addresses_.size() - 1);
  int index = uniform_rand(random_engine_);
  IPAddresses::const_iterator cit = ip_addresses_.begin();
  advance(cit, index);
  return *cit;
}

}  // namespace shill
