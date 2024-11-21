// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_METRICS_H_
#define SHILL_METRICS_H_

#include <cstdint>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include <chromeos/dbus/shill/dbus-constants.h>
#include <chromeos/net-base/mac_address.h>
#include <metrics/metrics_library.h>
#include <metrics/timer.h>

#include "shill/error.h"
#include "shill/mockable.h"
#include "shill/technology.h"
#include "shill/vpn/vpn_types.h"
#include "shill/wifi/ieee80211.h"

namespace shill {

// Represents a UMA metric name that can be defined by technology for a
// metric represented with EnumMetric or HistogramMetric, following the
// pattern "$kMetricPrefix.$TECH.$name" or "$kMetricPrefix.$name.$TECH"
// depending on the value of |location|.
// Note: This must be fully defined outside of the Metrics class to allow
// default member initialization for |location| within the class, e.g.
// MetricsNameByTechnology{"name"}.
struct MetricsNameByTechnology {
  enum class Location { kBeforeName, kAfterName };
  std::string_view name;
  Location location = Location::kBeforeName;
  bool operator==(const MetricsNameByTechnology& that) const = default;
};

class Metrics {
 public:
  using NameByTechnology = MetricsNameByTechnology;
  using TechnologyLocation = MetricsNameByTechnology::Location;

  // Helper type for describing a UMA enum metrics.
  // The template parameter is used for deriving the name of the metric. See
  // FixedName and NameByTechnology.
  template <typename N>
  struct EnumMetric {
    N n;
    int max;
    bool operator==(const EnumMetric<N>& that) const = default;
  };

  // Helper type for describing a UMA histogram metrics.
  // The template parameter is used for deriving the name of the metric. See
  // FixedName and NameByTechnology.
  template <typename N>
  struct HistogramMetric {
    N n;
    int min;
    int max;
    int num_buckets;
    bool operator==(const HistogramMetric<N>& that) const = default;
  };

  // Helper type for describing a UMA sparse histogram metrics.
  // The template parameter is used for deriving the name of the metric. See
  // FixedName and NameByTechnology.
  template <typename N>
  struct SparseMetric {
    N n;
    bool operator==(const SparseMetric<N>& that) const = default;
  };

  // Represents a fixed UMA metric name for a metric represented with
  // EnumMetric, HistogramMetric, or SparseMetric.
  struct FixedName {
    std::string_view name;
    bool operator==(const FixedName& that) const = default;
  };

  // Represents a UMA metric name by APN type.
  struct NameByApnType {
    std::string_view name;
    bool operator==(const NameByApnType& that) const = default;
  };

  // Represents a UMA metric name by VPN type.
  struct NameByVPNType {
    std::string_view name;
    bool operator==(const NameByVPNType& that) const = default;
  };

  // Represents a UMA metric name with a fixed prefix. Callers provide the
  // suffix for every call sites. This is convenient for group of metrics
  // like "Network.Shill.WiFi.RememberedSystemNetworkCount.*" that share a
  // common prefix.
  struct PrefixName {
    std::string_view prefix;
    bool operator==(const PrefixName& that) const = default;
  };

  enum WiFiChannel {
    kWiFiChannelUndef = 0,
    kWiFiChannel2412 = 1,
    kWiFiChannelMin24 = kWiFiChannel2412,
    kWiFiChannel2417 = 2,
    kWiFiChannel2422 = 3,
    kWiFiChannel2427 = 4,
    kWiFiChannel2432 = 5,
    kWiFiChannel2437 = 6,
    kWiFiChannel2442 = 7,
    kWiFiChannel2447 = 8,
    kWiFiChannel2452 = 9,
    kWiFiChannel2457 = 10,
    kWiFiChannel2462 = 11,
    kWiFiChannel2467 = 12,
    kWiFiChannel2472 = 13,
    kWiFiChannel2484 = 14,
    kWiFiChannelMax24 = kWiFiChannel2484,

    kWiFiChannel5180 = 15,
    kWiFiChannelMin5 = kWiFiChannel5180,
    kWiFiChannel5200 = 16,
    kWiFiChannel5220 = 17,
    kWiFiChannel5240 = 18,
    kWiFiChannel5260 = 19,
    kWiFiChannel5280 = 20,
    kWiFiChannel5300 = 21,
    kWiFiChannel5320 = 22,

    kWiFiChannel5500 = 23,
    kWiFiChannel5520 = 24,
    kWiFiChannel5540 = 25,
    kWiFiChannel5560 = 26,
    kWiFiChannel5580 = 27,
    kWiFiChannel5600 = 28,
    kWiFiChannel5620 = 29,
    kWiFiChannel5640 = 30,
    kWiFiChannel5660 = 31,
    kWiFiChannel5680 = 32,
    kWiFiChannel5700 = 33,

    kWiFiChannel5745 = 34,
    kWiFiChannel5765 = 35,
    kWiFiChannel5785 = 36,
    kWiFiChannel5805 = 37,
    kWiFiChannel5825 = 38,

    kWiFiChannel5170 = 39,
    kWiFiChannel5190 = 40,
    kWiFiChannel5210 = 41,
    kWiFiChannel5230 = 42,
    kWiFiChannelMax5 = kWiFiChannel5230,

    kWiFiChannel5955 = 43,
    kWiFiChannelMin6 = kWiFiChannel5955,
    kWiFiChannel5975 = 44,
    kWiFiChannel5995 = 45,
    kWiFiChannel6015 = 46,
    kWiFiChannel6035 = 47,
    kWiFiChannel6055 = 48,
    kWiFiChannel6075 = 49,
    kWiFiChannel6095 = 50,
    kWiFiChannel6115 = 51,
    kWiFiChannel6135 = 52,
    kWiFiChannel6155 = 53,
    kWiFiChannel6175 = 54,
    kWiFiChannel6195 = 55,
    kWiFiChannel6215 = 56,
    kWiFiChannel6235 = 57,
    kWiFiChannel6255 = 58,
    kWiFiChannel6275 = 59,
    kWiFiChannel6295 = 60,
    kWiFiChannel6315 = 61,
    kWiFiChannel6335 = 62,
    kWiFiChannel6355 = 63,
    kWiFiChannel6375 = 64,
    kWiFiChannel6395 = 65,
    kWiFiChannel6415 = 66,
    kWiFiChannel6435 = 67,
    kWiFiChannel6455 = 68,
    kWiFiChannel6475 = 69,
    kWiFiChannel6495 = 70,
    kWiFiChannel6515 = 71,
    kWiFiChannel6535 = 72,
    kWiFiChannel6555 = 73,
    kWiFiChannel6575 = 74,
    kWiFiChannel6595 = 75,
    kWiFiChannel6615 = 76,
    kWiFiChannel6635 = 77,
    kWiFiChannel6655 = 78,
    kWiFiChannel6675 = 79,
    kWiFiChannel6695 = 80,
    kWiFiChannel6715 = 81,
    kWiFiChannel6735 = 82,
    kWiFiChannel6755 = 83,
    kWiFiChannel6775 = 84,
    kWiFiChannel6795 = 85,
    kWiFiChannel6815 = 86,
    kWiFiChannel6835 = 87,
    kWiFiChannel6855 = 88,
    kWiFiChannel6875 = 89,
    kWiFiChannel6895 = 90,
    kWiFiChannel6915 = 91,
    kWiFiChannel6935 = 92,
    kWiFiChannel6955 = 93,
    kWiFiChannel6975 = 94,
    kWiFiChannel6995 = 95,
    kWiFiChannel7015 = 96,
    kWiFiChannel7035 = 97,
    kWiFiChannel7055 = 98,
    kWiFiChannel7075 = 99,
    kWiFiChannel7095 = 100,
    kWiFiChannel7115 = 101,
    kWiFiChannelMax6 = kWiFiChannel7115,

    /* NB: ignore old 11b bands 2312..2372 and 2512..2532 */
    /* NB: ignore regulated bands 4920..4980 and 5020..5160 */
    kWiFiChannelMax
  };

  enum WiFiFrequencyRange {
    kWiFiFrequencyRangeUndef = 0,
    kWiFiFrequencyRange24 = 1,
    kWiFiFrequencyRange5 = 2,
    kWiFiFrequencyRange6 = 3,

    kWiFiFrequencyRangeMax
  };

  enum WiFiNetworkPhyMode {
    kWiFiNetworkPhyModeUndef = 0,    // Unknown/undefined
    kWiFiNetworkPhyMode11a = 1,      // 802.11a
    kWiFiNetworkPhyMode11b = 2,      // 802.11b
    kWiFiNetworkPhyMode11g = 3,      // 802.11g
    kWiFiNetworkPhyMode11n = 4,      // 802.11n
    kWiFiNetworkPhyModeHalf = 5,     // PSB Half-width
    kWiFiNetworkPhyModeQuarter = 6,  // PSB Quarter-width
    kWiFiNetworkPhyMode11ac = 7,     // 802.11ac
    kWiFiNetworkPhyMode11ax = 8,     // 802.11ax
    kWiFiNetworkPhyMode11be = 9,     // 802.11be

    kWiFiNetworkPhyModeMax
  };

  enum EapOuterProtocol {
    kEapOuterProtocolUnknown = 0,
    kEapOuterProtocolLeap = 1,
    kEapOuterProtocolPeap = 2,
    kEapOuterProtocolTls = 3,
    kEapOuterProtocolTtls = 4,

    kEapOuterProtocolMax
  };
  static constexpr EnumMetric<NameByTechnology> kMetricNetworkEapOuterProtocol =
      {
          .n = NameByTechnology{"EapOuterProtocol"},
          .max = kEapOuterProtocolMax,
  };

  enum EapInnerProtocol {
    kEapInnerProtocolUnknown = 0,
    kEapInnerProtocolNone = 1,
    kEapInnerProtocolPeapMd5 = 2,
    kEapInnerProtocolPeapMschapv2 = 3,
    kEapInnerProtocolTtlsEapMd5 = 4,
    kEapInnerProtocolTtlsEapMschapv2 = 5,
    kEapInnerProtocolTtlsMschapv2 = 6,
    kEapInnerProtocolTtlsMschap = 7,
    kEapInnerProtocolTtlsPap = 8,
    kEapInnerProtocolTtlsChap = 9,

    kEapInnerProtocolMax
  };
  static constexpr EnumMetric<NameByTechnology> kMetricNetworkEapInnerProtocol =
      {
          .n = NameByTechnology{"EapInnerProtocol"},
          .max = kEapInnerProtocolMax,
  };

  enum WirelessSecurity {
    kWirelessSecurityUnknown = 0,
    kWirelessSecurityNone = 1,
    kWirelessSecurityWep = 2,
    kWirelessSecurityWpa = 3,
    // Value "802.11i/RSN" (4) is not used anymore.
    kWirelessSecurity8021x = 5,
    kWirelessSecurityPsk = 6,
    kWirelessSecurityWpa3 = 7,
    kWirelessSecurityWpaWpa2 = 8,
    kWirelessSecurityWpa2 = 9,
    kWirelessSecurityWpa2Wpa3 = 10,
    kWirelessSecurityWpaEnterprise = 11,
    kWirelessSecurityWpaWpa2Enterprise = 12,
    kWirelessSecurityWpa2Enterprise = 13,
    kWirelessSecurityWpa2Wpa3Enterprise = 14,
    kWirelessSecurityWpa3Enterprise = 15,
    kWirelessSecurityWpaAll = 16,
    kWirelessSecurityWpaAllEnterprise = 17,
    kWirelessSecurityWepEnterprise = 18,
    kWirelessSecurityTransOwe = 19,
    kWirelessSecurityOwe = 20,

    kWirelessSecurityMax
  };

  enum WirelessSecurityChange {
    kWirelessSecurityChangeWpa3ToWpa23 = 0,
    kWirelessSecurityChangeWpa3ToWpa123 = 1,
    kWirelessSecurityChangeWpa23ToWpa123 = 2,
    kWirelessSecurityChangeWpa2ToWpa12 = 3,
    kWirelessSecurityChangeEAPWpa3ToWpa23 = 4,
    kWirelessSecurityChangeEAPWpa3ToWpa123 = 5,
    kWirelessSecurityChangeEAPWpa23ToWpa123 = 6,
    kWirelessSecurityChangeEAPWpa2ToWpa12 = 7,
    kWirelessSecurityChangeWpa12ToWpa123 = 8,
    kWirelessSecurityChangeEAPWpa12ToWpa123 = 9,

    kWirelessSecurityChangeMax
  };

  // Possible result of a single network validation attempt. See b/236387876 for
  // details.
  enum PortalDetectorResult {
    kPortalDetectorResultUnknown = 0,
    kPortalDetectorResultConnectionFailure = 1,
    kPortalDetectorResultDNSFailure = 2,
    kPortalDetectorResultDNSTimeout = 3,
    kPortalDetectorResultHTTPFailure = 4,
    kPortalDetectorResultHTTPTimeout = 5,
    kPortalDetectorResultContentFailure = 6,
    kPortalDetectorResultContentTimeout = 7,
    kPortalDetectorResultRedirectFound = 8,
    kPortalDetectorResultRedirectNoUrl = 9,
    kPortalDetectorResultHTTPSFailure = 10,
    kPortalDetectorResultNoConnectivity = 11,
    kPortalDetectorResultOnline = 12,
    kPortalDetectorResultMax = 13,
  };
  // Network validation result recorded for the first network validation attempt
  // on a newly connected network.
  static constexpr EnumMetric<NameByTechnology> kPortalDetectorInitialResult = {
      .n = NameByTechnology{"PortalDetector.InitialResult",
                            TechnologyLocation::kAfterName},
      .max = kPortalDetectorResultMax,
  };
  // Network validation result recorded for any network validation attempt after
  // the first initial attempt.
  static constexpr EnumMetric<NameByTechnology> kPortalDetectorRetryResult = {
      .n = NameByTechnology{"PortalDetector.RepeatResult",
                            TechnologyLocation::kAfterName},
      .max = kPortalDetectorResultMax,
  };

  // Result of network validation aggregated until Internet connectivity has
  // been checked or until disconnection.
  enum PortalDetectorAggregateResult {
    kPortalDetectorAggregateResultUnknown = 0,
    kPortalDetectorAggregateResultNoConnectivity = 1,
    // Deprecated in m123. "partial connectivity" was changed to "portal
    // suspected" in m121.
    // kPortalDetectorAggregateResultPartialConnectivity = 2,
    kPortalDetectorAggregateResultRedirect = 3,
    // Deprecated in m123. "partial connectivity" was changed to "portal
    // suspected" in m121.
    // kPortalDetectorAggregateResultInternetAfterPartialConnectivity = 4,
    kPortalDetectorAggregateResultInternetAfterRedirect = 5,
    kPortalDetectorAggregateResultInternet = 6,
    kPortalDetectorAggregateResultPortalSuspected = 7,
    kPortalDetectorAggregateResultInternetAfterPortalSuspected = 8,

    kPortalDetectorAggregateResultMax,
  };
  static constexpr EnumMetric<NameByTechnology> kPortalDetectorAggregateResult =
      {
          .n = {"PortalDetector.AggregateResult",
                TechnologyLocation::kAfterName},
          .max = kPortalDetectorAggregateResultMax,
  };

