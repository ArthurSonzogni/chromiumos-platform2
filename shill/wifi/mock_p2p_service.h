// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_MOCK_P2P_SERVICE_H_
#define SHILL_WIFI_MOCK_P2P_SERVICE_H_

#include "shill/wifi/p2p_service.h"

#include <memory>
#include <string>

#include <gmock/gmock.h>

#include "shill/refptr_types.h"

namespace shill {

class MockP2PService : public P2PService {
 public:
  MockP2PService(LocalDeviceConstRefPtr device,
                 std::optional<std::string> ssid,
                 std::optional<std::string> passphrase,
                 std::optional<uint32_t> frequency)
      : P2PService(device, ssid, passphrase, frequency) {}

  ~MockP2PService() = default;
};

}  // namespace shill

#endif  // SHILL_WIFI_MOCK_P2P_SERVICE_H_
