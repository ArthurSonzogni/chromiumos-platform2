// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "base/at_exit.h"
#include <base/check.h>
#include <base/logging.h>
#include <base/strings/string_util.h>

#include "shill/metrics.h"
#include "shill/mock_control.h"
#include "shill/mock_event_dispatcher.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/supplicant/wpa_supplicant.h"
#include "shill/wifi/mock_wifi.h"
#include "shill/wifi/wifi_endpoint.h"

namespace shill {

using ::testing::NiceMock;

class WiFiIEsFuzz {
 public:
  static void Run(const uint8_t* data, size_t size) {
    std::vector<uint8_t> ies(data, data + size);
    KeyValueStore properties;
    properties.Set(WPASupplicant::kBSSPropertyIEs, ies);

    MockControl ctrl_iface;
    MockEventDispatcher dispatcher;
    MockMetrics metrics;
    NiceMock<MockManager> manager(&ctrl_iface, &dispatcher, &metrics);
    WiFiRefPtr wifi = base::MakeRefCounted<MockWiFi>(
        &manager, "wlan0", "0123456789AB", 1, 2, nullptr);

    Metrics::WiFiNetworkPhyMode phy_mode;

    auto endpoint = WiFiEndpoint::MakeOpenEndpoint(
        nullptr, wifi, "ssid", "00:00:00:00:00:01", kModeManaged, 2412, 0);
    endpoint->ParseIEs(properties, &phy_mode);

    // D-Bus wants our strings UTF-8, and ISO 3166 says they should be ASCII.
    CHECK(base::IsStringASCII(endpoint->country_code()));
  }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Turn off logging.
  logging::SetMinLogLevel(logging::LOGGING_FATAL);
  base::AtExitManager at_exit;

  WiFiIEsFuzz::Run(data, size);
  return 0;
}

}  // namespace shill