  // HTTP response codes of the HTTP probe sent for a network validation
  // attempt. The values recorded are a mix of valid HTTP response codes in the
  // [100, 599] range and special defined values.
  static constexpr SparseMetric<NameByTechnology>
      kPortalDetectorHTTPResponseCode = {
          .n = NameByTechnology{"PortalDetector.HTTPResponseCode",
                                TechnologyLocation::kAfterName},
  };
  // Value used with |kPortalDetectorHTTPResponseCode| to indicate an invalid
  // response code outside the [100, 599] range.
  static constexpr int kPortalDetectorHTTPResponseCodeInvalid = 0;
  // Value used with |kPortalDetectorHTTPResponseCode| to indicate a 302 or 307
  // response code when the Location header was missing or invalid.
  static constexpr int kPortalDetectorHTTPResponseCodeIncompleteRedirect = 1;
  // Value used with |kPortalDetectorHTTPResponseCode| to indicate a 200
  // response code when the Content-Length header is invalid.
  static constexpr int kPortalDetectorHTTPResponseCodeNoContentLength200 = 2;

  // Histogram of HTTP Content-Length values for network validation HTTP probes
  // which have received a HTTP 200 response status code.
  static constexpr HistogramMetric<NameByTechnology>
      kPortalDetectorHTTPResponseContentLength = {
          .n = NameByTechnology{"PortalDetector.HTTPResponseContentLength",
                                TechnologyLocation::kAfterName},
          .min = 1,
          .max = 10000,
          .num_buckets = 30,
  };

  // patchpanel::NeighborLinkMonitor statistics.
  enum NeighborLinkMonitorFailure {
    kNeighborLinkMonitorFailureUnknown = 0,
    kNeighborIPv4GatewayFailure = 1,
    kNeighborIPv4DNSServerFailure = 2,
    kNeighborIPv4GatewayAndDNSServerFailure = 3,
    kNeighborIPv6GatewayFailure = 4,
    kNeighborIPv6DNSServerFailure = 5,
    kNeighborIPv6GatewayAndDNSServerFailure = 6,

    kNeighborLinkMonitorFailureMax
  };
  static constexpr EnumMetric<NameByTechnology>
      kMetricNeighborLinkMonitorFailure = {
          .n = NameByTechnology{"NeighborLinkMonitorFailure"},
          .max = kNeighborLinkMonitorFailureMax,
  };

  enum WiFiApChannelSwitch {
    kWiFiApChannelSwitchUndef = 0,
    kWiFiApChannelSwitch24To24 = 1,
    kWiFiApChannelSwitch24To5 = 2,
    kWiFiApChannelSwitch5To24 = 3,
    kWiFiApChannelSwitch5To5 = 4,

    kWiFiApChannelSwitchMax
  };
  static constexpr EnumMetric<FixedName> kMetricApChannelSwitch = {
      .n = FixedName{"Network.Shill.WiFi.ApChannelSwitch"},
      .max = kWiFiApChannelSwitchMax,
  };

  // AP 802.11r support statistics.
  enum WiFiAp80211rSupport {
    kWiFiAp80211rNone = 0,
    kWiFiAp80211rOTA = 1,
    kWiFiAp80211rOTDS = 2,

    kWiFiAp80211rMax
  };
  static constexpr EnumMetric<FixedName> kMetricAp80211rSupport = {
      .n = FixedName{"Network.Shill.WiFi.Ap80211rSupport"},
      .max = kWiFiAp80211rMax,
  };

  // AP stream classification support metric.
  enum WiFiApSCSupport {
    kWiFiApSCUnsupported = 0,
    kWiFiApSCS = 1,
    kWiFiApMSCS = 2,
    kWiFiApSCBoth = 3,

    kWiFiApSCMax
  };
  static constexpr EnumMetric<FixedName> kMetricApSCSupport = {
      .n = FixedName{"Network.Shill.WiFi.ApSCSupport"},
      .max = kWiFiApSCMax,
  };

  // Alternate EDCA support metric.
  static constexpr char kMetricApAlternateEDCASupport[] =
      "Network.Shill.WiFi.ApAlternateEDCASupport";

  enum WiFiRoamComplete {
    kWiFiRoamSuccess = 0,
    kWiFiRoamFailure = 1,

    kWiFiRoamCompleteMax
  };

  enum WiFiCQMReason {
    kWiFiCQMPacketLoss = 0,
    kWiFiCQMBeaconLoss = 1,

    kWiFiCQMMax
  };

  enum WiFiReasonType {
    kReasonCodeTypeByAp,
    kReasonCodeTypeByClient,
    kReasonCodeTypeByUser,
    kReasonCodeTypeConsideredDead,
    kReasonCodeTypeMax
  };
  static constexpr EnumMetric<FixedName> kMetricLinkClientDisconnectType = {
      .n = FixedName{"Network.Shill.WiFi.ClientDisconnectType"},
      .max = kReasonCodeTypeMax,
  };
  static constexpr EnumMetric<FixedName> kMetricLinkApDisconnectType = {
      .n = FixedName{"Network.Shill.WiFi.ApDisconnectType"},
      .max = kReasonCodeTypeMax,
  };

  enum WiFiDisconnectByWhom { kDisconnectedByAp, kDisconnectedNotByAp };

  enum WiFiScanResult {
    kScanResultProgressiveConnected,
    kScanResultProgressiveErrorAndFullFoundNothing,
    kScanResultProgressiveErrorButFullConnected,
    kScanResultProgressiveAndFullFoundNothing,
    kScanResultProgressiveAndFullConnected,
    kScanResultFullScanFoundNothing,
    kScanResultFullScanConnected,
    kScanResultInternalError,
    kScanResultMax
  };
  static constexpr EnumMetric<FixedName> kMetricScanResult = {
      .n = FixedName{"Network.Shill.WiFi.ScanResult"},
      .max = kScanResultMax,
  };

  enum Cellular3GPPRegistrationDelayedDrop {
    kCellular3GPPRegistrationDelayedDropPosted = 0,
    kCellular3GPPRegistrationDelayedDropCanceled = 1,
    kCellular3GPPRegistrationDelayedDropMax
  };
  static constexpr EnumMetric<FixedName>
      kMetricCellular3GPPRegistrationDelayedDrop = {
          .n = FixedName{"Network.Shill.Cellular.3GPPRegistrationDelayedDrop"},
          .max = kCellular3GPPRegistrationDelayedDropMax,
  };

  enum CellularApnSource {
    kCellularApnSourceMoDb = 0,
    kCellularApnSourceUi = 1,
    kCellularApnSourceModem = 2,
    kCellularApnSourceFallback = 3,
    kCellularApnSourceAdmin = 4,
    kCellularApnSourceMax
  };

  enum CellularDropTechnology {
    kCellularDropTechnology1Xrtt = 0,
    kCellularDropTechnologyEdge = 1,
    kCellularDropTechnologyEvdo = 2,
    kCellularDropTechnologyGprs = 3,
    kCellularDropTechnologyGsm = 4,
    kCellularDropTechnologyHspa = 5,
    kCellularDropTechnologyHspaPlus = 6,
    kCellularDropTechnologyLte = 7,
    kCellularDropTechnologyUmts = 8,
    kCellularDropTechnologyUnknown = 9,
    kCellularDropTechnology5gNr = 10,
    kCellularDropTechnologyMax
  };
  static constexpr EnumMetric<FixedName> kMetricCellularDrop = {
      .n = FixedName{"Network.Shill.Cellular.Drop"},
      .max = kCellularDropTechnologyMax,
  };

  // These metrics enum values should not be renumbered or overwritten.
  enum CellularEntitlementCheck {
    kCellularEntitlementCheckAllowed = 0,
    kCellularEntitlementCheckInProgress = 1,
    kCellularEntitlementCheckFailedToBuildPayload = 2,
    kCellularEntitlementCheckFailedToParseIp = 3,
    kCellularEntitlementCheckUnexpectedRequestId = 4,
    kCellularEntitlementCheckUserNotAllowedToTether = 5,
    kCellularEntitlementCheckHttpSyntaxErrorOnServer = 6,
    kCellularEntitlementCheckUnrecognizedUser = 7,
    kCellularEntitlementCheckInternalErrorOnServer = 8,
    kCellularEntitlementCheckUnrecognizedErrorCode = 9,
    kCellularEntitlementCheckUnrecognizedHttpStatusCode = 10,
    kCellularEntitlementCheckHttpRequestError = 11,
    kCellularEntitlementCheckIllegalInProgress = 12,
    kCellularEntitlementCheckNotAllowedByModb = 13,
    kCellularEntitlementCheckUnknownCarrier = 14,
    kCellularEntitlementCheckNoIp = 15,
    kCellularEntitlementCheckNoCellularDevice = 16,
    kCellularEntitlementCheckNoNetwork = 17,
    kCellularEntitlementCheckNetworkNotConnected = 18,
    kCellularEntitlementCheckNetworkNotOnline = 19,
    kCellularEntitlementCheckNotAllowedOnVariant = 20,
    kCellularEntitlementCheckMax
  };
  static constexpr EnumMetric<FixedName> kMetricCellularEntitlementCheck = {
      .n = FixedName{"Network.Shill.Cellular.EntitlementCheck"},
      .max = kCellularEntitlementCheckMax,
  };

  // These values are persisted to logs for
  // Network.Shill.Cellular.ConnectResult. CellularConnectResult entries should
  // not be renumbered and numeric values should never be reused.
  enum class CellularConnectResult {
    kCellularConnectResultSuccess = 0,
    kCellularConnectResultUnknown = 1,
    kCellularConnectResultWrongState = 2,
    kCellularConnectResultOperationFailed = 3,
    kCellularConnectResultAlreadyConnected = 4,
    kCellularConnectResultNotRegistered = 5,
    kCellularConnectResultNotOnHomeNetwork = 6,
    kCellularConnectResultIncorrectPin = 7,
    kCellularConnectResultPinRequired = 8,
    kCellularConnectResultPinBlocked = 9,
    kCellularConnectResultInvalidApn = 10,
    kCellularConnectResultInternalError = 11,
    kCellularConnectResultMax
  };

  static constexpr EnumMetric<NameByApnType> kMetricCellularConnectResult = {
      .n = NameByApnType{"ConnectResult"},
      .max = static_cast<int>(CellularConnectResult::kCellularConnectResultMax),
  };

  enum CellularRoamingState {
    kCellularRoamingStateUnknown = 0,
    kCellularRoamingStateHome = 1,
    kCellularRoamingStateRoaming = 2,
    kCellularRoamingStateMax
  };
  // The |CellularApnType| values represent a bit mask, so each following value
  // has to be the next multiple of 2- i.e., 1, 2, 4, 8, ...
  enum class CellularApnType {
    kCellularApnTypeDefault = 1,
    kCellularApnTypeIA = 2,
    kCellularApnTypeDun = 4,
  };

  enum EthernetDriver {
    kEthernetDriverUnknown = 0,
    kEthernetDriverAlx = 1,
    kEthernetDriverAqc111 = 2,
    kEthernetDriverAsix = 3,
    kEthernetDriverAtlantic = 4,
    kEthernetDriverAx88179_178a = 5,
    kEthernetDriverCdcEem = 6,
    kEthernetDriverCdcEther = 7,
    kEthernetDriverCdcMbim = 8,
    kEthernetDriverCdcNcm = 9,
    kEthernetDriverDm9601 = 10,
    kEthernetDriverE100 = 11,
    kEthernetDriverE1000 = 12,
    kEthernetDriverE1000e = 13,
    kEthernetDriverIgb = 14,
    kEthernetDriverIgbvf = 15,
    kEthernetDriverIgc = 16,
    kEthernetDriverIpheth = 17,
    kEthernetDriverJme = 18,
    kEthernetDriverMcs7830 = 19,
    kEthernetDriverPegasus = 20,
    kEthernetDriverR8152 = 21,
    kEthernetDriverR8169 = 22,
    kEthernetDriverRtl8150 = 23,
    kEthernetDriverSmsc75xx = 24,
    kEthernetDriverSmsc95xx = 25,
    kEthernetDriverTg3 = 26,
    kEthernetDriverError = 27,
    kEthernetDriverRndisHost = 28,
    kEthernetDriverAtl1c = 29,
    kEthernetDriverSky2 = 30,
    kEthernetDriverStGmac = 31,
    kEthernetDriverNForce = 32,
    kEthernetDriverR8153 = 33,
    kEthernetDriverMax
  };
  static constexpr EnumMetric<FixedName> kMetricEthernetDriver = {
      .n = FixedName{"Network.Shill.Ethernet.Driver"},
      .max = kEthernetDriverMax,
  };

  // Remote authentication statistics for VPN connections.
  enum VpnRemoteAuthenticationType {
    kVpnRemoteAuthenticationTypeOpenVpnDefault = 0,
    kVpnRemoteAuthenticationTypeOpenVpnCertificate = 1,
    kVpnRemoteAuthenticationTypeL2tpIpsecDefault = 2,
    kVpnRemoteAuthenticationTypeL2tpIpsecCertificate = 3,
    kVpnRemoteAuthenticationTypeL2tpIpsecPsk = 4,
    kVpnRemoteAuthenticationTypeMax
  };
  static constexpr EnumMetric<FixedName> kMetricVpnRemoteAuthenticationType = {
      .n = FixedName{"Network.Shill.Vpn.RemoteAuthenticationType"},
      .max = kVpnRemoteAuthenticationTypeMax,
  };

  // User authentication type statistics for VPN connections.
  enum VpnUserAuthenticationType {
    kVpnUserAuthenticationTypeOpenVpnNone = 0,
    kVpnUserAuthenticationTypeOpenVpnCertificate = 1,
    kVpnUserAuthenticationTypeOpenVpnUsernamePassword = 2,
    kVpnUserAuthenticationTypeOpenVpnUsernamePasswordOtp = 3,
    kVpnUserAuthenticationTypeOpenVpnUsernameToken = 7,
    kVpnUserAuthenticationTypeL2tpIpsecNone = 4,
    kVpnUserAuthenticationTypeL2tpIpsecCertificate = 5,
    kVpnUserAuthenticationTypeL2tpIpsecUsernamePassword = 6,
    kVpnUserAuthenticationTypeMax
  };
  static constexpr EnumMetric<FixedName> kMetricVpnUserAuthenticationType = {
      .n = FixedName{"Network.Shill.Vpn.UserAuthenticationType"},
      .max = kVpnUserAuthenticationTypeMax,
  };

  // IKEv2 IPsec authentication statistics.
  enum VpnIpsecAuthenticationType {
    kVpnIpsecAuthenticationTypeUnknown = 0,
    kVpnIpsecAuthenticationTypePsk = 1,
    kVpnIpsecAuthenticationTypeEap = 2,
    kVpnIpsecAuthenticationTypeCertificate = 3,
    kVpnIpsecAuthenticationTypeMax
  };
  static constexpr EnumMetric<FixedName> kMetricVpnIkev2AuthenticationType = {
      .n = FixedName{"Network.Shill.Vpn.Ikev2.AuthenticationType"},
      .max = kVpnIpsecAuthenticationTypeMax,
  };

  // L2TP/IPsec group usage statistics.
  enum VpnL2tpIpsecTunnelGroupUsage {
    kVpnL2tpIpsecTunnelGroupUsageNo = 0,
    kVpnL2tpIpsecTunnelGroupUsageYes = 1,
    kVpnL2tpIpsecTunnelGroupUsageMax
  };
  static constexpr EnumMetric<FixedName> kMetricVpnL2tpIpsecTunnelGroupUsage = {
      .n = FixedName{"Network.Shill.Vpn.L2tpIpsecTunnelGroupUsage"},
      .max = kVpnL2tpIpsecTunnelGroupUsageMax,
  };

