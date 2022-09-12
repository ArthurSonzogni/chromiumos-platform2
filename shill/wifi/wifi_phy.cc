// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/wifi.h"
#include "shill/wifi/wifi_phy.h"

namespace shill {

WiFiPhy::WiFiPhy(uint32_t phy_index) : phy_index_(phy_index) {}

WiFiPhy::~WiFiPhy() = default;

void WiFiPhy::AddWiFiDevice(WiFiConstRefPtr device) {
  wifi_devices_.insert(device);
}

void WiFiPhy::DeleteWiFiDevice(WiFiConstRefPtr device) {
  wifi_devices_.erase(device);
}

// TODO(b/248103586): Move NL80211_CMD_NEW_WIPHY parsing out of WiFiPhy and into
// WiFiProvider.
void WiFiPhy::OnNewWiphy(const Nl80211Message& nl80211_message) {
  // Verify NL80211_CMD_NEW_WIPHY.
  if (nl80211_message.command() != NewWiphyMessage::kCommand) {
    LOG(ERROR) << "Received unexpected command:" << nl80211_message.command();
    return;
  }
  uint32_t message_phy_index;
  if (!nl80211_message.const_attributes()->GetU32AttributeValue(
          NL80211_ATTR_WIPHY, &message_phy_index)) {
    LOG(ERROR) << "NL80211_CMD_NEW_WIPHY had no NL80211_ATTR_WIPHY";
    return;
  }
  if (message_phy_index != phy_index_) {
    LOG(ERROR) << "WiFiPhy at index " << phy_index_
               << " received NL80211_CMD_NEW_WIPHY for unexpected phy index "
               << message_phy_index;
    return;
  }
  // TODO(b/244630773): Parse out the message and store phy information.
}

}  // namespace shill
