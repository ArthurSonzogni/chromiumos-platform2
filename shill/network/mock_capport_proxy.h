// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_MOCK_CAPPORT_PROXY_H_
#define SHILL_NETWORK_MOCK_CAPPORT_PROXY_H_

#include "shill/network/capport_proxy.h"

#include <memory>

#include <base/containers/span.h>
#include <gmock/gmock.h>
#include <net-base/ip_address.h>

#include "shill/metrics.h"

namespace shill {

class MockCapportProxy : public CapportProxy {
 public:
  MockCapportProxy();
  ~MockCapportProxy() override;

  MOCK_METHOD(void, SendRequest, (StatusCallback callback), (override));
  MOCK_METHOD(void, Stop, (), (override));
  MOCK_METHOD(bool, IsRunning, (), (const, override));
};

class MockCapportProxyFactory : public CapportProxyFactory {
 public:
  MockCapportProxyFactory();
  ~MockCapportProxyFactory() override;

  MOCK_METHOD(std::unique_ptr<CapportProxy>,
              Create,
              (Metrics*,
               std::string_view,
               const net_base::HttpUrl&,
               base::span<const net_base::IPAddress>,
               std::shared_ptr<brillo::http::Transport>,
               base::TimeDelta transport_timeout),
              (override));
};
}  // namespace shill
#endif  // SHILL_NETWORK_MOCK_CAPPORT_PROXY_H_
