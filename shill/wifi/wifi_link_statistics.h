// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_WIFI_LINK_STATISTICS_H_
#define SHILL_WIFI_WIFI_LINK_STATISTICS_H_

#include <cstdint>
#include <list>
#include <string>

#include <base/time/time.h>

#include "shill/metrics.h"
#include "shill/mockable.h"
#include "shill/net/rtnl_link_stats.h"
#include "shill/store/key_value_store.h"

namespace shill {

class WiFiLinkStatistics {
 public:
  // Enum corresponding to various network layer events defined in the
  // base Device class. This enum is used for labelling link statistics obtained
  // from NL80211 and RTNL kernel interfaces for a WiFi interface at the time of
  // these events.
  enum class Trigger {
    kUnknown,
    // IPv4 and IPv6 dynamic configuration is starting for this network. This
    // corresponds to the start of the initial DHCP lease acquisition by dhcpcd
    // and to the start of IPv6 SLAAC in the kernel.
    kIPConfigurationStart,
    // The network is connected and one of IPv4 or IPv6 is provisioned. This
    // corresponds to the beginning of the first network validation event if
    // PortalDetector is used for validating the network Internet access.
    kConnected,
    // A roaming event is triggering a DHCP renew.
    kDHCPRenewOnRoam,
    // DHCPv4 lease acquisation has successfully completed.
    kDHCPSuccess,
    // DHCPv4 lease acquisation has failed. This event happens whenever the
    // DHCPController instance associated with the network invokes its
    // FailureCallback.
    kDHCPFailure,
    // IPv6 SLAAC has completed successfully. On IPv4-only networks where IPv6
    // is not available, there is no timeout event of failure event recorded.
    kSlaacFinished,
    // A network validation attempt by PortalDetector is starting.
    kNetworkValidationStart,
    // A network validation attempt has completed and verified Internet
    // connectivity.
    kNetworkValidationSuccess,
    // A network validation attempt has completed but Internet connectivity
    // was not verified.
    kNetworkValidationFailure,
    // The kernel notified us, through a CQM event, that the RSSI is considered
    // low, it's below the configured threshold.
    kCQMRSSILow,
    // The kernel notified us, through a CQM event, that the RSSI is considered
    // high, it's above the configured threshold.
    kCQMRSSIHigh,
    // The kernel notified us, through a CQM event, that we have not received
    // beacons from the AP recently.
    kCQMBeaconLoss,
    // The kernel notified us, through a CQM event, that we've lost packets.
    kCQMPacketLoss,
    // We always update (some of) the link statistics in the background, every
    // |WiFi::kRequestStationInfoPeriod|. Among other things that allows us to
    // update the signal strength UI regularly.
    kBackground,
  };

  enum class ChannelWidth {
    kChannelWidthUnknown,
    kChannelWidth20MHz,
    kChannelWidth40MHz,
    kChannelWidth80MHz,
    kChannelWidth80p80MHz,  // 80+80MHz channel configuration.
    kChannelWidth160MHz,
    kChannelWidth320MHz,
  };
  enum class LinkMode {
    kLinkModeUnknown,
    kLinkModeLegacy,
    kLinkModeVHT,
    kLinkModeHE,
    kLinkModeEHT,
  };

  enum class GuardInterval {
    kLinkStatsGIUnknown,
    kLinkStatsGI_0_4,
    kLinkStatsGI_0_8,
    kLinkStatsGI_1_6,
    kLinkStatsGI_3_2,
  };

  struct LinkStats {
    uint32_t packets = -1;
    uint32_t bytes = -1;
    uint32_t bitrate = -1;  // unit is 100Kb/s.
    uint8_t mcs = -1;
    ChannelWidth width = ChannelWidth::kChannelWidthUnknown;
    LinkMode mode = LinkMode::kLinkModeUnknown;
    GuardInterval gi = GuardInterval::kLinkStatsGIUnknown;
    uint8_t nss = -1;
    uint8_t dcm = -1;
  };

  struct StationStats {
    uint32_t inactive_time = -1;
    uint32_t tx_retries = -1;
    uint32_t tx_failed = -1;
    uint64_t rx_drop_misc = -1;
    int32_t signal = 9999;  // wpa_supplicant uses int32_t value, default 9999.
    int32_t signal_avg = 9999;
    LinkStats rx;
    LinkStats tx;
  };

  struct Nl80211LinkStatistics {
    // The event that triggered the snapshot of WiFiLinkStatistics.
    Trigger trigger = Trigger::kUnknown;
    base::Time timestamp;
    StationStats nl80211_link_stats;
    Nl80211LinkStatistics(Trigger trigger, const StationStats& stats)
        : trigger(trigger), nl80211_link_stats(stats) {
      timestamp = base::Time::Now();
    }
  };

  struct RtnlLinkStatistics {
    // The event that triggered the snapshot of WiFiLinkStatistics.
    Trigger trigger = Trigger::kUnknown;
    base::Time timestamp;
    old_rtnl_link_stats64 rtnl_link_stats;
    RtnlLinkStatistics(Trigger event, const old_rtnl_link_stats64& stats)
        : trigger(event), rtnl_link_stats(stats) {
      timestamp = base::Time::Now();
    }
  };

  WiFiLinkStatistics() = default;
  virtual ~WiFiLinkStatistics() = default;

  // Clear all existing nl80211 and RTNL link statistics in the lists
  void Reset();

  static std::string LinkStatisticsTriggerToString(Trigger event);

  // Convert StationStats to a key/value store object that can be used to export
  // statistics over D-Bus.
  static KeyValueStore StationStatsToKV(const StationStats& stats);

  // Update a new snapshot of WiFi link statistics.
  // If trigger is a start network event, the WiFi link statistics is
  // appended to the WiFi link statistics list; if it is an end network
  // event, pop the WiFi link statistics of the corresponding start network
  // event from the list and print the difference if the end network event is a
  // failure.

  // Each network activity must call WiFi::RetrieveLinkStatistics() at both
  // start network event and end network event; otherwise, the WiFi link
  // statistics of the start network event is left in the list and matches
  // the wrong end network event.
  mockable void UpdateNl80211LinkStatistics(Trigger trigger,
                                            const StationStats& stats);
  mockable void UpdateRtnlLinkStatistics(Trigger trigger,
                                         const old_rtnl_link_stats64& stats);

  static Metrics::WiFiLinkQualityTrigger ConvertLinkStatsTriggerEvent(
      Trigger trigger);

  static Metrics::WiFiLinkQualityReport ConvertLinkStatsReport(
      const StationStats& stats);

 private:
  friend class WiFiLinkStatisticsTest;

  // The snapshot of link statistics is updated if the trigger is not
  // kUnknown. The difference between the end and start network events is
  // printed to the log if necessary, i.e., the end network event is a failure,
  // such as kDHCPFailure, kNetworkValidationFailure
  std::list<Nl80211LinkStatistics> nl80211_link_statistics_;
  std::list<RtnlLinkStatistics> rtnl_link_statistics_;
};

}  // namespace shill

#endif  // SHILL_WIFI_WIFI_LINK_STATISTICS_H_
