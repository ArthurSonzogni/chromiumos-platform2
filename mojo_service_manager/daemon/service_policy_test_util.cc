// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo_service_manager/daemon/service_policy_test_util.h"

#include <ostream>
#include <set>
#include <string>

#include <base/check.h>

namespace chromeos::mojo_service_manager {
namespace {

template <typename T>
void PrintStringSet(const std::set<T>& set, std::ostream* out) {
  (*out) << "{";
  for (const auto& s : set) {
    (*out) << "\"" << s << "\", ";
  }
  (*out) << "}";
}
}  // namespace

ServicePolicyMap CreateServicePolicyMapForTest(
    const std::map<std::string,
                   std::pair<std::optional<uint32_t>, std::set<uint32_t>>>&
        items) {
  ServicePolicyMap result;
  for (const auto& item : items) {
    result[item.first] =
        CreateServicePolicyForTest(item.second.first, item.second.second);
  }
  return result;
}

ServicePolicyMap CreateServicePolicyMapForTest(
    const std::map<std::string, std::pair<std::string, std::set<std::string>>>&
        items) {
  ServicePolicyMap result;
  for (const auto& item : items) {
    result[item.first] =
        CreateServicePolicyForTest(item.second.first, item.second.second);
  }
  return result;
}

ServicePolicyMap CreateServicePolicyMapForTest(
    const std::map<std::string,
                   std::pair<std::optional<uint32_t>, std::set<uint32_t>>>&
        items_uid,
    const std::map<std::string, std::pair<std::string, std::set<std::string>>>&
        items_selinux) {
  auto map_uid = CreateServicePolicyMapForTest(items_uid);
  auto map_selinux = CreateServicePolicyMapForTest(items_selinux);
  CHECK(MergeServicePolicyMaps(&map_selinux, &map_uid));
  return map_uid;
}

ServicePolicy CreateServicePolicyForTest(const std::optional<uint32_t>& owner,
                                         const std::set<uint32_t>& requesters) {
  ServicePolicy policy;
  policy.owner_uid_ = owner;
  policy.requesters_uid_ = requesters;
  return policy;
}

ServicePolicy CreateServicePolicyForTest(
    const std::string& owner, const std::set<std::string>& requesters) {
  ServicePolicy policy;
  policy.owner_ = owner;
  policy.requesters_ = requesters;
  return policy;
}

bool operator==(const ServicePolicy& a, const ServicePolicy& b) {
  return a.owner() == b.owner() && a.requesters() == b.requesters() &&
         a.owner_uid() == b.owner_uid() &&
         a.requesters_uid() == b.requesters_uid();
}

std::ostream& operator<<(std::ostream& out, const ServicePolicy& policy) {
  out << "ServicePolicy{ owner_uid: ";
  if (policy.owner_uid()) {
    out << policy.owner_uid().value();
  } else {
    out << "[null]";
  }
  out << ", requesters_uid: ";
  PrintStringSet(policy.requesters_uid(), &out);

  out << ", owner: ";
  if (policy.owner().empty()) {
    out << "[null]";
  } else {
    out << policy.owner();
  }
  out << ", requesters: ";
  PrintStringSet(policy.requesters(), &out);
  out << "}";
  return out;
}

}  // namespace chromeos::mojo_service_manager
