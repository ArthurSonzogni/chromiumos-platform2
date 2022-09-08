// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mock_arp_client.h"

namespace shill {

MockArpClient::MockArpClient() : ArpClient(0) {}

MockArpClient::~MockArpClient() = default;

}  // namespace shill