  enum IPType {
    kIPTypeUnknown = 0,
    kIPTypeIPv4Only = 1,
    kIPTypeIPv6Only = 2,
    kIPTypeDualStack = 3,
    kIPTypeMax
  };
  static constexpr EnumMetric<NameByTechnology> kMetricIPType = {
      .n = NameByTechnology{"IPType"},
      .max = kIPTypeMax,
  };

  // This enum contains the encryption algorithms we are using for IPsec now,
  // but not the complete list of algorithms which are supported by strongswan.
  // It is the same for the following enums for integrity algorithms and DH
  // groups.
  enum VpnIpsecEncryptionAlgorithm {
    kVpnIpsecEncryptionAlgorithmUnknown = 0,

    kVpnIpsecEncryptionAlgorithm_AES_CBC_128 = 1,
    kVpnIpsecEncryptionAlgorithm_AES_CBC_192 = 2,
    kVpnIpsecEncryptionAlgorithm_AES_CBC_256 = 3,
    kVpnIpsecEncryptionAlgorithm_CAMELLIA_CBC_128 = 4,
    kVpnIpsecEncryptionAlgorithm_CAMELLIA_CBC_192 = 5,
    kVpnIpsecEncryptionAlgorithm_CAMELLIA_CBC_256 = 6,
    kVpnIpsecEncryptionAlgorithm_3DES_CBC = 7,
    kVpnIpsecEncryptionAlgorithm_AES_GCM_16_128 = 8,
    kVpnIpsecEncryptionAlgorithm_AES_GCM_16_192 = 9,
    kVpnIpsecEncryptionAlgorithm_AES_GCM_16_256 = 10,
    kVpnIpsecEncryptionAlgorithm_AES_GCM_12_128 = 11,
    kVpnIpsecEncryptionAlgorithm_AES_GCM_12_192 = 12,
    kVpnIpsecEncryptionAlgorithm_AES_GCM_12_256 = 13,
    kVpnIpsecEncryptionAlgorithm_AES_GCM_8_128 = 14,
    kVpnIpsecEncryptionAlgorithm_AES_GCM_8_192 = 15,
    kVpnIpsecEncryptionAlgorithm_AES_GCM_8_256 = 16,

    kVpnIpsecEncryptionAlgorithmMax,
  };
  static constexpr EnumMetric<NameByVPNType> kMetricVpnIkeEncryptionAlgorithm =
      {
          .n = NameByVPNType{"IkeEncryptionAlgorithm"},
          .max = kVpnIpsecEncryptionAlgorithmMax,
  };
  static constexpr EnumMetric<NameByVPNType> kMetricVpnEspEncryptionAlgorithm =
      {
          .n = NameByVPNType{"EspEncryptionAlgorithm"},
          .max = kVpnIpsecEncryptionAlgorithmMax,
  };

  enum VpnIpsecIntegrityAlgorithm {
    kVpnIpsecIntegrityAlgorithmUnknown = 0,

    kVpnIpsecIntegrityAlgorithm_HMAC_SHA2_256_128 = 1,
    kVpnIpsecIntegrityAlgorithm_HMAC_SHA2_384_192 = 2,
    kVpnIpsecIntegrityAlgorithm_HMAC_SHA2_512_256 = 3,
    kVpnIpsecIntegrityAlgorithm_HMAC_SHA1_96 = 4,
    kVpnIpsecIntegrityAlgorithm_AES_XCBC_96 = 5,
    kVpnIpsecIntegrityAlgorithm_AES_CMAC_96 = 6,

    kVpnIpsecIntegrityAlgorithmMax,
  };
  static constexpr EnumMetric<NameByVPNType> kMetricVpnIkeIntegrityAlgorithm = {
      .n = NameByVPNType{"IkeIntegrityAlgorithm"},
      .max = kVpnIpsecIntegrityAlgorithmMax,
  };
  static constexpr EnumMetric<NameByVPNType> kMetricVpnEspIntegrityAlgorithm = {
      .n = NameByVPNType{"EspIntegrityAlgorithm"},
      .max = kVpnIpsecIntegrityAlgorithmMax,
  };

  enum VpnIpsecDHGroup {
    kVpnIpsecDHGroupUnknown = 0,

    kVpnIpsecDHGroup_ECP_256 = 1,
    kVpnIpsecDHGroup_ECP_384 = 2,
    kVpnIpsecDHGroup_ECP_521 = 3,
    kVpnIpsecDHGroup_ECP_256_BP = 4,
    kVpnIpsecDHGroup_ECP_384_BP = 5,
    kVpnIpsecDHGroup_ECP_512_BP = 6,
    kVpnIpsecDHGroup_CURVE_25519 = 7,
    kVpnIpsecDHGroup_CURVE_448 = 8,
    kVpnIpsecDHGroup_MODP_1024 = 9,
    kVpnIpsecDHGroup_MODP_1536 = 10,
    kVpnIpsecDHGroup_MODP_2048 = 11,
    kVpnIpsecDHGroup_MODP_3072 = 12,
    kVpnIpsecDHGroup_MODP_4096 = 13,
    kVpnIpsecDHGroup_MODP_6144 = 14,
    kVpnIpsecDHGroup_MODP_8192 = 15,

    kVpnIpsecDHGroupMax,
  };
  static constexpr EnumMetric<NameByVPNType> kMetricVpnIkeDHGroup = {
      .n = NameByVPNType{"IkeDHGroup"},
      .max = kVpnIpsecDHGroupMax,
  };

  // OpenVPN cipher algorithm used after negotiating with server.
  enum VpnOpenVPNCipher {
    kVpnOpenVPNCipherUnknown = 0,
    kVpnOpenVPNCipher_BF_CBC = 1,
    kVpnOpenVPNCipher_AES_256_GCM = 2,
    kVpnOpenVPNCipher_AES_128_GCM = 3,
    kVpnOpenVPNCipherMax
  };
  static constexpr EnumMetric<FixedName> kMetricVpnOpenVPNCipher = {
      .n = FixedName{"Network.Shill.Vpn.OpenVPNCipher"},
      .max = kVpnOpenVPNCipherMax,
  };

  // Key pair source (e.g., user input) used in a WireGuard Connection.
  enum VpnWireGuardKeyPairSource {
    kVpnWireguardKeyPairSourceUnknown = 0,
    kVpnWireGuardKeyPairSourceUserInput = 1,
    kVpnWireGuardKeyPairSourceSoftwareGenerated = 2,
    kVpnWireGuardKeyPairSourceMax
  };
  static constexpr EnumMetric<FixedName> kMetricVpnWireGuardKeyPairSource = {
      .n = FixedName{"Network.Shill.Vpn.WireGuardKeyPairSource"},
      .max = kVpnWireGuardKeyPairSourceMax,
  };

  // Allowed IPs type used in a WireGuard connection.
  enum VpnWireGuardAllowedIPsType {
    kVpnWireGuardAllowedIPsTypeHasDefaultRoute = 0,
    kVpnWireGuardAllowedIPsTypeNoDefaultRoute = 1,
    kVpnWireGuardAllowedIPsTypeMax
  };
  static constexpr EnumMetric<FixedName> kMetricVpnWireGuardAllowedIPsType = {
      .n = FixedName{"Network.Shill.Vpn.WireGuardAllowedIPsType"},
      .max = kVpnWireGuardAllowedIPsTypeMax,
  };

  // Result of a connection initiated by Service::UserInitiatedConnect.
  enum UserInitiatedConnectionResult {
    kUserInitiatedConnectionResultSuccess = 0,
    kUserInitiatedConnectionResultFailure = 1,
    kUserInitiatedConnectionResultAborted = 2,
    kUserInitiatedConnectionResultMax
  };
  static constexpr EnumMetric<FixedName>
      kMetricWifiUserInitiatedConnectionResult = {
          .n = FixedName{"Network.Shill.WiFi.UserInitiatedConnectionResult"},
          .max = kUserInitiatedConnectionResultMax,
  };

  // Reason when a connection initiated by Service::UserInitiatedConnect fails.
  enum UserInitiatedConnectionFailureReason {
    kUserInitiatedConnectionFailureReasonBadPassphrase = 1,
    kUserInitiatedConnectionFailureReasonBadWEPKey = 2,
    kUserInitiatedConnectionFailureReasonConnect = 3,
    kUserInitiatedConnectionFailureReasonDHCP = 4,
    kUserInitiatedConnectionFailureReasonDNSLookup = 5,
    kUserInitiatedConnectionFailureReasonEAPAuthentication = 6,
    kUserInitiatedConnectionFailureReasonEAPLocalTLS = 7,
    kUserInitiatedConnectionFailureReasonEAPRemoteTLS = 8,
    kUserInitiatedConnectionFailureReasonOutOfRange = 9,
    kUserInitiatedConnectionFailureReasonPinMissing = 10,
    kUserInitiatedConnectionFailureReasonUnknown = 11,
    kUserInitiatedConnectionFailureReasonNone = 12,
    kUserInitiatedConnectionFailureReasonNotAssociated = 13,
    kUserInitiatedConnectionFailureReasonNotAuthenticated = 14,
    kUserInitiatedConnectionFailureReasonTooManySTAs = 15,
    kUserInitiatedConnectionFailureReasonMax
  };
  static constexpr EnumMetric<FixedName>
      kMetricWifiUserInitiatedConnectionFailureReason = {
          .n =
              FixedName{
                  "Network.Shill.WiFi.UserInitiatedConnectionFailureReason"},
          .max = Metrics::kUserInitiatedConnectionFailureReasonMax,
  };

  // Device presence.
  enum DevicePresenceStatus {
    kDevicePresenceStatusNo = 0,
    kDevicePresenceStatusYes = 1,
    kDevicePresenceStatusMax
  };
  static constexpr EnumMetric<NameByTechnology> kMetricDevicePresenceStatus = {
      .n = NameByTechnology{"DevicePresenceStatus"},
      .max = kDevicePresenceStatusMax,
  };

  // The state if a technology--Ethernet, WiFi, Cellular--is enabled or not
  enum TechnologyEnabled {
    kTechnologyEnabledNo = 0,
    kTechnologyEnabledYes = 1,
    kTechnologyEnabledMax
  };
  // Metric indicating if a technology is enabled or not. The enabled state is
  // checked periodically together with |DevicePresenceStatus|. The checking
  // frequency is every |kDeviceStatusCheckInterval|.
  static constexpr EnumMetric<NameByTechnology> kMetricTechnologyEnabled = {
      .n = NameByTechnology{"TechnologyEnabled"},
      .max = kTechnologyEnabledMax,
  };

  enum DeviceTechnologyType {
    kDeviceTechnologyTypeUnknown = 0,
    kDeviceTechnologyTypeEthernet = 1,
    kDeviceTechnologyTypeWifi = 2,
    // deprecated: kDeviceTechnologyTypeWimax = 3,
    kDeviceTechnologyTypeCellular = 4,
    kDeviceTechnologyTypeMax
  };

  // These correspond to entries in Chrome's tools/metrics/histograms/enums.xml.
  // Please do not remove entries (append 'Deprecated' instead), and update the
  // enums.xml file when entries are added.
  enum NetworkServiceError {
    kNetworkServiceErrorNone = 0,
    kNetworkServiceErrorAAA = 1,
    kNetworkServiceErrorActivation = 2,
    kNetworkServiceErrorBadPassphrase = 3,
    kNetworkServiceErrorBadWEPKey = 4,
    kNetworkServiceErrorConnect = 5,
    kNetworkServiceErrorDHCP = 6,
    kNetworkServiceErrorDNSLookup = 7,
    kNetworkServiceErrorEAPAuthentication = 8,
    kNetworkServiceErrorEAPLocalTLS = 9,
    kNetworkServiceErrorEAPRemoteTLS = 10,
    kNetworkServiceErrorHTTPGet = 11,
    kNetworkServiceErrorIPsecCertAuth = 12,
    kNetworkServiceErrorIPsecPSKAuth = 13,
    kNetworkServiceErrorInternal = 14,
    kNetworkServiceErrorNeedEVDO = 15,
    kNetworkServiceErrorNeedHomeNetwork = 16,
    kNetworkServiceErrorOTASP = 17,
    kNetworkServiceErrorOutOfRange = 18,
    kNetworkServiceErrorPPPAuth = 19,
    kNetworkServiceErrorPinMissing = 20,
    kNetworkServiceErrorUnknown = 21,
    kNetworkServiceErrorNotAssociated = 22,
    kNetworkServiceErrorNotAuthenticated = 23,
    kNetworkServiceErrorTooManySTAs = 24,
    kNetworkServiceErrorDisconnect = 25,
    kNetworkServiceErrorSimLocked = 26,
    kNetworkServiceErrorNotRegistered = 27,
    kNetworkServiceErrorInvalidAPN = 28,
    kNetworkServiceErrorSimCarrierLocked = 29,
    kNetworkServiceErrorDelayedConnectSetup = 30,
    kNetworkServiceErrorSuspectInactiveSim = 31,
    kNetworkServiceErrorSuspectSubscriptionError = 32,
    kNetworkServiceErrorSuspectModemDisallowed = 33,
    kNetworkServiceErrorMax
  };
  static constexpr EnumMetric<NameByTechnology> kMetricNetworkServiceError = {
      .n = NameByTechnology{"ServiceErrors"},
      .max = Metrics::kNetworkServiceErrorMax,
  };
  static constexpr EnumMetric<FixedName> kMetricPasspointConnectionResult = {
      .n = FixedName{"Network.Shill.WiFi.Passpoint.ConnectionResult"},
      .max = Metrics::kNetworkServiceErrorMax,
  };

  // Corresponds to RegulatoryDomain enum values in
  // /chromium/src/tools/metrics/histograms/enums.xml.
  // kRegDom00, kRegDom99, kRegDom98 and kRegDom97 are special alpha2 codes
  enum RegulatoryDomain {
    kRegDom00 = 1,
    kCountryCodeInvalid = 678,
    kRegDom99 = 679,
    kRegDom98 = 680,
    kRegDom97 = 681,
    kRegDomMaxValue
  };

  // Hotspot 2.0 version number metric.
  enum HS20Support {
    kHS20Unsupported = 0,
    kHS20VersionInvalid = 1,
    kHS20Version1 = 2,
    kHS20Version2 = 3,
    kHS20Version3 = 4,
    kHS20SupportMax
  };
  static constexpr EnumMetric<FixedName> kMetricHS20Support = {
      .n = FixedName{"Network.Shill.WiFi.HS20Support"},
      .max = kHS20SupportMax,
  };

  // Is the WiFi adapter detected on the system in the allowlist of adapters
  // that can be reported through structured metrics or not?
  enum WiFiAdapterInAllowlist {
    kNotInAllowlist = 0,
    kInAVL = 1,
    kInAllowlist = 2,
    kAllowlistMax
  };
  static constexpr EnumMetric<FixedName> kMetricAdapterInfoAllowlisted = {
      .n = FixedName{"Network.Shill.WiFi.AdapterAllowlisted"},
      .max = kAllowlistMax,
  };

  static constexpr int kTimerHistogramNumBuckets = 50;
  static constexpr int kTimerHistogramNumBucketsLarge = 150;

  static constexpr HistogramMetric<NameByTechnology>
      kMetricDHCPv4ProvisionDurationMillis = {
          .n = NameByTechnology{"DHCPv4ProvisionDurationMillis"},
          .min = 1,
          .max = 30000,  // 30 seconds
          .num_buckets = kTimerHistogramNumBuckets,
  };

  static constexpr HistogramMetric<NameByTechnology>
      kMetricSLAACProvisionDurationMillis = {
          .n = NameByTechnology{"SLAACProvisionDurationMillis"},
          .min = 1,
          .max = 30000,  // 30 seconds
          .num_buckets = kTimerHistogramNumBuckets,
  };

