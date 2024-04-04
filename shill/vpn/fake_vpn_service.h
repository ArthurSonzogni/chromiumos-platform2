// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_FAKE_VPN_SERVICE_H_
#define SHILL_VPN_FAKE_VPN_SERVICE_H_

#include "shill/vpn/vpn_service.h"

namespace shill {

// VPNService with a fake VPNDriver.
class FakeVPNService : public VPNService {
 public:
  explicit FakeVPNService(Manager* manager);
  FakeVPNService(const FakeVPNService&) = delete;
  FakeVPNService& operator=(const FakeVPNService&) = delete;

  ~FakeVPNService() override;
};

}  // namespace shill

#endif  // SHILL_VPN_FAKE_VPN_SERVICE_H_
