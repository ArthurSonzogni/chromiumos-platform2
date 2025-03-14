// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_WIFI_CQM_H_
#define SHILL_WIFI_WIFI_CQM_H_

#include "shill/metrics.h"

namespace shill {

class Nl80211Message;
class WiFi;

// WiFiCQM class implements connection quality monitoring feature. Connection
// Quality Monitor (CQM) is a feature provided by the kernel. It emits
// notification for different kinds of adverse network situations such as beacon
// losses, packet losses, poor signal levels. Shill listens to Connection
// Quality Monitor notifications from the kernel and takes next steps such as,
// adding metrics and/or firmware dumps. Following are the attributes supported
// by connection quality monitor:
//  NL80211_ATTR_CQM_RSSI_THOLD,
//  NL80211_ATTR_CQM_RSSI_HYST,
//  NL80211_ATTR_CQM_RSSI_THRESHOLD_EVENT,
//  NL80211_ATTR_CQM_PKT_LOSS_EVENT,
//  NL80211_ATTR_CQM_TXE_RATE,
//  NL80211_ATTR_CQM_TXE_PKTS,
//  NL80211_ATTR_CQM_TXE_INTVL,
//  NL80211_ATTR_CQM_BEACON_LOSS_EVENT,
//  NL80211_ATTR_CQM_RSSI_LEVEL
//
// The above attributes can be configured by the userspace as well, if not
// configured, kernel will be using the default values. Also one thing to note,
// in a given cqm message, there will always be one attribute e.g. RSSI
// threshold breach message and Packet loss message cannot be clubbed together.
class WiFiCQM {
 public:
  WiFiCQM(Metrics* metrics, WiFi* wifi);
  WiFiCQM(const WiFiCQM&) = delete;
  WiFiCQM& operator=(const WiFiCQM&) = delete;

  virtual ~WiFiCQM();

  // When this is triggered in response to a CQM msg from the kernel, this
  // evaluates the validity of the message, and captures metrics and
  // conditionally triggers firmware dump for Beacon and Packet losses.
  void OnCQMNotify(const Nl80211Message& nl80211_message);

 private:
  friend class WiFiCQMTest;

  // Triggers firmware dump with a rate-limiting mechanism.
  void TriggerFwDump();

  base::Time previous_fw_dump_time_;
  WiFi* wifi_;        // |wifi_| owns |this|.
  Metrics* metrics_;  // Owned by wifi_->manager().
};

}  // namespace shill

#endif  // SHILL_WIFI_WIFI_CQM_H_