  // WiFi disconnect type, indicates the source of the WiFi disconnection.
  enum WiFiDisconnectType {
    // The disconnection is due to a system error
    kWiFiDisconnectTypeSystem = 0,
    // WiFi is disconnected in UI by the user
    kWiFiDisconnectTypeUser = 1,
    // System suspend disconnects WiFi
    kWiFiDisconnectTypeSuspend = 2,
    // Selecting a new network will disconnect from the current network first
    kWiFiDisconnectTypeSelectNetwork = 3,
    // WiFi is disabled or stopped
    kWiFiDisconnectTypeDisable = 4,
    // WiFi disconnection due to unloading the WiFi service
    kWiFiDisconnectTypeUnload = 5,
    // WiFi disconnects when Ethernet is available if DisconnectWiFiOnEthernet
    // is enabled
    kWiFiDisconnectTypeEthernet = 6,
    // WiFi disconnection due to clearing credentials
    kWiFiDisconnectTypeClearCredential = 7,
    // Disconnect from a WiFi service if it has no endpoint left
    kWiFiDisconnectTypeNoEndpointLeft = 8,
    // Disconnect from a WiFi service if the associated WiFi device changes
    kWiFiDisconnectTypeNewWiFi = 9,
    // IP configuration failed
    kWiFiDisconnectTypeIPConfigFailure = 10,
    // Connecting to a pending service timed out
    kWiFiDisconnectTypePendingTimeout = 11,
    // 4-way Handshake timed out
    kWiFiDisconnectTypeHandshakeTimeout = 12,
    // Reconnecting the service timed out
    kWiFiDisconnectTypeReconnectTimeout = 13,
    // Disconnect the pending service on roaming to a wrong AP
    kWiFiDisconnectTypeRoamingIncorrectBSSID = 14,
    // Default type for WiFi disconnects initiated by shill if it doesn't belong
    // to the other WiFiDisconnectType
    kWiFiDisconnectTypeShill = 15,
    kWiFiDisconnectTypeMax
  };

  static constexpr EnumMetric<FixedName> kMetricWiFiDisconnect = {
      // "Wifi" is used instead of "WiFi" because the name of this metric used
      // to be derived from the display name of Technology::kWiFi.
      .n = FixedName{"Network.Shill.Wifi.Disconnect"},
      .max = kWiFiDisconnectTypeMax,
  };

  static constexpr HistogramMetric<FixedName> kMetricWiFiSignalAtDisconnect = {
      // "Wifi" is used instead of "WiFi" because the name of this metric used
      // to be derived from the display name of Technology::kWiFi.
      .n = FixedName{"Network.Shill.Wifi.SignalAtDisconnect"},
      .min = 1,
      .max = 200,
      .num_buckets = 40,
  };

  static constexpr char kMetricNetworkChannelSuffix[] = "Channel";
  static constexpr int kMetricNetworkChannelMax = kWiFiChannelMax;
  static constexpr char kMetricNetworkPhyModeSuffix[] = "PhyMode";
  static constexpr int kMetricNetworkPhyModeMax = kWiFiNetworkPhyModeMax;
  static constexpr char kMetricNetworkSecuritySuffix[] = "Security";
  static constexpr char kMetricWirelessSecurityChange[] =
      "Network.Shill.Wifi.SecurityChange";
  static constexpr char kMetricNetworkSignalStrengthSuffix[] = "SignalStrength";
  static constexpr int kMetricNetworkSignalStrengthMin = 1;
  static constexpr int kMetricNetworkSignalStrengthMax = 200;
  static constexpr int kMetricNetworkSignalStrengthNumBuckets = 40;

  // Metric recorded during provisioning of Passpoint credentials with the
  // Manager AddPasspointCredentials() DBus API. Indicates it the call was
  // successful, if an error happened, or if the credentials were invalid.
  enum PasspointProvisioningResult {
    kPasspointProvisioningSuccess = 0,
    kPasspointProvisioningNoOrInvalidFqdn = 1,
    kPasspointProvisioningNoOrInvalidRealm = 2,
    kPasspointProvisioningInvalidEapMethod = 3,
    kPasspointProvisioningInvalidEapProperties = 4,
    kPasspointProvisioningInvalidOrganizationIdentifier = 5,
    kPasspointProvisioningInvalidExpirationTime = 6,
    kPasspointProvisioningShillProfileError = 7,
    kPasspointProvisioningCredentialsAlreadyExist = 8,

    kPasspointProvisioningMax,
  };
  static constexpr EnumMetric<FixedName> kMetricPasspointProvisioningResult = {
      .n = FixedName{"Network.Shill.WiFi.Passpoint.ProvisioningResult"},
      .max = kPasspointProvisioningMax,
  };

  // Metric recorded when removing Passpoint credentaisl with the
  // Manager RemovePasspointCredentials() DBus API.
  enum PasspointRemovalResult {
    kPasspointRemovalSuccess = 0,
    kPasspointRemovalNotFound = 1,
    kPasspointRemovalNoActiveUserProfile = 2,
    kPasspointRemovalFailure = 3,

    kPasspointRemovalMax,
  };
  static constexpr EnumMetric<FixedName> kMetricPasspointRemovalResult = {
      .n = FixedName{"Network.Shill.WiFi.Passpoint.RemovalResult"},
      .max = kPasspointRemovalMax,
  };

  // Enum specifying how CAPPORT is advertised with RFC8910.
  enum CapportSupported {
    // The upstream network doesn't support CAPPORT protocol.
    kCapportNotSupported = 0,
    // The upstream network supports CAPPORT protocol by DHCPv4.
    kCapportSupportedByDHCPv4 = 1,
    // The upstream network supports CAPPORT protocol by IPv6 RA.
    kCapportSupportedByRA = 2,
    // The upstream network supports CAPPORT protocol by DHCP v4 and IPv6 RA.
    kCapportSupportedByDHCPv4AndRA = 3,

    kCapportSupportedMax,
  };

  // Metric counting whether the upstream network supports CAPPORT protocol.
  // This metric is only recorded at most once by network connection when a
  // portal is found with an HTTP redirect.
  static constexpr EnumMetric<NameByTechnology> kMetricCapportSupported = {
      .n = NameByTechnology{"PortalDetector.CAPPORTSupported",
                            TechnologyLocation::kAfterName},
      .max = kCapportSupportedMax,
  };

  // Metric counting whether the upstream network supports CAPPORT protocol.
  // This metric is only recorded once for every network connection where
  // CAPPORT was advertised, regardless of whether a HTTP redirect was found or
  // not with legacy HTTP probes.
  static constexpr EnumMetric<NameByTechnology> kMetricCapportAdvertised = {
      .n = NameByTechnology{"PortalDetector.CAPPORTAdvertised",
                            TechnologyLocation::kAfterName},
      .max = kCapportSupportedMax,
  };

  // Enum specifying the result when querying the CAPPORT API.
  enum CapportQueryResult {
    // Successfully got the response from CAPPORT API.
    kCapportQuerySuccess = 0,
    // Failed to get the response from CAPPORT API.
    kCapportRequestError = 1,
    // The response from CAPPORT API is not successful.
    kCapportResponseError = 2,
    // Failed to parse the JSON string from CAPPORT API.
    kCapportInvalidJSON = 3,

    kCapportQueryResultMax,
  };
  static constexpr EnumMetric<FixedName> kMetricCapportQueryResult = {
      .n = FixedName{"Network.Shill.CAPPORT.QueryResult"},
      .max = kCapportQueryResultMax,
  };

  // Boolean metric counting whether the CAPPORT server contains the venue info
  // URL. This metric is only recorded once for every CAPPORT session when we
  // can determine the CAPPORT server contains it or not.
  //
  // We assume:
  // 1. Before receiving the first CAPPORT status with is_captive=false, we
  //    should receive at least one status with is_captive=true.
  // 2. If the CAPPORT server contains the venue info URL, then it must send the
  //    URL in the first status with either is_captive=true or is_captive=false.
  //
  // Based on that, this metric is sent with true if a CAPPORT status with a
  // venue info URL has been received. If a CAPPORT status with is_captive=false
  // has been received and all the received CAPPORT status don't contain a venue
  // info URL, then this metric is sent with false. Otherwise, this metric will
  // not be sent.
  static constexpr char kMetricCapportContainsVenueInfoUrl[] =
      "Network.Shill.CAPPORT.ContainsVenueInfoURL";

  // Boolean metric counting whether the CAPPORT status contains the
  // seconds-remaining field. This metric is only recorded once for every
  // CAPPORT session when we can determine the CAPPORT server contains it or
  // not.
  //
  // The seconds-remaining field only exists when is_captive=false. When
  // is_captive is always true, we cannot determine whether the CAPPORT status
  // contains the field or not. In this case, this metric will not be sent.
  static constexpr char kMetricCapportContainsSecondsRemaining[] =
      "Network.Shill.CAPPORT.ContainsSecondsRemaining";

  // Boolean metric counting whether the CAPPORT status contains the
  // bytes-remaining field. This metric is only recorded once for every
  // CAPPORT session when we can determine the CAPPORT server contains it or
  // not.
  //
  // The bytes-remaining field only exists when is_captive=false. When
  // is_captive is always true, we cannot determine whether the CAPPORT status
  // contains the field or not. In this case, this metric will not be sent.
  static constexpr char kMetricCapportContainsBytesRemaining[] =
      "Network.Shill.CAPPORT.ContainsBytesRemaining";

  // Metric recording as an histogram the maximum of seconds-remaining from
  // CAPPORT status.
  static constexpr HistogramMetric<FixedName>
      kMetricCapportMaxSecondsRemaining = {
          .n = FixedName{"Network.Shill.CAPPORT.MaxSecondsRemaining"},
          .min = 1,
          .max = 60 * 60,  // 1 hour
          .num_buckets = 40,
  };

  // Metric counting whether the upstream network presents a portal or a Terms
  // and Conditions URL.
  enum TermsAndConditionsAggregateResult {
    kTermsAndConditionsAggregateResultUnknown = 0,
    // No portal detected, no terms and conditions URL.
    kTermsAndConditionsAggregateResultNoPortalNoURL = 1,
    // No portal detected, terms and conditions URL provided.
    kTermsAndConditionsAggregateResultNoPortalWithURL = 2,
    // Portal detected, no terms and conditions URL.
    kTermsAndConditionsAggregateResultPortalNoURL = 3,
    // Portal detected and terms and conditions URL provided.
    kTermsAndConditionsAggregateResultPortalWithURL = 4,

    kTermsAndConditionsAggregateResultMax,
  };
  static constexpr EnumMetric<FixedName>
      kMetricTermsAndConditionsAggregateResult = {
          .n = FixedName{"Network.Shill.PortalDetector."
                         "TermsAndConditionsAggregateResult"},
          .max = kTermsAndConditionsAggregateResultMax,
  };

  // Metric indicating the provisioning origin of Passpoint credentials.
  // This metric is recorded once for any successful Passpoint provisioning
  // event.
  enum PasspointOrigin {
    // Unknown or unspecified origin.
    kPasspointOriginOther = 0,
    // Credentials came from an Android App.
    kPasspointOriginAndroid = 1,
    // Credentials came from a Policy.
    kPasspointOriginPolicy = 2,
    // Credentials came from a cellular carrier profile.
    kPasspointOriginCarrier = 3,

    kPasspointOriginMax,
  };
  static constexpr EnumMetric<FixedName> kMetricPasspointOrigin = {
      .n = FixedName{"Network.Shill.WiFi.Passpoint.Origin"},
      .max = kPasspointOriginMax,
  };

  // Metric indicating the EAP method used for a Passpoint credential
  // object. This metric is recorded once for any successful Passpoint
  // provisioning event.
  // TODO(b/207730857) Update this enum once EAP-SIM support for Passpoint
  // is available.
  enum PasspointSecurity {
    kPasspointSecurityUnknown = 0,
    kPasspointSecurityTLS = 1,
    kPasspointSecurityTTLSUnknown = 2,
    kPasspointSecurityTTLSPAP = 3,
    kPasspointSecurityTTLSMSCHAP = 4,
    kPasspointSecurityTTLSMSCHAPV2 = 5,

    kPasspointSecurityMax,
  };
  static constexpr EnumMetric<FixedName> kMetricPasspointSecurity = {
      .n = FixedName{"Network.Shill.WiFi.Passpoint.Security"},
      .max = kPasspointSecurityMax,
  };

  // Metric indicating if a Passpoint profile indicates all WiFi
  // connections established with its credentials should be considered metered
  // or not. This metric is recorded once for any successful Passpoint
  // provisioning event.
  enum PasspointMeteredness {
    // Services matching this Passpoint credentials should be considered
    // non-metered.
    kPasspointNotMetered = 0,
    // Services matching this Passpoint credentials should be considered
    // metered.
    kPasspointMetered = 1,

    kPasspointMeteredMax,
  };
  static constexpr EnumMetric<FixedName> kMetricPasspointMeteredness = {
      .n = FixedName{"Network.Shill.WiFi.Passpoint.Meteredness"},
      .max = kPasspointMeteredMax,
  };

  // Metric indicating the outcome of a Passpoint credentials match event.
  // Passpoint match events are generated by the supplicant during an
  // Interworking Select operation and processed by shill for updating the
  // associated WiFi Services as connectable using the Passpoint credentials.
  enum PasspointMatch {
    kPasspointNoMatch = 0,
    // The Service associated with the matching AP was not found.
    kPasspointMatchServiceNotFound = 1,
    // The AP had already matched with higher priority Passpoint credentials.
    kPasspointMatchPriorPasspointMatch = 2,
    // The service associated with the AP that matched has non-Passpoint
    // credentials of highier priority (PSK, non-Passpoint EAP).
    kPasspointMatchPriorCredentials = 3,
    // The AP had a prior Passpoint credentials match with a priority
    // lower than "Home". The associated Service will use the new Passpoint
    // credentials for future connections.
    kPasspointMatchUpgradeToHomeMatch = 4,
    // The AP had a prior Passpoint credentials match with priority
    // lower than "Roaming". The associated Service will use the new Passpoint
    // credentials for future connections.
    kPasspointMatchUpgradeToRoamingMatch = 5,
    // The AP matched for the first time with Home priority.
    kPasspointMatchNewHomeMatch = 6,
    // The AP matched for the first time with Roaming priority.
    kPasspointMatchNewRoamingMatch = 7,
    // The AP matched for the first time with unknown priority.
    kPasspointMatchNewUnknownMatch = 8,

    kPasspointMatchMax,
  };
  static constexpr EnumMetric<FixedName> kMetricPasspointMatch = {
      .n = FixedName{"Network.Shill.WiFi.Passpoint.Match"},
      .max = kPasspointMatchMax,
  };

  // Sparse histogram metric recording the number of "Home" service provider
  // FQDNs specified in a Passpoint profile. This metric is recorded once for
  // any successful Passpoint provisioning event.
  static constexpr char kMetricPasspointDomains[] =
      "Network.Shill.WiFi.Passpoint.Domains";

  // Sparse histogram metric recording the number of "Home" Organization
  // Identifiers specified in a Passpoint profile. This metric is recorded once
  // for any successful Passpoint provisioning event.
  static constexpr char kMetricPasspointHomeOis[] =
      "Network.Shill.WiFi.Passpoint.HomeOis";

  // Sparse histogram metric recording the number of required "Home"
  // Organization Identifiers specified in a Passpoint profile. This metric is
  // recorded once for any successful Passpoint provisioning event.
  static constexpr char kMetricPasspointRequiredHomeOis[] =
      "Network.Shill.WiFi.Passpoint.RequiredHomeOis";

