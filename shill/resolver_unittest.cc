// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/resolver.h"

#include <base/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/stl_util.h>
#include <base/stringprintf.h>
#include <gtest/gtest.h>

#include "shill/mock_control.h"

using std::string;
using std::vector;
using testing::Test;

namespace shill {

namespace {
const char kTestDeviceName0[] = "netdev0";
const char kNameServer0[] = "8.8.8.8";
const char kNameServer1[] = "8.8.9.9";
const char kSearchDomain0[] = "chromium.org";
const char kSearchDomain1[] = "google.com";
const char kSearchDomain2[] = "crosbug.com";
const char kExpectedOutput[] =
  "nameserver 8.8.8.8\n"
  "nameserver 8.8.9.9\n"
  "search chromium.org google.com\n"
  "options single-request timeout:1 attempts:3\n";
const char kExpectedShortTimeoutOutput[] =
  "nameserver 8.8.8.8\n"
  "nameserver 8.8.9.9\n"
  "search chromium.org google.com\n"
  "options single-request timeout-ms:300 attempts:15\n";
const char kExpectedIgnoredSearchOutput[] =
  "nameserver 8.8.8.8\n"
  "nameserver 8.8.9.9\n"
  "search google.com\n"
  "options single-request timeout:1 attempts:3\n";
}  // namespace {}

class ResolverTest : public Test {
 public:
  ResolverTest() : resolver_(Resolver::GetInstance()) {}

  virtual void SetUp() {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    path_ = temp_dir_.path().Append("resolver");
    resolver_->set_path(path_);
  }

  virtual void TearDown() {
    resolver_->set_path(FilePath(""));  // Don't try to save the store.
    ASSERT_TRUE(temp_dir_.Delete());
  }

 protected:
  string ReadFile();

  base::ScopedTempDir temp_dir_;
  Resolver *resolver_;
  FilePath path_;
};

string ResolverTest::ReadFile() {
  string data;
  EXPECT_TRUE(file_util::ReadFileToString(resolver_->path_, &data));
  return data;
}

TEST_F(ResolverTest, NonEmpty) {
  EXPECT_FALSE(file_util::PathExists(path_));
  EXPECT_TRUE(resolver_->ClearDNS());

  MockControl control;
  vector<string> dns_servers;
  vector<string> domain_search;
  dns_servers.push_back(kNameServer0);
  dns_servers.push_back(kNameServer1);
  domain_search.push_back(kSearchDomain0);
  domain_search.push_back(kSearchDomain1);

  EXPECT_TRUE(resolver_->SetDNSFromLists(
      dns_servers, domain_search, Resolver::kDefaultTimeout));
  EXPECT_TRUE(file_util::PathExists(path_));
  EXPECT_EQ(kExpectedOutput, ReadFile());

  EXPECT_TRUE(resolver_->ClearDNS());
}

TEST_F(ResolverTest, ShortTimeout) {
  EXPECT_FALSE(file_util::PathExists(path_));
  EXPECT_TRUE(resolver_->ClearDNS());

  MockControl control;
  vector<string> dns_servers;
  vector<string> domain_search;
  dns_servers.push_back(kNameServer0);
  dns_servers.push_back(kNameServer1);
  domain_search.push_back(kSearchDomain0);
  domain_search.push_back(kSearchDomain1);

  EXPECT_TRUE(resolver_->SetDNSFromLists(
      dns_servers, domain_search, Resolver::kShortTimeout));
  EXPECT_TRUE(file_util::PathExists(path_));
  EXPECT_EQ(kExpectedShortTimeoutOutput, ReadFile());

  EXPECT_TRUE(resolver_->ClearDNS());
}

TEST_F(ResolverTest, Empty) {
  EXPECT_FALSE(file_util::PathExists(path_));

  MockControl control;
  vector<string> dns_servers;
  vector<string> domain_search;

  EXPECT_TRUE(resolver_->SetDNSFromLists(
      dns_servers, domain_search, Resolver::kDefaultTimeout));
  EXPECT_FALSE(file_util::PathExists(path_));
}

TEST_F(ResolverTest, IgnoredSearchList) {
  EXPECT_FALSE(file_util::PathExists(path_));
  EXPECT_TRUE(resolver_->ClearDNS());

  MockControl control;
  vector<string> dns_servers;
  vector<string> domain_search;
  dns_servers.push_back(kNameServer0);
  dns_servers.push_back(kNameServer1);
  domain_search.push_back(kSearchDomain0);
  domain_search.push_back(kSearchDomain1);
  vector<string> ignored_search;
  ignored_search.push_back(kSearchDomain0);
  ignored_search.push_back(kSearchDomain2);
  resolver_->set_ignored_search_list(ignored_search);
  EXPECT_TRUE(resolver_->SetDNSFromLists(
      dns_servers, domain_search, Resolver::kDefaultTimeout));
  EXPECT_TRUE(file_util::PathExists(path_));
  EXPECT_EQ(kExpectedIgnoredSearchOutput, ReadFile());

  EXPECT_TRUE(resolver_->ClearDNS());
}

}  // namespace shill
