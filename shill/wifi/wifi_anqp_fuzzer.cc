// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include <base/at_exit.h>
#include <base/check.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <chromeos/net-base/mac_address.h>

#include "shill/mock_control.h"
#include "shill/mock_event_dispatcher.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/store/key_value_store.h"
#include "shill/supplicant/wpa_supplicant.h"
#include "shill/wifi/mock_wifi.h"
#include "shill/wifi/wifi_endpoint.h"

namespace shill {

using ::testing::NiceMock;

class WiFiANQPFuzz {
 public:
  static void Run(const uint8_t* data, size_t size) {
    std::vector<uint8_t> caps(data, data + size);
    KeyValueStore anqp;
    anqp.Set(WPASupplicant::kANQPChangePropertyCapabilityList, caps);
    KeyValueStore properties;
    properties.Set(WPASupplicant::kBSSPropertyANQP, anqp);

    MockControl ctrl_iface;
    MockEventDispatcher dispatcher;
    MockMetrics metrics;
    NiceMock<MockManager> manager(&ctrl_iface, &dispatcher, &metrics);
    WiFiRefPtr wifi = base::MakeRefCounted<MockWiFi>(
        &manager, "wlan0",
        net_base::MacAddress(0x01, 0x23, 0x45, 0x67, 0x89, 0xAB), 1, 2,
        nullptr);

    auto endpoint = WiFiEndpoint::MakeOpenEndpoint(
        nullptr, wifi, "ssid",
        net_base::MacAddress(0x00, 0x00, 0x00, 0x00, 0x00, 0x01), kModeManaged,
        2412, 0);
    endpoint->ParseANQPFields(properties);
  }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Turn off logging.
  logging::SetMinLogLevel(logging::LOGGING_FATAL);
  base::AtExitManager at_exit;

  WiFiANQPFuzz::Run(data, size);
  return 0;
}

}  // namespace shill