  // Sparse histogram metric recording the number of "Roaming" Organization
  // Identifiers specified in a Passpoint profile. This metric is recorded once
  // for any successful Passpoint provisioning event.
  static constexpr char kMetricPasspointRoamingOis[] =
      "Network.Shill.WiFi.Passpoint.RoamingOis";

  // Sparse histogram metric recording the number of Passpoint credentials
  // matches found in a single Interworking select operation conducted by the
  // supplicant.
  static constexpr char kMetricPasspointInterworkingMatches[] =
      "Network.Shill.WiFi.Passpoint.InterworkingMatches";

  // Metric recording as an histogram the duration of an Interworking select
  // operation in milliseconds.
  static constexpr HistogramMetric<FixedName>
      kMetricPasspointInterworkingDurationMillis = {
          .n =
              FixedName{
                  "Network.Shill.WiFi.Passpoint.InterworkingDurationMillis"},
          .min = 1,
          .max = 20000,  // 20 seconds
          .num_buckets = kTimerHistogramNumBuckets,
  };

  // Sparse histogram metric recording the number of Passpoint credentials
  // saved for a user profile. This metric is recorded once every time the
  // WifiProvider loads a user profile.
  static constexpr char kMetricPasspointSavedCredentials[] =
      "Network.Shill.WiFi.Passpoint.SavedCredentials";

  // Histogram metric recording the number of connected Passpoint networks
  // requiring the acceptance of Terms and Conditions.
  enum PasspointTermsAndConditions {
    // An association to a Passpoint network was successful.
    kPasspointTermsAndConditionsAssociated = 0,
    // An association to a Passpoint network led to a terms and conditions URL.
    kPasspointTermsAndConditionsURL = 1,
    kPasspointTermsAndConditionsMax,
  };
  static constexpr EnumMetric<FixedName> kMetricPasspointTermsAndConditions = {
      .n = FixedName{"Network.Shill.WiFi.Passpoint.TermsAndConditions"},
      .max = kPasspointTermsAndConditionsMax,
  };

  static constexpr int kMetricRememberedWiFiNetworkCountMax = 1024;
  static constexpr int kMetricRememberedWiFiNetworkCountMin = 1;
  static constexpr int kMetricRememberedWiFiNetworkCountNumBuckets = 32;
  static constexpr HistogramMetric<PrefixName>
      kMetricRememberedSystemWiFiNetworkCountBySecurityModeFormat = {
          .n = PrefixName{"Network.Shill.WiFi.RememberedSystemNetworkCount."},
          .min = kMetricRememberedWiFiNetworkCountMin,
          .max = kMetricRememberedWiFiNetworkCountMax,
          .num_buckets = kMetricRememberedWiFiNetworkCountNumBuckets,
  };
  static constexpr HistogramMetric<PrefixName>
      kMetricRememberedUserWiFiNetworkCountBySecurityModeFormat = {
          .n = PrefixName{"Network.Shill.WiFi.RememberedUserNetworkCount."},
          .min = kMetricRememberedWiFiNetworkCountMin,
          .max = kMetricRememberedWiFiNetworkCountMax,
          .num_buckets = kMetricRememberedWiFiNetworkCountNumBuckets,
  };
  static constexpr HistogramMetric<FixedName>
      kMetricRememberedWiFiNetworkCount = {
          .n = FixedName{"Network.Shill.WiFi.RememberedNetworkCount"},
          .min = kMetricRememberedWiFiNetworkCountMin,
          .max = kMetricRememberedWiFiNetworkCountMax,
          .num_buckets = kMetricRememberedWiFiNetworkCountNumBuckets,
  };
  static constexpr HistogramMetric<FixedName> kMetricPasspointNetworkCount = {
      .n = FixedName{"Network.Shill.WiFi.PasspointNetworkCount"},
      .min = kMetricRememberedWiFiNetworkCountMin,
      .max = kMetricRememberedWiFiNetworkCountMax,
      .num_buckets = kMetricRememberedWiFiNetworkCountNumBuckets,
  };
  static constexpr HistogramMetric<FixedName> kMetricHiddenSSIDNetworkCount = {
      .n = FixedName{"Network.Shill.WiFi.HiddenSSIDNetworkCount"},
      .min = kMetricRememberedWiFiNetworkCountMin,
      .max = kMetricRememberedWiFiNetworkCountMax,
      .num_buckets = kMetricRememberedWiFiNetworkCountNumBuckets,
  };

  static constexpr char kMetricHiddenSSIDEverConnected[] =
      "Network.Shill.WiFi.HiddenSSIDEverConnected";
  static constexpr char kMetricWiFiCQMNotification[] =
      "Network.Shill.WiFi.CQMNotification";

  static constexpr char kMetricTimeToConnectMillisecondsSuffix[] =
      "TimeToConnect";
  static constexpr int kMetricTimeToConnectMillisecondsMax =
      60 * 1000;  // 60 seconds
  static constexpr int kMetricTimeToConnectMillisecondsMin = 1;
  static constexpr int kMetricTimeToConnectMillisecondsNumBuckets = 60;
  static constexpr char kMetricTimeToScanAndConnectMillisecondsSuffix[] =
      "TimeToScanAndConnect";
  static constexpr char kMetricTimeToDisableMillisecondsSuffix[] =
      "TimeToDisable";
  static constexpr int kMetricTimeToDisableMillisecondsMax =
      60 * 1000;  // 60 seconds
  static constexpr int kMetricTimeToDisableMillisecondsMin = 1;
  static constexpr int kMetricTimeToDisableMillisecondsNumBuckets = 60;
  static constexpr char kMetricTimeToEnableMillisecondsSuffix[] =
      "TimeToEnable";
  static constexpr int kMetricTimeToEnableMillisecondsMax =
      60 * 1000;  // 60 seconds
  static constexpr int kMetricTimeToEnableMillisecondsMin = 1;
  static constexpr int kMetricTimeToEnableMillisecondsNumBuckets = 60;
  static constexpr char kMetricTimeToInitializeMillisecondsSuffix[] =
      "TimeToInitialize";
  static constexpr int kMetricTimeToInitializeMillisecondsMax =
      30 * 1000;  // 30 seconds
  static constexpr int kMetricTimeToInitializeMillisecondsMin = 1;
  static constexpr int kMetricTimeToInitializeMillisecondsNumBuckets = 30;
  static constexpr char kMetricTimeResumeToReadyMillisecondsSuffix[] =
      "TimeResumeToReady";
  static constexpr char kMetricsWiFiTimeResumeToReadyLBMilliseconds[] =
      "Network.Shill.WiFi.TimeResumeToReadyLB";
  static constexpr char kMetricsWiFiTimeResumeToReadyHBMilliseconds[] =
      "Network.Shill.WiFi.TimeResumeToReadyHB";
  static constexpr char kMetricsWiFiTimeResumeToReadyUHBMilliseconds[] =
      "Network.Shill.WiFi.TimeResumeToReadyUHB";
  static constexpr char kMetricTimeToConfigMillisecondsSuffix[] =
      "TimeToConfig";
  static constexpr char kMetricTimeToJoinMillisecondsSuffix[] = "TimeToJoin";
  static constexpr char kMetricTimeToOnlineMillisecondsSuffix[] =
      "TimeToOnline";
  static constexpr char kMetricTimeToPortalMillisecondsSuffix[] =
      "TimeToPortal";
  static constexpr char kMetricTimeToRedirectFoundMillisecondsSuffix[] =
      "TimeToRedirectFound";
  static constexpr char kMetricTimeToScanMillisecondsSuffix[] = "TimeToScan";
  static constexpr int kMetricTimeToScanMillisecondsMax =
      180 * 1000;  // 3 minutes
  static constexpr int kMetricTimeToScanMillisecondsMin = 1;
  static constexpr int kMetricTimeToScanMillisecondsNumBuckets = 90;
  static constexpr int kTimerHistogramMillisecondsMax = 45 * 1000;
  static constexpr int kTimerHistogramMillisecondsMaxLarge = 90 * 1000;
  static constexpr int kTimerHistogramMillisecondsMin = 1;

  // Called with the number of detection attempts when the PortalDetector
  // completes and the result is 'online'.
  static constexpr HistogramMetric<NameByTechnology>
      kPortalDetectorAttemptsToOnline = {
          .n = {"PortalDetector.AttemptsToOnline",
                TechnologyLocation::kAfterName},
          .min = 1,
          .max = 20,
          .num_buckets = 20,
  };

  // Called with the number of detection attempts when the PortalDetector
  // completes or is stopped and the result is a non connected state.
  static constexpr HistogramMetric<NameByTechnology>
      kPortalDetectorAttemptsToDisconnect = {
          .n = {"PortalDetector.AttemptsToDisconnect",
                TechnologyLocation::kAfterName},
          .min = 1,
          .max = 20,
          .num_buckets = 20,
  };

  // Called with the number of detection attempts when a Service first
  // transitions to redirect-found.
  static constexpr HistogramMetric<NameByTechnology>
      kPortalDetectorAttemptsToRedirectFound = {
          .n = {"PortalDetector.AttemptsToRedirectFound",
                TechnologyLocation::kAfterName},
          .min = 1,
          .max = 10,
          .num_buckets = 10,
  };

  // Duration of a complete portal detection attempt when the result is
  // PortalDetector.ValidationState.kInternetConnectivity, in milliseconds.
  static constexpr HistogramMetric<NameByTechnology>
      kPortalDetectorInternetValidationDuration = {
          .n = {"PortalDetector.InternetValidationDuration",
                TechnologyLocation::kAfterName},
          .min = 10,
          .max = 10000,  // 10 seconds
          .num_buckets = 40,
  };

  // Duration of a complete portal detection attempt when the result is
  // PortalDetector.ValidationState.kPortalRedirection, in milliseconds.
  static constexpr HistogramMetric<NameByTechnology>
      kPortalDetectorPortalDiscoveryDuration = {
          .n = {"PortalDetector.PortalDiscoveryDuration",
                TechnologyLocation::kAfterName},
          .min = 10,
          .max = 10000,  // 10 seconds
          .num_buckets = 40,
  };

  // Duration of a PortalDetector HTTP probe in milliseconds, for all probe
  // results.
  static constexpr HistogramMetric<NameByTechnology>
      kPortalDetectorHTTPProbeDuration = {
          .n = {"PortalDetector.HTTPProbeDuration",
                TechnologyLocation::kAfterName},
          .min = 10,
          .max = 10000,  // 10 seconds
          .num_buckets = 40,
  };

  // Duration of a PortalDetector HTTPS probe in milliseconds, for all probe
  // results.
  static constexpr HistogramMetric<NameByTechnology>
      kPortalDetectorHTTPSProbeDuration = {
          .n = {"PortalDetector.HTTPSProbeDuration",
                TechnologyLocation::kAfterName},
          .min = 10,
          .max = 10000,  // 10 seconds
          .num_buckets = 40,
  };

  // Duration in milliseconds from the initial network connection to the first
  // discovery of a captive portal redirect.
  static constexpr HistogramMetric<NameByTechnology>
      kPortalDetectorTimeToRedirect = {
          .n = {"PortalDetector.TimeToRedirect",
                TechnologyLocation::kAfterName},
          .min = 10,
          .max = base::Minutes(2).InMilliseconds(),
          .num_buckets = kTimerHistogramNumBuckets,
  };

  // Duration in milliseconds from the initial network connection to the first
  // verification of Internet connectivity with network validation.
  static constexpr HistogramMetric<NameByTechnology>
      kPortalDetectorTimeToInternet = {
          .n = {"PortalDetector.TimeToInternet",
                TechnologyLocation::kAfterName},
          .min = 10,
          .max = base::Minutes(1).InMilliseconds(),
          .num_buckets = kTimerHistogramNumBuckets,
  };

  // Duration in milliseconds from the first captive portal redirect found to
  // the first verification of Internet connectivity with network validation.
  static constexpr HistogramMetric<NameByTechnology>
      kPortalDetectorTimeToInternetAfterRedirect = {
          .n = {"PortalDetector.TimeToInternetAfterRedirect",
                TechnologyLocation::kAfterName},
          .min = 10,
          .max = base::Minutes(5).InMilliseconds(),
          .num_buckets = kTimerHistogramNumBuckets,
  };

  // Duration in milliseconds from the initial network connection to the first
  // CAPPORT query result advertising the user portal URL.
  static constexpr HistogramMetric<NameByTechnology>
      kPortalDetectorTimeToCAPPORTUserPortalURL = {
          .n = {"PortalDetector.TimeToCAPPORTUserPortalURL",
                TechnologyLocation::kAfterName},
          .min = 10,
          .max = base::Minutes(2).InMilliseconds(),
          .num_buckets = kTimerHistogramNumBuckets,
  };

  // Duration in milliseconds from the initial network connection to the first
  // CAPPORT query result indicating "is_captive" transitioned from true to
  // false (Access to the external network was granted).
  static constexpr HistogramMetric<NameByTechnology>
      kPortalDetectorTimeToCAPPORTNotCaptive = {
          .n = {"PortalDetector.TimeToCAPPORTNotCaptive",
                TechnologyLocation::kAfterName},
          .min = 10,
          .max = base::Minutes(5).InMilliseconds(),
          .num_buckets = kTimerHistogramNumBuckets,
  };

  // Result of CAPPORT status queries aggregated until the CAPPORT network
  // indicates that access to the external network has been granted.
  enum AggregateCAPPORTResult {
    kAggregateCAPPORTResultUnknown = 0,
    kAggregateCAPPORTResultCaptive = 1,
    kAggregateCAPPORTResultOpenWithoutInternet = 2,
    kAggregateCAPPORTResultOpenWithInternet = 3,

    kAggregateCAPPORTResultMax,
  };
  static constexpr EnumMetric<NameByTechnology>
      kPortalDetectorAggregateCAPPORTResult = {
          .n = {"PortalDetector.AggregateCAPPORTResult",
                TechnologyLocation::kAfterName},
          .max = kAggregateCAPPORTResultMax,
  };

  static constexpr char kMetricWiFiScanTimeInEbusyMilliseconds[] =
      "Network.Shill.WiFi.ScanTimeInEbusy";

  static constexpr char kMetricPowerManagerKey[] = "metrics";

  // Signal strength when link becomes unreliable (multiple link monitor
  // failures in short period of time). This name of this metric uses "Wifi"
  // instead of "WiFi" because its name used to be constructed from a
  // Technology value.
  static constexpr HistogramMetric<FixedName>
      kMetricUnreliableLinkSignalStrength = {
          .n = FixedName{"Network.Shill.Wifi.UnreliableLinkSignalStrength"},
          .min = 1,
          .max = 100,
          .num_buckets = 40,
  };

  // AP 802.11r/k/v support statistics.
  static constexpr char kMetricAp80211kSupport[] =
      "Network.Shill.WiFi.Ap80211kSupport";
  static constexpr char kMetricAp80211vDMSSupport[] =
      "Network.Shill.WiFi.Ap80211vDMSSupport";
  static constexpr char kMetricAp80211vBSSMaxIdlePeriodSupport[] =
      "Network.Shill.WiFi.Ap80211vBSSMaxIdlePeriodSupport";
  static constexpr char kMetricAp80211vBSSTransitionSupport[] =
      "Network.Shill.WiFi.Ap80211vBSSTransitionSupport";
  static constexpr char kMetricCiscoAdaptiveFTSupport[] =
      "Network.Shill.WiFi.CiscoAdaptiveFTSupport";
  static constexpr EnumMetric<FixedName> kMetricLinkApDisconnectReason = {
      .n = FixedName{"Network.Shill.WiFi.ApDisconnectReason"},
      .max = IEEE_80211::kReasonCodeMax,
  };
  static constexpr EnumMetric<FixedName> kMetricLinkClientDisconnectReason = {
      .n = FixedName{"Network.Shill.WiFi.ClientDisconnectReason"},
      .max = IEEE_80211::kReasonCodeMax,
  };

