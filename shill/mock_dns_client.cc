// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mock_dns_client.h"

#include <base/time/time.h>

namespace shill {

MockDnsClient::MockDnsClient()
    : DnsClient(net_base::IPFamily::kIPv4,
                "",
                base::Seconds(0),
                nullptr,
                ClientCallback()) {}

MockDnsClient::~MockDnsClient() = default;

}  // namespace shill
