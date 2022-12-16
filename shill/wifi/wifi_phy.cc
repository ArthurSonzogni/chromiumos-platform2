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

void WiFiPhy::AddWiFiLocalDevice(LocalDeviceConstRefPtr device) {
  wifi_local_devices_.insert(device);
}

void WiFiPhy::DeleteWiFiLocalDevice(LocalDeviceConstRefPtr device) {
  wifi_local_devices_.erase(device);
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
  ParseInterfaceTypes(nl80211_message);
  // TODO(b/244630773): Parse out the message and store phy information.
  ParseConcurrency(nl80211_message);
}

bool WiFiPhy::SupportsIftype(nl80211_iftype iftype) {
  return base::Contains(supported_ifaces_, iftype);
}

void WiFiPhy::ParseInterfaceTypes(const Nl80211Message& nl80211_message) {
  // Verify NL80211_CMD_NEW_WIPHY.
  if (nl80211_message.command() != NewWiphyMessage::kCommand) {
    LOG(ERROR) << "Received unexpected command: " << nl80211_message.command();
    return;
  }

  AttributeListConstRefPtr ifaces;
  if (nl80211_message.const_attributes()->ConstGetNestedAttributeList(
          NL80211_ATTR_SUPPORTED_IFTYPES, &ifaces)) {
    AttributeIdIterator ifaces_iter(*ifaces);
    for (; !ifaces_iter.AtEnd(); ifaces_iter.Advance()) {
      uint32_t iface;
      if (!ifaces->GetU32AttributeValue(ifaces_iter.GetId(), &iface)) {
        LOG(ERROR) << "Failed to get supported iface type "
                   << ifaces_iter.GetId();
        continue;
      }
      if (iface < 0 || iface >= NL80211_IFTYPE_MAX) {
        LOG(ERROR) << "Invalid iface type: " << iface;
        continue;
      }
      supported_ifaces_.insert(nl80211_iftype(iface));
    }
  }
}

void WiFiPhy::ParseConcurrency(const Nl80211Message& nl80211_message) {
  // Verify NL80211_CMD_NEW_WIPHY.
  if (nl80211_message.command() != NewWiphyMessage::kCommand) {
    LOG(ERROR) << "Received unexpected command: " << nl80211_message.command();
    return;
  }

  // Check that the message contains concurrency combinations.
  AttributeListConstRefPtr interface_combinations_attr;
  if (!nl80211_message.const_attributes()->ConstGetNestedAttributeList(
          NL80211_ATTR_INTERFACE_COMBINATIONS, &interface_combinations_attr)) {
    return;
  }
  // Iterate over the combinations in the message.
  concurrency_combs_.clear();
  AttributeIdIterator comb_iter(*interface_combinations_attr);
  for (; !comb_iter.AtEnd(); comb_iter.Advance()) {
    struct ConcurrencyCombination comb;
    AttributeListConstRefPtr iface_comb_attr;
    if (!interface_combinations_attr->ConstGetNestedAttributeList(
            comb_iter.GetId(), &iface_comb_attr)) {
      continue;  // Next combination.
    }

    // Check that the combination has limits.
    AttributeListConstRefPtr iface_limits_attr;
    if (!iface_comb_attr->ConstGetNestedAttributeList(NL80211_IFACE_COMB_LIMITS,
                                                      &iface_limits_attr)) {
      continue;  // Next combination.
    }

    iface_comb_attr->GetU32AttributeValue(NL80211_IFACE_COMB_MAXNUM,
                                          &comb.max_num);
    iface_comb_attr->GetU32AttributeValue(NL80211_IFACE_COMB_NUM_CHANNELS,
                                          &comb.num_channels);

    AttributeIdIterator limit_iter(*iface_limits_attr);
    for (; !limit_iter.AtEnd(); limit_iter.Advance()) {
      struct IfaceLimit limit;
      AttributeListConstRefPtr limit_attr;
      if (!iface_limits_attr->ConstGetNestedAttributeList(limit_iter.GetId(),
                                                          &limit_attr)) {
        LOG(WARNING) << "Interface combination limit " << limit_iter.GetId()
                     << " not found";
        // If we reach this line then the message is malformed and we should
        // stop parsing it.
        return;
      }
      limit_attr->GetU32AttributeValue(NL80211_IFACE_LIMIT_MAX, &limit.max);

      // Check that the limit contains interface types.
      AttributeListConstRefPtr iface_types_attr;
      if (!limit_attr->ConstGetNestedAttributeList(NL80211_IFACE_LIMIT_TYPES,
                                                   &iface_types_attr)) {
        continue;
      }
      for (uint32_t iftype = NL80211_IFTYPE_UNSPECIFIED;
           iftype < NUM_NL80211_IFTYPES; iftype++) {
        if (iface_types_attr->GetFlagAttributeValue(iftype, nullptr)) {
          limit.iftypes.push_back(nl80211_iftype(iftype));
        }
      }
      comb.limits.push_back(limit);
    }
    concurrency_combs_.push_back(comb);
  }
}

}  // namespace shill