  // AP 802.11u support statistics.
  static constexpr char kMetricAp80211uANQPSupport[] =
      "Network.Shill.WiFi.Ap80211uANQPSupport";

  // Result of ANQP queries.
  enum ANQPQueryResult {
    kANQPQueryResultUnknown = 0,
    kANQPQueryResultSuccess = 1,
    kANQPQueryResultFailure = 2,
    kANQPQueryResultInvalidFrame = 3,

    kANQPQueryResultMax,
  };
  static constexpr EnumMetric<FixedName> kMetricWiFiANQPQueryResult = {
      .n = FixedName{"Network.Shill.WiFi.ANQP.QueryResult"},
      .max = kANQPQueryResultMax,
  };

  // ANQP capabilities support.
  static constexpr char kMetricANQPVenueNameSupport[] =
      "Network.Shill.WiFi.ANQP.VenueNameSupport";
  static constexpr char kMetricANQPVenueURLSupport[] =
      "Network.Shill.WiFi.ANQP.VenueURLSupport";
  static constexpr char kMetricANQPNetworkAuthTypeSupport[] =
      "Network.Shill.WiFi.ANQP.NetworkAuthTypeSupport";
  static constexpr char kMetricANQPAddressTypeAvailabilitySupport[] =
      "Network.Shill.WiFi.ANQP.AddressTypeAvailabilitySupport";

  // 802.11 Status Codes for auth/assoc failures
  static constexpr char kMetricWiFiAssocFailureType[] =
      "Network.Shill.WiFi.AssocFailureType";
  static constexpr char kMetricWiFiAuthFailureType[] =
      "Network.Shill.WiFi.AuthFailureType";

  // Roam time for FT and non-FT key management suites.
  static constexpr char kMetricWifiRoamTimePrefix[] =
      "Network.Shill.WiFi.RoamTime";
  static constexpr int kMetricWifiRoamTimeMillisecondsMax = 1000;
  static constexpr int kMetricWifiRoamTimeMillisecondsMin = 1;
  static constexpr int kMetricWifiRoamTimeNumBuckets = 20;

  // Roam completions for FT and non-FT key management suites.
  static constexpr char kMetricWifiRoamCompletePrefix[] =
      "Network.Shill.WiFi.RoamComplete";

  // Session Lengths for FT and non-FT key management suites.
  static constexpr char kMetricWifiSessionLengthPrefix[] =
      "Network.Shill.WiFi.SessionLength";
  static constexpr int kMetricWifiSessionLengthMillisecondsMax = 10000;
  static constexpr int kMetricWifiSessionLengthMillisecondsMin = 1;
  static constexpr int kMetricWifiSessionLengthNumBuckets = 20;

  // Suffixes for roam histograms.
  static constexpr char kMetricWifiPSKSuffix[] = "PSK";
  static constexpr char kMetricWifiFTPSKSuffix[] = "FTPSK";
  static constexpr char kMetricWifiEAPSuffix[] = "EAP";
  static constexpr char kMetricWifiFTEAPSuffix[] = "FTEAP";

  // Cellular specific statistics.
  static constexpr HistogramMetric<FixedName>
      kMetricCellularSignalStrengthBeforeDrop = {
          .n = FixedName{"Network.Shill.Cellular.SignalStrengthBeforeDrop"},
          .min = 1,
          .max = 100,
          .num_buckets = 10,
  };

  // Number of peers used in a WireGuard connection.
  static constexpr HistogramMetric<FixedName> kMetricVpnWireGuardPeersNum = {
      .n = FixedName{"Network.Shill.Vpn.WireGuardPeersNum"},
      .min = 1,
      .max = 10,
      .num_buckets = 11,
  };

  // The length in seconds of a lease that has expired while the DHCP client was
  // attempting to renew the lease. CL:557297 changed the number of buckets for
  // the 'ExpiredLeaseLengthSeconds' metric. That would lead to confusing
  // display of samples collected before and after the change. To avoid that,
  // the 'ExpiredLeaseLengthSeconds' metric is renamed to
  // 'ExpiredLeaseLengthSeconds2'.
  static constexpr HistogramMetric<NameByTechnology>
      kMetricExpiredLeaseLengthSeconds = {
          .n = NameByTechnology{"ExpiredLeaseLengthSeconds2"},
          .min = 1,
          .max = 7 * 24 * 60 * 60,  // 7 days
          .num_buckets = 100,
  };

  // Number of wifi services available when auto-connect is initiated.
  static constexpr HistogramMetric<FixedName>
      kMetricWifiAutoConnectableServices = {
          .n = FixedName{"Network.Shill.WiFi.AutoConnectableServices"},
          .min = 1,
          .max = 50,
          .num_buckets = 10,
  };

  // Number of BSSes available for a wifi service when we attempt to connect
  // to that service.
  static constexpr HistogramMetric<FixedName> kMetricWifiAvailableBSSes = {
      .n = FixedName{"Network.Shill.WiFi.AvailableBSSesAtConnect"},
      .min = 1,
      .max = 50,
      .num_buckets = 10,
  };

  // Wifi TX bitrate in Mbps.
  static constexpr HistogramMetric<FixedName> kMetricWifiTxBitrate = {
      .n = FixedName{"Network.Shill.WiFi.TransmitBitrateMbps"},
      .min = 1,
      .max = 7000,
      .num_buckets = 100,
  };

  // Number of attempts made to connect to supplicant before success (max ==
  // failure).
  static constexpr HistogramMetric<FixedName> kMetricWifiSupplicantAttempts = {
      .n = FixedName{"Network.Shill.WiFi.SupplicantAttempts"},
      .min = 1,
      .max = 10,
      .num_buckets = 11,
  };

  // Wireless regulatory domain metric.
  static constexpr char kMetricRegulatoryDomain[] =
      "Network.Shill.WiFi.RegulatoryDomain";

  // MBO support metric.
  static constexpr char kMetricMBOSupport[] = "Network.Shill.WiFi.MBOSupport";

  // 6GHz Band Support metric.
  static constexpr char kMetricBand6GHzSupport[] =
      "Network.Shill.WiFi.Band6GHzSupport";

  // Seconds between latest WiFi rekey attempt and service failure, in seconds.
  static constexpr HistogramMetric<FixedName>
      kMetricTimeFromRekeyToFailureSeconds = {
          .n = FixedName{"Network.Shill.WiFi.TimeFromRekeyToFailureSeconds"},
          .min = 0,
          .max = 180,
          .num_buckets = 30,
  };

  // Version number of the format of WiFi structured metrics. Changed when the
  // formatting of the metrics changes, so that the server-side code knows
  // which fields to expect.
  static constexpr int kWiFiStructuredMetricsVersion = 1;

  // When emitting WiFi structured metrics, if we encounter errors and the
  // numeric values of some of the fields can not be populated, use this as
  // value for the field.
  static constexpr int kWiFiStructuredMetricsErrorValue = -1;

  // 9999 is wpa_supplicant's value for "invalid RSSI", let's use the same.
  static constexpr int kWiFiStructuredMetricsErrorValueRSSI = 9999;

  // Some WiFi adapters like the ones integrated in some Qualcomm SoCs do not
  // have a PCI vendor/product/subsystem ID. When we detect such an adapter on
  // the system we use "0x0000" as PCI Vendor ID since that ID is not used by
  // the PCI-SIG. Otherwise if we assigned an actual vendor ID like Qualcomm's
  // ID we may have conflicting values with PCI devices from those vendors.
  static constexpr int kWiFiIntegratedAdapterVendorId = 0x0000;

  struct WiFiAdapterInfo {
    int vendor_id;
    int product_id;
    int subsystem_id;
  };

  enum WiFiSessionTagState {
    kWiFiSessionTagStateUnknown = 0,
    kWiFiSessionTagStateUnexpected = 1,
    kWiFiSessionTagStateExpected = 2,
    kWiFiSessionTagStateMax
  };
  static constexpr char kWiFiSessionTagStateMetricPrefix[] =
      "Network.Shill.WiFi.SessionTagState";
  static constexpr char kWiFiSessionTagConnectionAttemptSuffix[] =
      "ConnectionAttempt";
  static constexpr char kWiFiSessionTagConnectionAttemptResultSuffix[] =
      "ConnectionAttemptResult";
  static constexpr char kWiFiSessionTagDisconnectionSuffix[] = "Disconnection";
  static constexpr char kWiFiSessionTagLinkQualityTriggerSuffix[] =
      "LinkQualityTrigger";
  static constexpr char kWiFiSessionTagLinkQualityReportSuffix[] =
      "LinkQualityReport";

  enum WiFiBadPassphraseServiceType {
    kNonUserInitiatedNeverConnected = 0,
    kNonUserInitiatedConnectedBefore = 1,
    kUserInitiatedNeverConnected = 2,
    kUserInitiatedConnectedBefore = 3,
    kBadPassphraseServiceTypeMax
  };
  static constexpr EnumMetric<FixedName> kMetricWiFiBadPassphraseServiceType = {
      .n = FixedName{"Network.Shill.WiFi.BadPassphraseServiceType"},
      .max = kBadPassphraseServiceTypeMax,
  };

  Metrics();
  Metrics(const Metrics&) = delete;
  Metrics& operator=(const Metrics&) = delete;

  virtual ~Metrics();

  // Converts the WiFi frequency into the associated UMA channel enumerator.
  static WiFiChannel WiFiFrequencyToChannel(uint16_t frequency);

  // Converts WiFi Channel to the associated frequency range.
  static WiFiFrequencyRange WiFiChannelToFrequencyRange(WiFiChannel channel);

  // Converts a flimflam EAP outer protocol string into its UMA enumerator.
  static EapOuterProtocol EapOuterProtocolStringToEnum(
      const std::string& outer);

  // Converts a flimflam EAP inner protocol string into its UMA enumerator.
  static EapInnerProtocol EapInnerProtocolStringToEnum(
      const std::string& inner);

  // Specializes |metric_name| with the specified |technology_id| and
  // |location|.
  static std::string GetFullMetricName(
      std::string_view metric_name,
      Technology technology_id,
      TechnologyLocation location = TechnologyLocation::kBeforeName);

  // Notifies this object that an AP was discovered and of that AP's 802.11k
  // support.
  void NotifyAp80211kSupport(bool neighbor_list_supported);

  // Notifies this object that an AP was discovered and of that AP's 802.11r
  // support.
  void NotifyAp80211rSupport(bool ota_ft_supported, bool otds_ft_supported);

  // Notifies this object that an AP was discovered and of that AP's 802.11v
  // DMS support.
  void NotifyAp80211vDMSSupport(bool dms_supported);

  // Notifies this object that an AP was discovered and of that AP's 802.11v
  // BSS Max Idle Period support.
  void NotifyAp80211vBSSMaxIdlePeriodSupport(
      bool bss_max_idle_period_supported);

  // Notifies this object that an AP was discovered and of that AP's 802.11v
  // BSS Transition support.
  void NotifyAp80211vBSSTransitionSupport(bool bss_transition_supported);

  // Notifies this object that an AP was discovered with Cisco Adaptive FT
  // support.
  void NotifyCiscoAdaptiveFTSupport(bool adaptive_ft_supported);

  // Notifies this object that an AP was discovered with ANQP support.
  void NotifyANQPSupport(bool anqp_supported);

  // Notifies this object of WiFi disconnect.
  // TODO(b/234176329): Deprecate those metrics once
  // go/cros-wifi-structured-metrics-dd has fully landed.
  virtual void Notify80211Disconnect(WiFiDisconnectByWhom by_whom,
                                     IEEE_80211::WiFiReasonCode reason);

  // Notifies this object that an AP has switched channels.
  void NotifyApChannelSwitch(uint16_t frequency, uint16_t new_frequency);

  // Registers a device with this object so the device can use the timers to
  // track state transition metrics.
  void RegisterDevice(int interface_index, Technology technology);

  // Checks to see if the device has already been registered.
  bool IsDeviceRegistered(int interface_index, Technology technology);

  // Deregisters the device from this class.  All state transition timers
  // will be removed.
  virtual void DeregisterDevice(int interface_index);

  // Notifies this object that a device has been initialized.
  void NotifyDeviceInitialized(int interface_index);

  // Notifies this object that a device has started the enable process.
  void NotifyDeviceEnableStarted(int interface_index);

  // Notifies this object that a device has completed the enable process.
  void NotifyDeviceEnableFinished(int interface_index);

  // Notifies this object that a device has started the disable process.
  void NotifyDeviceDisableStarted(int interface_index);

  // Notifies this object that a device has completed the disable process.
  void NotifyDeviceDisableFinished(int interface_index);

  // Notifies this object that a device has started the scanning process.
  virtual void NotifyDeviceScanStarted(int interface_index);

  // Notifies this object that a device has completed the scanning process.
  virtual void NotifyDeviceScanFinished(int interface_index);

  // Report the status of the scan.
  mockable void ReportDeviceScanResultToUma(Metrics::WiFiScanResult result);

  // Terminates an underway scan (does nothing if a scan wasn't underway).
  virtual void ResetScanTimer(int interface_index);

  // Notifies this object that a device has started the connect process.
  virtual void NotifyDeviceConnectStarted(int interface_index);

  // Notifies this object that a device has completed the connect process.
  virtual void NotifyDeviceConnectFinished(int interface_index);

  // Resets both the connect_timer and the scan_connect_timer the timer (the
  // latter so that a future connect will not erroneously be associated with
  // the previous scan).
  virtual void ResetConnectTimer(int interface_index);

  // Notifies this object that a cellular device has been dropped by the
  // network.
  void NotifyCellularDeviceDrop(const std::string& network_technology,
                                uint16_t signal_strength);

  // Notifies this object of the entitlement check result
  virtual void NotifyCellularEntitlementCheckResult(
      Metrics::CellularEntitlementCheck result);

  // IPConfigMethod and SimType are used for cellular metrics reporting
  enum class IPConfigMethod {
    kUnknown = 0,
    kPPP = 1,
    kStatic = 2,
    kDynamic = 3
  };
  enum class SimType {
    kUnknown = 0,
    kPsim = 1,
    kEsim = 2,
  };

  struct CellularDeviceId {
    enum class BusType {
      kUnknown = 0,
      kPci = 1,
      kUsb = 2,
      kSoc = 3,
    };
    BusType bus_type;
    uint16_t vid;
    uint16_t pid;
  };

  struct DetailedCellularConnectionResult {
    // The values are used in metrics and thus should not be changed.
    enum class APNType {
      kDefault,
      kAttach,
      kDUN,
    };
    enum class ConnectionAttemptType {
      kUnknown = 0,
      kUserConnect = 1,
      kAutoConnect = 2
    };
    Error::Type error;
    std::string detailed_error;
    std::string uuid;
    std::map<std::string, std::string> apn_info;
    std::vector<APNType> connection_apn_types;
    IPConfigMethod ipv4_config_method;
    IPConfigMethod ipv6_config_method;
    std::string home_mccmnc;
    std::string serving_mccmnc;
    std::string roaming_state;
    uint32_t tech_used;
    uint32_t iccid_length;
    SimType sim_type;
    std::string gid1;
    uint32_t modem_state;
    int interface_index;
    uint32_t use_apn_revamp_ui;
    ConnectionAttemptType connection_attempt_type;
    uint32_t subscription_error_seen;
    uint64_t last_connected;
    uint64_t last_online;
    CellularDeviceId device_id;
  };

