// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mock_dns_client.h"

#include <string>
#include <vector>

#include "shill/ip_address.h"

using std::string;
using std::vector;

namespace shill {

MockDNSClient::MockDNSClient()
    : DNSClient(IPAddress::kFamilyIPv4, "", vector<string>(), 0, NULL,
                ClientCallback()) {}

MockDNSClient::~MockDNSClient() {}

}  // namespace shill
