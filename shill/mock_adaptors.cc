// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mock_adaptors.h"

namespace shill {

DeviceMockAdaptor::DeviceMockAdaptor() = default;
DeviceMockAdaptor::~DeviceMockAdaptor() = default;

IPConfigMockAdaptor::IPConfigMockAdaptor() = default;
IPConfigMockAdaptor::~IPConfigMockAdaptor() = default;

ManagerMockAdaptor::ManagerMockAdaptor() = default;
ManagerMockAdaptor::~ManagerMockAdaptor() = default;

ServiceMockAdaptor::ServiceMockAdaptor() = default;
ServiceMockAdaptor::~ServiceMockAdaptor() = default;

#ifndef DISABLE_VPN
ThirdPartyVpnMockAdaptor::ThirdPartyVpnMockAdaptor() = default;
ThirdPartyVpnMockAdaptor::~ThirdPartyVpnMockAdaptor() = default;
#endif

}  // namespace shill
