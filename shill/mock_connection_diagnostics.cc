// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mock_connection_diagnostics.h"

#include "shill/connection_diagnostics.h"

namespace shill {

MockConnectionDiagnostics::MockConnectionDiagnostics()
    : ConnectionDiagnostics(
          "wlan1",
          1,
          *net_base::IPAddress::CreateFromString("192.168.1.2"),
          *net_base::IPAddress::CreateFromString("192.168.1.1"),
          {},
          nullptr,
          nullptr,
          base::DoNothing()) {}

MockConnectionDiagnostics::~MockConnectionDiagnostics() = default;

MockConnectionDiagnosticsFactory::MockConnectionDiagnosticsFactory() = default;

MockConnectionDiagnosticsFactory::~MockConnectionDiagnosticsFactory() = default;
}  // namespace shill
