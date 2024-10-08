// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dns-proxy/resolv_conf.h"

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <chromeos/net-base/ip_address.h>
#include <gtest/gtest.h>

using testing::Test;

namespace dns_proxy {

namespace {
const net_base::IPAddress kNameServer0 =
    *net_base::IPAddress::CreateFromString("8.8.8.8");
const net_base::IPAddress kNameServer1 =
    *net_base::IPAddress::CreateFromString("8.8.9.9");
const net_base::IPAddress kNameServer2 =
    *net_base::IPAddress::CreateFromString("2001:4860:4860:0:0:0:0:8888");
constexpr char kNameServerProxy[] = "100.115.94.1";
constexpr char kSearchDomain0[] = "chromium.org";
constexpr char kSearchDomain1[] = "google.com";
constexpr char kSearchDomainEvil[] = "google.com\nnameserver 6.6.6.6";
constexpr char kSearchDomainSubtlyEvil[] = "crate&barrel.com";
constexpr char kExpectedOutput[] =
    "nameserver 8.8.8.8\n"
    "nameserver 8.8.9.9\n"
    "nameserver 2001:4860:4860::8888\n"
    "search chromium.org google.com\n"
    "options single-request timeout:1 attempts:5\n";
constexpr char kExpectedProxyOutput[] =
    "nameserver 100.115.94.1\n"
    "options single-request timeout:1 attempts:5\n";
constexpr char kExpectedProxyWithSearchOutput[] =
    "nameserver 100.115.94.1\n"
    "search chromium.org google.com\n"
    "options single-request timeout:1 attempts:5\n";
}  // namespace

class ResolvConfTest : public Test {
 public:
  ResolvConfTest() : resolv_conf_(new ResolvConf()) {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    path_ = temp_dir_.GetPath().Append("resolv_conf");
    resolv_conf_->set_path(path_);
    EXPECT_FALSE(base::PathExists(path_));
  }

  void TearDown() override {
    resolv_conf_->set_path(base::FilePath(""));  // Don't try to save the store.
    ASSERT_TRUE(temp_dir_.Delete());
  }

 protected:
  std::string ReadFile();

  base::ScopedTempDir temp_dir_;
  std::unique_ptr<ResolvConf> resolv_conf_;
  base::FilePath path_;
};

std::string ResolvConfTest::ReadFile() {
  std::string data;
  EXPECT_TRUE(base::ReadFileToString(resolv_conf_->path_, &data));
  return data;
}

TEST_F(ResolvConfTest, NonEmpty) {
  std::vector<net_base::IPAddress> dns_servers = {kNameServer0, kNameServer1,
                                                  kNameServer2};
  std::vector<std::string> domain_search = {kSearchDomain0, kSearchDomain1};

  EXPECT_TRUE(resolv_conf_->SetDNSFromLists(dns_servers, domain_search));
  EXPECT_TRUE(base::PathExists(path_));
  EXPECT_EQ(kExpectedOutput, ReadFile());
}

TEST_F(ResolvConfTest, Sanitize) {
  std::vector<net_base::IPAddress> dns_servers = {kNameServer0, kNameServer1,
                                                  kNameServer2};
  std::vector<std::string> domain_search = {kSearchDomainEvil, kSearchDomain0,
                                            kSearchDomain1,
                                            kSearchDomainSubtlyEvil};

  EXPECT_TRUE(resolv_conf_->SetDNSFromLists(dns_servers, domain_search));
  EXPECT_TRUE(base::PathExists(path_));
  EXPECT_EQ(kExpectedOutput, ReadFile());
}

TEST_F(ResolvConfTest, Empty) {
  std::vector<net_base::IPAddress> dns_servers;
  std::vector<std::string> domain_search;

  EXPECT_TRUE(resolv_conf_->SetDNSFromLists(dns_servers, domain_search));
}

TEST_F(ResolvConfTest, Proxy) {
  EXPECT_TRUE(resolv_conf_->SetDNSProxyAddresses({kNameServerProxy}));
  EXPECT_TRUE(base::PathExists(path_));
  EXPECT_EQ(kExpectedProxyOutput, ReadFile());
}

TEST_F(ResolvConfTest, ProxyClear) {
  EXPECT_TRUE(resolv_conf_->SetDNSProxyAddresses({kNameServerProxy}));
  EXPECT_TRUE(base::PathExists(path_));
  EXPECT_TRUE(resolv_conf_->SetDNSProxyAddresses({}));
  EXPECT_TRUE(base::PathExists(path_));
}

TEST_F(ResolvConfTest, ProxyToggle) {
  std::vector<net_base::IPAddress> dns_servers = {kNameServer0, kNameServer1,
                                                  kNameServer2};
  std::vector<std::string> domain_search = {kSearchDomain0, kSearchDomain1};
  // Connection's DNS
  EXPECT_TRUE(resolv_conf_->SetDNSFromLists(dns_servers, domain_search));
  EXPECT_TRUE(base::PathExists(path_));
  EXPECT_EQ(kExpectedOutput, ReadFile());
  // DNS proxy set
  EXPECT_TRUE(resolv_conf_->SetDNSProxyAddresses({kNameServerProxy}));
  EXPECT_TRUE(base::PathExists(path_));
  EXPECT_EQ(kExpectedProxyWithSearchOutput, ReadFile());
  // Connection DNS update (no change to resolv.conf)
  EXPECT_TRUE(resolv_conf_->SetDNSFromLists(dns_servers, domain_search));
  EXPECT_TRUE(base::PathExists(path_));
  EXPECT_EQ(kExpectedProxyWithSearchOutput, ReadFile());
  // DNS proxy cleared
  EXPECT_TRUE(resolv_conf_->SetDNSProxyAddresses({}));
  EXPECT_TRUE(base::PathExists(path_));
  EXPECT_EQ(kExpectedOutput, ReadFile());
}

}  // namespace dns_proxy