  struct CellularNetworkValidationResult {
    std::map<std::string, std::string> apn_info;
    std::string uuid;
    int portal_detection_result;
    int portal_detection_count;
    IPConfigMethod ipv4_config_method;
    IPConfigMethod ipv6_config_method;
    std::string home_mccmnc;
    std::string serving_mccmnc;
    std::string roaming_state;
    uint32_t tech_used;
    SimType sim_type;
  };

  struct CellularPowerOptimizationInfo {
    enum class PowerState {
      kUnknown = 0,
      kOn = 1,
      kLow = 2,
      kOff = 3,
    };
    enum class CellularPowerOptimizationReason {
      kNoServiceGeneral = 0,
      kNoServiceInvalidApn = 1,
      kNoServiceNoSubscription = 2,
      kNoServiceAdminRestriction = 3,
      kNoServiceLongNotOnline = 4,
    };
    PowerState new_power_state;
    CellularPowerOptimizationReason reason;
    uint32_t since_last_online_hours;
  };

  // Notifies this object of the resulting status of a cellular connection
  virtual void NotifyCellularConnectionResult(
      Error::Type error, DetailedCellularConnectionResult::APNType apn_type);

  // Notifies this object of the resulting status of a cellular connection
  virtual void NotifyDetailedCellularConnectionResult(
      const DetailedCellularConnectionResult& result);

  // Notifies result of portal detection over a cellular connection
  virtual void NotifyCellularNetworkValidationResult(
      const CellularNetworkValidationResult& result);

  // Notifies modem power optimization performed
  virtual void NotifyCellularPowerOptimization(
      const CellularPowerOptimizationInfo& power_opt_info);

  // Sends linear histogram data to UMA.
  virtual bool SendEnumToUMA(const std::string& name, int sample, int max);

  // Sends bool to UMA.
  virtual bool SendBoolToUMA(const std::string& name, bool b);

  // Send logarithmic histogram data to UMA.
  virtual bool SendToUMA(
      const std::string& name, int sample, int min, int max, int num_buckets);

  // Sends sparse histogram data to UMA.
  virtual bool SendSparseToUMA(const std::string& name, int sample);

  // Notifies this object that of the HS20 support of an access that has
  // been connected to.
  void NotifyHS20Support(bool hs20_supported, int hs20_version_number);

  // Calculate Regulatory domain value given two letter country code.
  // Return value corresponds to Network.Shill.WiFi.RegulatoryDomain histogram
  // buckets. The full enum can be found in
  // /chromium/src/tools/metrics/histograms/enums.xml.
  static int GetRegulatoryDomainValue(std::string country_code);

  // Notifies this object of the MBO support of the access point that has been
  // connected to.
  void NotifyMBOSupport(bool mbo_support);

  // Notifies this object of the 6GHz band support of the access point that has
  // been connected to.
  void NotifyBand6GHzSupport(bool band6ghz_support);

  // Notifies this object that of the stream classification support of an access
  // point that has been connected to.
  void NotifyStreamClassificationSupport(bool scs_supported,
                                         bool mscs_supported);

  // Notifies this object that of the alternate EDCA support of an access point
  // that has been connected to.
  void NotifyAlternateEDCASupport(bool alternate_edca_supported);

  // Emits the |WiFiAdapterStateChanged| structured event that notifies that
  // the WiFi adapter has been enabled or disabled. Includes the IDs describing
  // the type of the adapter (e.g. PCI IDs).
  mockable void NotifyWiFiAdapterStateChanged(bool enabled,
                                              const WiFiAdapterInfo& info);

  enum ConnectionAttemptType {
    kAttemptTypeUnknown = 0,
    kAttemptTypeUserInitiated = 1,
    kAttemptTypeAuto = 2
  };

  enum SSIDProvisioningMode {
    kProvisionUnknown = 0,
    kProvisionManual = 1,
    kProvisionPolicy = 2,
    kProvisionSync = 3
  };

  struct WiFiConnectionAttemptInfo {
    ConnectionAttemptType type;
    WiFiNetworkPhyMode mode;
    Metrics::WirelessSecurity security;
    EapInnerProtocol eap_inner;
    EapOuterProtocol eap_outer;
    WiFiFrequencyRange band;
    WiFiChannel channel;
    int rssi;
    std::string ssid;
    std::optional<net_base::MacAddress> bssid;
    SSIDProvisioningMode provisioning_mode;
    bool ssid_hidden;
    int ap_oui;
    struct ApSupportedFeatures {
      struct Ap80211krv {
        int neighbor_list_supported = kWiFiStructuredMetricsErrorValue;
        int ota_ft_supported = kWiFiStructuredMetricsErrorValue;
        int otds_ft_supported = kWiFiStructuredMetricsErrorValue;
        int dms_supported = kWiFiStructuredMetricsErrorValue;
        int bss_max_idle_period_supported = kWiFiStructuredMetricsErrorValue;
        int bss_transition_supported = kWiFiStructuredMetricsErrorValue;
      } krv_info;
      struct ApHS20 {
        int supported = kWiFiStructuredMetricsErrorValue;
        int version = kWiFiStructuredMetricsErrorValue;
      } hs20_info;
      int mbo_supported = kWiFiStructuredMetricsErrorValue;
    } ap_features;
  };

  // Emits the |WiFiConnectionAttempt| structured event that notifies that the
  // device is attempting to connect to an AP. It describes the parameters of
  // the connection (channel/band, security mode, etc.).
  virtual void NotifyWiFiConnectionAttempt(
      const WiFiConnectionAttemptInfo& info, uint64_t session_tag);

  // Emits the |WiFiConnectionAttemptResult| structured event that describes
  // the result of the corresponding |WiFiConnectionAttempt| event.
  virtual void NotifyWiFiConnectionAttemptResult(
      Metrics::NetworkServiceError result_code, uint64_t session_tag);

  enum WiFiDisconnectionType {
    kWiFiDisconnectionTypeUnknown = 0,
    kWiFiDisconnectionTypeExpectedUserAction = 1,
    kWiFiDisconnectionTypeExpectedRoaming = 2,
    kWiFiDisconnectionTypeUnexpectedAPDisconnect = 3,
    kWiFiDisconnectionTypeUnexpectedSTADisconnect = 4
  };

  // Reuse Android framework proto WifiDisconnectReported.FailureCode in:
  // frameworks/proto_logging/stats/atoms.proto
  enum class WiFiDisconnectReasonCode {
    kReasonCodeUnknown = 0,

    // WiFi supplicant failure reason codes (IEEE Std 802.11-2020, 9.4.1.7,
    // Table 9-49 and IEEE Std 802.11ax-2021, 9.4.1.7, Table 9-49).
    kReasonCodeUnspecified = 1,
    kReasonCodePrevAuthNotValid = 2,
    kReasonCodeDeauthLeaving = 3,
    kReasonCodeDisassocDueToInactivity = 4,
    kReasonCodeDisassocAPBusy = 5,
    kReasonCodeClass2FrameFromNonAuthSTA = 6,
    kReasonCodeClass3FrameFromNonAssocSTA = 7,
    kReasonCodeDisassocSTAHasLeft = 8,
    kReasonCodeSTAReqAssocWithoutAuth = 9,
    kReasonCodePwrCapabilityNotValid = 10,
    kReasonCodeSupportedChannelNotValid = 11,
    kReasonCodeBssTransitionDisassoc = 12,
    kReasonCodeInvalidIE = 13,
    kReasonCodeMichaelMICFailure = 14,
    kReasonCodeFourwayHandshakeTimeout = 15,
    kReasonCodeGroupKeyUpdateTimeout = 16,
    kReasonCodeIEIn4wayDiffers = 17,
    kReasonCodeGroupCipherNotValid = 18,
    kReasonCodePairwiseCipherNotValid = 19,
    kReasonCodeAKMPNotValid = 20,
    kReasonCodeUnsupportedRSNIEVersion = 21,
    kReasonCodeInvalidRSNIECapab = 22,
    kReasonCodeIEEE8021XAuthFailed = 23,
    kReasonCodeCipherSuiteRejected = 24,
    kReasonCodeTDLSTeardownUnreachable = 25,
    kReasonCodeTDLSTeardownUnspecified = 26,
    kReasonCodeSSPRequestedDisassoc = 27,
    kReasonCodeNoSSPRoamingAgreement = 28,
    kReasonCodeBadCipherOrAKM = 29,
    kReasonCodeNotAuthorizedThisLocation = 30,
    kReasonCodeServiceChangePrecludesTS = 31,
    kReasonCodeUnspecifiedQoSReason = 32,
    kReasonCodeNotEnoughBandwidth = 33,
    kReasonCodeDisassocLowACK = 34,
    kReasonCodeExceededTXOP = 35,
    kReasonCodeSTALeaving = 36,
    kReasonCodeEndTSBADLS = 37,
    kReasonCodeUnknownTSBA = 38,
    kReasonCodeTimeout = 39,
    kReasonCodePeerkeyMismatch = 45,
    kReasonCodeAuthorizedAccessLimitReached = 46,
    kReasonCodeExternalServiceRequirements = 47,
    kReasonCodeInvalidFTActionFrameCount = 48,
    kReasonCodeInvalidPMKID = 49,
    kReasonCodeInvalidMDE = 50,
    kReasonCodeInvalidFTE = 51,
    kReasonCodeMeshPeeringCancelled = 52,
    kReasonCodeMeshMaxPeers = 53,
    kReasonCodeMeshConfigPolicyViolation = 54,
    kReasonCodeMeshCloseRCVD = 55,
    kReasonCodeMeshMaxRetries = 56,
    kReasonCodeMeshConfirmTimeout = 57,
    kReasonCodeMeshInvalidGTK = 58,
    kReasonCodeMeshInconsistentParams = 59,
    kReasonCodeMeshInvalidSecurityCap = 60,
    kReasonCodeMeshPathErrorNoProxyInfo = 61,
    kReasonCodeMeshPathErrorNoForwardingInfo = 62,
    kReasonCodeMeshPathErrorDestUnreachable = 63,
    kReasonCodeMACAddressAlreadyExistsInMbss = 64,
    kReasonCodeMeshChannelSwitchRegulatoryReq = 65,
    kReasonCodeMeshChannelSwitchUnspecified = 66,
    kReasonCodeTransmissionLinkEstablishmentFailed = 67,
    kReasonCodeAlternativeChannelOccupied = 68,
    kReasonCodePoorRSSIConditions = 71,

    // Android ClientModeImpl error codes defined in
    // packages/modules/Wifi/service/java/com/android/server/wifi/
    // WifiMetrics.java
    kReasonCodeIfaceDestroyed = 10000,
    kReasonCodeWiFiDisabled = 10001,
    kReasonCodeSupplicantDisconnected = 10002,
    kReasonCodeConnectingWatchdogTimer = 10003,
    kReasonCodeRoamWatchdogTimer = 10004,

    // New reasons tracking disconnections initiated by Android wifi framework
    // Framework disconnect, generic reason
    kReasonCodeDisconnectGeneral = 10005,
    // Disconnecting due to unspecified IP reachability lost.
    kReasonCodeDisconnectNUDFailureGeneric = 10006,
    // Disconnecting due to IP reachability lost from roaming
    kReasonCodeDisconnectNUDFailureRoam = 10007,
    // Disconnecting due to IP reachability lost from the CONFIRM command
    kReasonCodeDisconnectNUDFailureConfirm = 10008,
    // Disconnecting due to IP reachability lost from kernel check
    kReasonCodeDisconnectNUDFailureOrganic = 10009,
    // Connectivity no longer wants this network
    kReasonCodeDisconnectUnwantedByConnectivity = 10010,
    // Timeout creating the IP client
    kReasonCodeDisconnectCreateIPClientTimeout = 10011,
    // IP provisioning failure
    kReasonCodeDisconnectIPProvisioningFailure = 10012,
    // Disconnect by P2P
    kReasonCodeDisconnectP2PRequestedDisconnect = 10013,
    // Network is removed from the WifiConfigManager
    kReasonCodeDisconnectNetworkRemoved = 10014,
    // Network is marked as untrusted
    kReasonCodeDisconnectNetworkUntrusted = 10015,
    // Network is marked as metered
    kReasonCodeDisconnectNetworkMetered = 10016,
    // Network is temporarily disabled
    kReasonCodeDisconnectTempDisabled = 10017,
    // Network is permanently disabled
    kReasonCodeDisconnectPermDisabled = 10018,
    kReasonCodeDisconnectCarrierOffloadDisabled = 10019,
    // Disconnecting due to Passpoint terms and conditions page
    kReasonCodeDisconnectPasspointTAC = 10020,
    // Disconnecting due to issues with terms and conditions URL
    kReasonCodeDisconnectVNCRequest = 10021,
    // Connected to a network that is already removed
    kReasonCodeDisconnectUnknownNetwork = 10022,
    // User initiated a new connection
    kReasonCodeDisconnectNewConnectionUser = 10023,
    // New connection triggered by non-user
    kReasonCodeDisconnectNewConnectionOthers = 10024,
    // Wi-Fi 7 is enabled or disabled for this network
    kReasonCodeDisconnectNetworkWiFi7Toggled = 10025,
  };

  static WiFiDisconnectReasonCode GetWiFiDisconnectReasonCode(
      WiFiDisconnectType type, IEEE_80211::WiFiReasonCode code);

  // Emits the |WiFiConnectionEnd| structured event.
  virtual void NotifyWiFiDisconnection(WiFiDisconnectionType type,
                                       WiFiDisconnectReasonCode reason,
                                       uint64_t session_tag);

  enum WiFiLinkQualityTrigger {
    kWiFiLinkQualityTriggerUnknown = 0,
    kWiFiLinkQualityTriggerCQMRSSILow = 1,
    kWiFiLinkQualityTriggerCQMRSSIHigh = 2,
    kWiFiLinkQualityTriggerCQMBeaconLoss = 3,
    kWiFiLinkQualityTriggerCQMPacketLoss = 4,
    kWiFiLinkQualityTriggerBackgroundCheck = 5,
    kWiFiLinkQualityTriggerIPConfigurationStart = 6,
    kWiFiLinkQualityTriggerConnected = 7,
    kWiFiLinkQualityTriggerDHCPRenewOnRoam = 8,
    kWiFiLinkQualityTriggerDHCPSuccess = 9,
    kWiFiLinkQualityTriggerDHCPFailure = 10,
    kWiFiLinkQualityTriggerSlaacFinished = 11,
    kWiFiLinkQualityTriggerNetworkValidationStart = 12,
    kWiFiLinkQualityTriggerNetworkValidationSuccess = 13,
    kWiFiLinkQualityTriggerNetworkValidationFailure = 14,
  };

  enum WiFiChannelWidth {
    kWiFiChannelWidthUnknown = 0,
    kWiFiChannelWidth20MHz = 1,
    kWiFiChannelWidth40MHz = 2,
    kWiFiChannelWidth80MHz = 3,
    kWiFiChannelWidth80p80MHz = 4,  // 80+80MHz channels.
    kWiFiChannelWidth160MHz = 5,
    kWiFiChannelWidth320MHz = 6,
  };

