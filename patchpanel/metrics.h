// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_METRICS_H_
#define PATCHPANEL_METRICS_H_

namespace patchpanel {

// UMA metrics name for patchpanel Manager Dbus API calls.
constexpr char kDbusUmaEventMetrics[] = "Network.Patchpanel.Dbus";
// UMA metrics name for ArcService events.
constexpr char kArcServiceUmaEventMetrics[] = "Network.Patchpanel.ArcService";

// UMA metrics name for results of a CreateTetheredNetwork request.
constexpr char kCreateTetheredNetworkUmaEventMetrics[] =
    "Network.Patchpanel.Tethering.CreateTetheredNetwork";
// UMA metrics name for DHCP server events when used by CreateTetheredNetwork.
constexpr char kTetheringDHCPServerUmaEventMetrics[] =
    "Network.Patchpanel.Tethering.DHCPServer";
// UMA metrics name for results of a CreateLocalOnlyNetwork request.
constexpr char kCreateLocalOnlyNetworkUmaEventMetrics[] =
    "Network.Patchpanel.LocalOnlyNetwork.CreateLocalOnlyNetwork";
// UMA metrics name for DHCP server events when used by CreateLocalOnlyNetwork.
constexpr char kLocalOnlyNetworkDHCPServerUmaEventMetrics[] =
    "Network.Patchpanel.LocalOnlyNetwork.DHCPServer";

// UMA metrics name for ARC WiFi multicast active time.
constexpr char kMulticastActiveTimeMetrics[] =
    "Network.Multicast.ARC.ActiveTime";
// UMA metrics names for multicast packet count.
constexpr char kMulticastTotalCountMetrics[] = "Network.Multicast.TotalCount";
constexpr char kMulticastEthernetConnectedCountMetrics[] =
    "Network.Multicast.Ethernet.ConnectedCount";
constexpr char kMulticastEthernetMDNSConnectedCountMetrics[] =
    "Network.Multicast.Ethernet.MDNS.ConnectedCount";
constexpr char kMulticastEthernetSSDPConnectedCountMetrics[] =
    "Network.Multicast.Ethernet.SSDP.ConnectedCount";
constexpr char kMulticastWiFiConnectedCountMetrics[] =
    "Network.Multicast.WiFi.ConnectedCount";
constexpr char kMulticastWiFiMDNSConnectedCountMetrics[] =
    "Network.Multicast.WiFi.MDNS.ConnectedCount";
constexpr char kMulticastWiFiSSDPConnectedCountMetrics[] =
    "Network.Multicast.WiFi.SSDP.ConnectedCount";
constexpr char kMulticastARCWiFiMDNSActiveCountMetrics[] =
    "Network.Multicast.ARC.WiFi.MDNS.ActiveCount";
constexpr char kMulticastARCWiFiSSDPActiveCountMetrics[] =
    "Network.Multicast.ARC.WiFi.SSDP.ActiveCount";
constexpr char kMulticastARCWiFiMDNSInactiveCountMetrics[] =
    "Network.Multicast.ARC.WiFi.MDNS.InactiveCount";
constexpr char kMulticastARCWiFiSSDPInactiveCountMetrics[] =
    "Network.Multicast.ARC.WiFi.SSDP.InactiveCount";

// UMA metrics events for |kDbusUmaEventMetrics|;
enum class DbusUmaEvent {
  kUnknown = 0,
  kArcStartup = 1,
  kArcStartupSuccess = 2,
  kArcShutdown = 3,
  kArcShutdownSuccess = 4,
  kArcVmStartup = 5,
  kArcVmStartupSuccess = 6,
  kArcVmShutdown = 7,
  kArcVmShutdownSuccess = 8,
  kTerminaVmStartup = 9,
  kTerminaVmStartupSuccess = 10,
  kTerminaVmShutdown = 11,
  kTerminaVmShutdownSuccess = 12,
  kParallelsVmStartup = 13,
  kParallelsVmStartupSuccess = 14,
  kParallelsVmShutdown = 15,
  kParallelsVmShutdownSuccess = 16,
  kSetVpnIntent = 17,
  kSetVpnIntentSuccess = 18,
  kConnectNamespace = 19,
  kConnectNamespaceSuccess = 20,
  kGetTrafficCounters = 21,
  kGetTrafficCountersSuccess = 22,
  kModifyPortRule = 23,
  kModifyPortRuleSuccess = 24,
  kGetDevices = 25,
  kGetDevicesSuccess = 26,
  kSetVpnLockdown = 27,
  kSetVpnLockdownSuccess = 28,
  kSetDnsRedirectionRule = 29,
  kSetDnsRedirectionRuleSuccess = 30,
  kCreateLocalOnlyNetwork = 31,
  kCreateLocalOnlyNetworkSuccess = 32,
  kCreateTetheredNetwork = 33,
  kCreateTetheredNetworkSuccess = 34,
  kGetDownstreamNetworkInfo = 35,
  kGetDownstreamNetworkInfoSuccess = 36,
  kBruschettaVmStartup = 37,
  kBruschettaVmStartupSuccess = 38,
  kBruschettaVmShutdown = 39,
  kBruschettaVmShutdownSuccess = 40,
  kBorealisVmStartup = 41,
  kBorealisVmStartupSuccess = 42,
  kBorealisVmShutdown = 43,
  kBorealisVmShutdownSuccess = 44,

  kMaxValue,
};

// UMA metrics events for |kArcServiceUmaEventMetrics|;
enum class ArcServiceUmaEvent {
  kUnknown = 0,
  kStart = 1,
  kStartSuccess = 2,
  kStartWithoutStop = 3,
  kStop = 4,
  kStopSuccess = 5,
  kStopBeforeStart = 6,
  kAddDevice = 7,
  kAddDeviceSuccess = 8,
  kSetVethMtuError = 10,
  kOneTimeContainerSetupError = 11,

  kMaxValue,
};

// UMA metrics events for |kTetheringDHCPServerUmaEventMetrics| and
// |kLocalOnlyNetworkDHCPServerUmaEventMetrics| (the same DHCP server
// implementation is reused).
enum class DHCPServerUmaEvent {
  kUnknown = 0,
  kStart = 1,
  kStartSuccess = 2,
  kStop = 3,
  kStopSuccess = 4,
  kDHCPMessageRequest = 5,
  kDHCPMessageAck = 6,
  kDHCPMessageNak = 7,
  kDHCPMessageDecline = 8,

  kMaxValue,
};

// UMA metrics events for both |kCreateTetheredNetworkUmaEventMetrics| and
// |kCreateLocalOnlyNetworkUmaEventMetrics| (the same implementation is
// reused).
enum class CreateDownstreamNetworkResult {
  kUnknown = 0,
  kSuccess = 1,
  kInternalError = 2,
  kInvalidRequest = 3,
  kInvalidArgument = 4,
  kDownstreamUsed = 5,
  kUpstreamUnknown = 6,
  kDHCPServerFailure = 7,
  kDatapathError = 8,

  kMaxValue,
};

}  // namespace patchpanel

#endif  // PATCHPANEL_METRICS_H_
