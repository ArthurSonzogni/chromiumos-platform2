// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_CONNECTION_DIAGNOSTICS_H_
#define SHILL_MOCK_CONNECTION_DIAGNOSTICS_H_

#include <memory>
#include <string_view>
#include <vector>

#include <gmock/gmock.h>
#include <net-base/http_url.h>
#include <net-base/ip_address.h>

#include "shill/connection_diagnostics.h"
#include "shill/event_dispatcher.h"
#include "shill/metrics.h"

namespace shill {

class MockConnectionDiagnostics : public ConnectionDiagnostics {
 public:
  MockConnectionDiagnostics();
  ~MockConnectionDiagnostics() override;

  MOCK_METHOD(bool, Start, (const net_base::HttpUrl& url), (override));
};

class MockConnectionDiagnosticsFactory : public ConnectionDiagnosticsFactory {
 public:
  MockConnectionDiagnosticsFactory();
  ~MockConnectionDiagnosticsFactory() override;

  MOCK_METHOD(std::unique_ptr<ConnectionDiagnostics>,
              Create,
              (std::string_view iface_name,
               int iface_index,
               const net_base::IPAddress& ip_address,
               const net_base::IPAddress& gateway,
               const std::vector<net_base::IPAddress>& dns_list,
               EventDispatcher* dispatcher,
               Metrics* metrics,
               ConnectionDiagnostics::ResultCallback result_callback),
              (override));
};

}  // namespace shill
#endif  // SHILL_MOCK_CONNECTION_DIAGNOSTICS_H_