  enum WiFiLinkMode {
    kWiFiLinkModeUnknown = 0,
    kWiFiLinkModeLegacy = 1,
    kWiFiLinkModeVHT = 2,
    kWiFiLinkModeHE = 3,
    kWiFiLinkModeEHT = 4,
  };

  enum WiFiGuardInterval {
    kWiFiGuardIntervalUnknown = 0,
    kWiFiGuardInterval_0_4 = 1,
    kWiFiGuardInterval_0_8 = 2,
    kWiFiGuardInterval_1_6 = 3,
    kWiFiGuardInterval_3_2 = 4,
  };

  struct WiFiRxTxStats {
    int64_t packets = kWiFiStructuredMetricsErrorValue;
    int64_t bytes = kWiFiStructuredMetricsErrorValue;
    int bitrate = kWiFiStructuredMetricsErrorValue;  // unit is 100 Kb/s.
    int mcs = kWiFiStructuredMetricsErrorValue;
    WiFiLinkMode mode = kWiFiLinkModeUnknown;
    WiFiGuardInterval gi = kWiFiGuardIntervalUnknown;
    int nss = kWiFiStructuredMetricsErrorValue;
    int dcm = kWiFiStructuredMetricsErrorValue;
    bool operator==(const WiFiRxTxStats& other) const {
      if (packets != other.packets) {
        return false;
      }
      if (bytes != other.bytes) {
        return false;
      }
      if (bitrate != other.bitrate) {
        return false;
      }
      if (mcs != other.mcs) {
        return false;
      }
      if (mode != other.mode) {
        return false;
      }
      if (gi != other.gi) {
        return false;
      }
      if (nss != other.nss) {
        return false;
      }
      if (dcm != other.dcm) {
        return false;
      }
      return true;
    }
    bool operator!=(const WiFiRxTxStats& other) const {
      return !(*this == other);
    }
  };

  enum BTStack {
    kBTStackUnknown = 0,
    kBTStackBlueZ = 1,
    kBTStackFloss = 2,
  };

  // For consistency, use the same integer values as Floss
  // https://android.googlesource.com/platform/packages/modules/Bluetooth/+/ac69da6c45771293530338709ee6e9599065ca5d/system/gd/rust/topshim/src/profiles/mod.rs#7
  enum BTProfileConnectionState {
    kBTProfileConnectionStateDisconnected = 0,
    kBTProfileConnectionStateDisconnecting = 1,
    kBTProfileConnectionStateConnecting = 2,
    kBTProfileConnectionStateConnected = 3,
    kBTProfileConnectionStateActive = 4,
    kBTProfileConnectionStateInvalid = 0x7FFFFFFE,
  };

  struct WiFiLinkQualityReport {
    int64_t tx_retries = kWiFiStructuredMetricsErrorValue;
    int64_t tx_failures = kWiFiStructuredMetricsErrorValue;
    int64_t rx_drops = kWiFiStructuredMetricsErrorValue;
    int64_t inactive_time = kWiFiStructuredMetricsErrorValue;
    int64_t fcs_errors = kWiFiStructuredMetricsErrorValue;
    int64_t rx_mpdus = kWiFiStructuredMetricsErrorValue;
    int chain0_signal = kWiFiStructuredMetricsErrorValueRSSI;
    int chain0_signal_avg = kWiFiStructuredMetricsErrorValueRSSI;
    int chain1_signal = kWiFiStructuredMetricsErrorValueRSSI;
    int chain1_signal_avg = kWiFiStructuredMetricsErrorValueRSSI;
    int beacon_signal_avg = kWiFiStructuredMetricsErrorValueRSSI;
    int signal = kWiFiStructuredMetricsErrorValueRSSI;
    int signal_avg = kWiFiStructuredMetricsErrorValueRSSI;
    int noise = kWiFiStructuredMetricsErrorValueRSSI;
    int last_ack_signal = kWiFiStructuredMetricsErrorValueRSSI;
    int ack_signal_avg = kWiFiStructuredMetricsErrorValueRSSI;
    int64_t beacons_received = kWiFiStructuredMetricsErrorValue;
    int64_t beacons_lost = kWiFiStructuredMetricsErrorValue;
    int64_t expected_throughput = kWiFiStructuredMetricsErrorValue;
    WiFiChannelWidth width = kWiFiChannelWidthUnknown;
    WiFiRxTxStats rx;
    WiFiRxTxStats tx;
    bool bt_enabled = false;
    BTStack bt_stack = kBTStackUnknown;
    BTProfileConnectionState bt_hfp = kBTProfileConnectionStateInvalid;
    BTProfileConnectionState bt_a2dp = kBTProfileConnectionStateInvalid;
    bool bt_active_scanning = false;
    bool operator==(const WiFiLinkQualityReport& other) const {
      if (tx_retries != other.tx_retries) {
        return false;
      }
      if (tx_failures != other.tx_failures) {
        return false;
      }
      if (rx_drops != other.rx_drops) {
        return false;
      }
      if (chain0_signal != other.chain0_signal) {
        return false;
      }
      if (chain0_signal_avg != other.chain0_signal_avg) {
        return false;
      }
      if (chain1_signal != other.chain1_signal) {
        return false;
      }
      if (chain1_signal_avg != other.chain1_signal_avg) {
        return false;
      }
      if (beacon_signal_avg != other.beacon_signal_avg) {
        return false;
      }
      if (beacons_received != other.beacons_received) {
        return false;
      }
      if (beacons_lost != other.beacons_lost) {
        return false;
      }
      if (expected_throughput != other.expected_throughput) {
        return false;
      }
      if (width != other.width) {
        return false;
      }
      if (rx != other.rx) {
        return false;
      }
      if (tx != other.tx) {
        return false;
      }
      if (bt_enabled != other.bt_enabled) {
        return false;
      }
      if (bt_stack != other.bt_stack) {
        return false;
      }
      if (bt_hfp != other.bt_hfp) {
        return false;
      }
      if (bt_a2dp != other.bt_a2dp) {
        return false;
      }
      if (bt_active_scanning != other.bt_active_scanning) {
        return false;
      }
      if (inactive_time != other.inactive_time) {
        return false;
      }
      if (fcs_errors != other.fcs_errors) {
        return false;
      }
      if (rx_mpdus != other.rx_mpdus) {
        return false;
      }
      if (signal != other.signal) {
        return false;
      }
      if (signal_avg != other.signal_avg) {
        return false;
      }
      if (noise != other.noise) {
        return false;
      }
      if (last_ack_signal != other.last_ack_signal) {
        return false;
      }
      if (ack_signal_avg != other.ack_signal_avg) {
        return false;
      }
      return true;
    }
    bool operator!=(const WiFiLinkQualityReport& other) const {
      return !(*this == other);
    }
  };

  // Emits the |WiFiLinkQualityTrigger| structured event.
  mockable void NotifyWiFiLinkQualityTrigger(WiFiLinkQualityTrigger trigger,
                                             uint64_t session_tag);

  // Emits the |WiFiLinkQualityReport| structured event. It contains information
  // about the quality of the wireless link (e.g. MCS index, rate of packet
  // loss, etc.)
  mockable void NotifyWiFiLinkQualityReport(const WiFiLinkQualityReport& report,
                                            uint64_t session_tag);

  // Returns a persistent hash to be used to uniquely identify an APN.
  static int64_t HashApn(const std::string& uuid,
                         const std::string& apn_name,
                         const std::string& username,
                         const std::string& password);

  // Converts GID1 from hex string to int64_t
  static std::optional<int64_t> IntGid1(const std::string& gid1);

  // Notifies the object that the wifi connection became unreliable.
  virtual void NotifyWiFiConnectionUnreliable();

  // Notifies the object that the BSSID has changed.
  virtual void NotifyBSSIDChanged();

  // Notifies the object that a rekey event has started.
  virtual void NotifyRekeyStart();

  // Notifies this object of the status when bad-passphrase is identified
  virtual void NotifyWiFiBadPassphrase(bool ever_connected, bool user_initiate);

  // Sends linear histogram data to UMA for a metric with a fixed name.
  virtual void SendEnumToUMA(const EnumMetric<FixedName>& metric, int sample);

  // Sends linear histogram data to UMA for a metric split by APN type.
  virtual void SendEnumToUMA(const EnumMetric<NameByApnType>& metric,
                             DetailedCellularConnectionResult::APNType type,
                             int sample);

  // Sends linear histogram data to UMA for a metric split by shill
  // Technology.
  virtual void SendEnumToUMA(const EnumMetric<NameByTechnology>& metric,
                             Technology tech,
                             int sample);

  // Sends linear histogram data to UMA for a metric split by VPN type.
  virtual void SendEnumToUMA(const EnumMetric<NameByVPNType>& metric,
                             VPNType type,
                             int sample);

  // Sends linear histogram data to UMA for a metric with a prefix name.
  virtual void SendEnumToUMA(const EnumMetric<PrefixName>& metric,
                             const std::string& suffix,
                             int sample);

  // Sends logarithmic histogram data to UMA for a metric with a fixed name.
  virtual void SendToUMA(const HistogramMetric<FixedName>& metric, int sample);

  // Sends logarithmic histogram data to UMA for a metric split by shill
  // Technology.
  virtual void SendToUMA(const HistogramMetric<NameByTechnology>& metric,
                         Technology tech,
                         int sample);

  // Sends logarithmic histogram data to UMA for a metric with a prefix name.
  virtual void SendToUMA(const HistogramMetric<PrefixName>& metric,
                         const std::string& suffix,
                         int sample);

  // Sends logarithmic histogram data to UMA for a metric split by VPN type.
  virtual void SendToUMA(const HistogramMetric<NameByVPNType>& metric,
                         VPNType type,
                         int sample);

  // Sends sparse histogram data to UMA for a metric with a fixed name.
  virtual void SendSparseToUMA(const SparseMetric<FixedName>& metric,
                               int sample);

  // Sends sparse histogram data to UMA for a metric split by shill technology
  virtual void SendSparseToUMA(const SparseMetric<NameByTechnology>& metric,
                               Technology technology,
                               int sample);

  // Reports the elapsed time recorded by |timer| for the histogram name and
  // settings defined by |timer|.
  void ReportMilliseconds(const chromeos_metrics::TimerReporter& timer);

  void SetLibraryForTesting(MetricsLibraryInterface* library);

 private:
  friend class MetricsTest;
  FRIEND_TEST(MetricsTest, FrequencyToChannel);
  FRIEND_TEST(MetricsTest, ResetConnectTimer);
  FRIEND_TEST(MetricsTest, TimeFromRekeyToFailureBSSIDChange);
  FRIEND_TEST(MetricsTest, TimeFromRekeyToFailureExceedMaxDuration);
  FRIEND_TEST(MetricsTest, TimeFromRekeyToFailureValidDuration);
  FRIEND_TEST(MetricsTest, TimeToScanIgnore);
  FRIEND_TEST(WiFiMainTest, UpdateGeolocationObjects);

  struct DeviceMetrics {
    DeviceMetrics() {}
    Technology technology;
    std::unique_ptr<chromeos_metrics::TimerReporter> initialization_timer;
    std::unique_ptr<chromeos_metrics::TimerReporter> enable_timer;
    std::unique_ptr<chromeos_metrics::TimerReporter> disable_timer;
    std::unique_ptr<chromeos_metrics::TimerReporter> scan_timer;
    std::unique_ptr<chromeos_metrics::TimerReporter> connect_timer;
    std::unique_ptr<chromeos_metrics::TimerReporter> scan_connect_timer;
  };
  using DeviceMetricsLookupMap =
      std::map<const int, std::unique_ptr<DeviceMetrics>>;

  static constexpr uint16_t kWiFiBandwidth5MHz = 5;
  static constexpr uint16_t kWiFiBandwidth20MHz = 20;
  static constexpr uint16_t kWiFiFrequency2412 = 2412;
  static constexpr uint16_t kWiFiFrequency2472 = 2472;
  static constexpr uint16_t kWiFiFrequency2484 = 2484;
  static constexpr uint16_t kWiFiFrequency5170 = 5170;
  static constexpr uint16_t kWiFiFrequency5180 = 5180;
  static constexpr uint16_t kWiFiFrequency5230 = 5230;
  static constexpr uint16_t kWiFiFrequency5240 = 5240;
  static constexpr uint16_t kWiFiFrequency5320 = 5320;
  static constexpr uint16_t kWiFiFrequency5500 = 5500;
  static constexpr uint16_t kWiFiFrequency5700 = 5700;
  static constexpr uint16_t kWiFiFrequency5745 = 5745;
  static constexpr uint16_t kWiFiFrequency5825 = 5825;
  static constexpr uint16_t kWiFiFrequency5955 = 5955;
  static constexpr uint16_t kWiFiFrequency7115 = 7115;

  DeviceMetrics* GetDeviceMetrics(int interface_index) const;

  void set_time_to_scan_timer(int interface_index,
                              chromeos_metrics::TimerReporter* timer) {
    DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
    device_metrics->scan_timer.reset(timer);  // Passes ownership
  }
  void set_time_to_connect_timer(int interface_index,
                                 chromeos_metrics::TimerReporter* timer) {
    DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
    device_metrics->connect_timer.reset(timer);  // Passes ownership
  }
  void set_time_to_scan_connect_timer(int interface_index,
                                      chromeos_metrics::TimerReporter* timer) {
    DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
    device_metrics->scan_connect_timer.reset(timer);  // Passes ownership
  }
  void set_time_between_rekey_and_connection_failure_timer(
      chromeos_metrics::Timer* timer) {
    time_between_rekey_and_connection_failure_timer_.reset(
        timer);  // Passes ownership
  }

  // Return a pseudonymized string (salted+hashed) version of the session tag.
  std::string PseudonymizeTag(uint64_t tag);

  // |library_| points to |metrics_library_| when shill runs normally.
  // However, in order to allow for unit testing, we point |library_| to a
  // MetricsLibraryMock object instead.
  MetricsLibrary metrics_library_;
  MetricsLibraryInterface* library_;
  // Randomly generated 32 bytes used as a salt to pseudonymize session tags.
  std::string pseudo_tag_salt_;
  std::unique_ptr<chromeos_metrics::Timer>
      time_between_rekey_and_connection_failure_timer_;
  DeviceMetricsLookupMap devices_metrics_;
};

std::ostream& operator<<(std::ostream& stream,
                         const Metrics::EnumMetric<Metrics::FixedName>& metric);
std::ostream& operator<<(
    std::ostream& stream,
    const Metrics::EnumMetric<Metrics::NameByApnType>& metric);
std::ostream& operator<<(
    std::ostream& stream,
    const Metrics::EnumMetric<Metrics::NameByTechnology>& metric);
std::ostream& operator<<(
    std::ostream& stream,
    const Metrics::EnumMetric<Metrics::NameByVPNType>& metric);
std::ostream& operator<<(
    std::ostream& stream,
    const Metrics::EnumMetric<Metrics::PrefixName>& metric);
std::ostream& operator<<(
    std::ostream& stream,
    const Metrics::HistogramMetric<Metrics::FixedName>& metric);
std::ostream& operator<<(
    std::ostream& stream,
    const Metrics::HistogramMetric<Metrics::NameByTechnology>& metric);
std::ostream& operator<<(
    std::ostream& stream,
    const Metrics::HistogramMetric<Metrics::PrefixName>& metric);
std::ostream& operator<<(
    std::ostream& stream,
    const Metrics::SparseMetric<Metrics::FixedName>& metric);
std::ostream& operator<<(
    std::ostream& stream,
    const Metrics::SparseMetric<Metrics::NameByTechnology>& metric);

}  // namespace shill

#endif  // SHILL_METRICS_H_
