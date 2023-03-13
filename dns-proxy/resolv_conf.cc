// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dns-proxy/resolv_conf.h"

#include <string>
#include <vector>

#include <base/containers/contains.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_util.h>

#include "dns-proxy/dns_util.h"
#include "shill/net/ip_address.h"

namespace dns_proxy {

ResolvConf::ResolvConf() = default;

ResolvConf::~ResolvConf() = default;

bool ResolvConf::SetDNSFromLists(
    const std::vector<std::string>& name_servers,
    const std::vector<std::string>& domain_search_list) {
  name_servers_ = name_servers;
  domain_search_list_ = domain_search_list;
  return Emit();
}

bool ResolvConf::Emit() {
  if (path_.empty()) {
    LOG(DFATAL) << "No path set";
    return false;
  }

  // dns-proxy always used if set.
  const auto name_servers =
      !dns_proxy_addrs_.empty() ? dns_proxy_addrs_ : name_servers_;
  if (name_servers.empty() && domain_search_list_.empty()) {
    LOG(ERROR) << "DNS list is empty";
    return ClearDNS();
  }

  std::vector<std::string> lines;
  for (const auto& server : name_servers) {
    const auto addr = shill::IPAddress::CreateFromString(server);
    if (!addr.has_value()) {
      LOG(WARNING) << "Malformed nameserver IP: " << server;
      continue;
    }
    lines.push_back("nameserver " + addr->ToString());
  }

  std::vector<std::string> filtered_domain_search_list;
  for (const auto& domain : domain_search_list_) {
    if (IsValidDNSDomain(domain)) {
      filtered_domain_search_list.push_back(domain);
    } else {
      LOG(WARNING) << "Malformed search domain: " << domain;
    }
  }

  if (!filtered_domain_search_list.empty()) {
    lines.push_back("search " +
                    base::JoinString(filtered_domain_search_list, " "));
  }

  // - Send queries one-at-a-time, rather than parallelizing IPv4
  //   and IPv6 queries for a single host.
  // - Override the default 5-second request timeout and use a
  //   1-second timeout instead. (NOTE: Chrome's ADNS will use
  //   one second, regardless of what we put here.)
  // - Allow 5 attempts, rather than the default of 2.
  //   - For glibc, the worst case number of queries will be
  //        attempts * count(servers) * (count(search domains)+1)
  //   - For Chrome, the worst case number of queries will be
  //        attempts * count(servers) + 3 * glibc
  //   See crbug.com/224756 for supporting data.
  lines.push_back("options single-request timeout:1 attempts:5");

  // Newline at end of file
  lines.push_back("");

  const auto contents = base::JoinString(lines, "\n");

  LOG(INFO) << "Writing DNS out to " << path_.value();
  int count = base::WriteFile(path_, contents.c_str(), contents.size());

  return count == static_cast<int>(contents.size());
}

bool ResolvConf::SetDNSProxyAddresses(
    const std::vector<std::string>& proxy_addrs) {
  dns_proxy_addrs_ = proxy_addrs;
  return Emit();
}

bool ResolvConf::ClearDNS() {
  if (path_.empty()) {
    LOG(DFATAL) << "No path set";
    return false;
  }

  name_servers_.clear();
  domain_search_list_.clear();
  dns_proxy_addrs_.clear();
  return base::DeleteFile(path_);
}

}  // namespace dns_proxy
