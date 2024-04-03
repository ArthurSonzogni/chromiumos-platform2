// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DNS_PROXY_MOCK_RESOLV_CONF_H_
#define DNS_PROXY_MOCK_RESOLV_CONF_H_

#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "dns-proxy/resolv_conf.h"

namespace dns_proxy {

class MockResolvConf : public ResolvConf {
 public:
  MockResolvConf();
  MockResolvConf(const MockResolvConf&) = delete;
  MockResolvConf& operator=(const MockResolvConf&) = delete;

  ~MockResolvConf() override;

  MOCK_METHOD(bool,
              SetDNSProxyAddresses,
              (const std::vector<std::string>&),
              (override));
};

}  // namespace dns_proxy

#endif  // DNS_PROXY_MOCK_RESOLV_CONF_H_
