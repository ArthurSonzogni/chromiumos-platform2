// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_MOCK_CONNECTION_DIAGNOSTICS_H_
#define SHILL_NETWORK_MOCK_CONNECTION_DIAGNOSTICS_H_

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <chromeos/net-base/dns_client.h>
#include <chromeos/net-base/http_url.h>
#include <chromeos/net-base/ip_address.h>
#include <gmock/gmock.h>

#include "shill/event_dispatcher.h"
#include "shill/network/connection_diagnostics.h"
#include "shill/network/icmp_session.h"

namespace shill {

class MockConnectionDiagnostics : public ConnectionDiagnostics {
 public:
  MockConnectionDiagnostics();
  ~MockConnectionDiagnostics() override;

  MOCK_METHOD(void, Start, (const net_base::HttpUrl& url), (override));
  MOCK_METHOD(bool, IsRunning, (), (const, override));
};

class MockConnectionDiagnosticsFactory : public ConnectionDiagnosticsFactory {
 public:
  MockConnectionDiagnosticsFactory();
  ~MockConnectionDiagnosticsFactory() override;

  MOCK_METHOD(std::unique_ptr<ConnectionDiagnostics>,
              Create,
              (std::string_view iface_name,
               int iface_index,
               net_base::IPFamily ip_family,
               std::optional<net_base::IPAddress> gateway,
               const std::vector<net_base::IPAddress>& dns_list,
               std::unique_ptr<net_base::DNSClientFactory> dns_client_factory,
               std::unique_ptr<IcmpSessionFactory> icmp_session_factory,
               std::string_view logging_tag,
               EventDispatcher* dispatcher),
              (override));
};

}  // namespace shill
#endif  // SHILL_NETWORK_MOCK_CONNECTION_DIAGNOSTICS_H_
