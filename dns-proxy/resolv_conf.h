// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DNS_PROXY_RESOLV_CONF_H_
#define DNS_PROXY_RESOLV_CONF_H_

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/memory/ref_counted.h>
#include <base/no_destructor.h>

namespace dns_proxy {

// This provides a static function for dumping the DNS information out
// of an ipconfig into a "resolv.conf" formatted file.
class ResolvConf {
 public:
  // The default comma-separated list of search-list prefixes that
  // should be ignored when writing out a DNS configuration.  These
  // are usually preconfigured by a DHCP server and are not of real
  // value to the user.  This will release DNS bandwidth for searches
  // we expect will have a better chance of getting what the user is
  // looking for.
  static const char kDefaultIgnoredSearchList[];

  virtual ~ResolvConf();

  // Since this is a singleton, use ResolvConf::GetInstance()->Foo().
  static ResolvConf* GetInstance();

  virtual void set_path(const base::FilePath& path) { path_ = path; }

  // Install domain name service parameters, given a list of
  // DNS servers in |name_servers|, and a list of DNS search suffixes in
  // |domain_search_list|.
  virtual bool SetDNSFromLists(
      const std::vector<std::string>& name_servers,
      const std::vector<std::string>& domain_search_list);

  // Tells the resolver that DNS should go through the proxy address(es)
  // provided. If |proxy_addrs| is non-empty, this name server will be used
  // instead of any provided by SetDNSFromLists. Previous name servers are not
  // forgotten, and will be restored if this method is called again with
  // |proxy_addrs| empty.
  virtual bool SetDNSProxyAddresses(
      const std::vector<std::string>& proxy_addrs);

  // Remove any created domain name service file.
  virtual bool ClearDNS();

 protected:
  ResolvConf();
  ResolvConf(const ResolvConf&) = delete;
  ResolvConf& operator=(const ResolvConf&) = delete;

 private:
  friend class ResolvConfTest;
  friend class base::NoDestructor<ResolvConf>;

  // Writes the resolver file.
  bool Emit();

  base::FilePath path_;
  std::vector<std::string> name_servers_;
  std::vector<std::string> domain_search_list_;
  std::vector<std::string> dns_proxy_addrs_;
};

}  // namespace dns_proxy

#endif  // DNS_PROXY_RESOLV_CONF_H_
