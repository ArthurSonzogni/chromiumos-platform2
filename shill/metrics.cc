// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/metrics.h"

#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/containers/contains.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/dbus/shill/dbus-constants.h>
#include <crypto/random.h>
#include <crypto/sha2.h>
#include <metrics/bootstat.h>
#include <metrics/structured_events.h>

#include "shill/cellular/apn_list.h"
#include "shill/cellular/cellular_consts.h"
#include "shill/connection_diagnostics.h"
#include "shill/logging.h"
#include "shill/vpn/vpn_types.h"
#include "shill/wifi/wifi_metrics_utils.h"
#include "shill/wifi/wifi_service.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kMetrics;
}  // namespace Logging

namespace {

// Name prefix used for Shill UMA metrics whose names are generated
// dynamically at event recording time.
constexpr char kMetricPrefix[] = "Network.Shill";

// Length of the random salt used to pseudonymize logs.
constexpr int kPseudoTagSaltLen = 32;
// How many bytes of the hash are printed.
constexpr int kPseudoTagHashLen = 8;

bool IsInvalidTag(uint64_t tag) {
  return tag == WiFiService::kSessionTagInvalid;
}

int64_t GetMicroSecondsMonotonic() {
  // base::TimeTicks doesn't provide a method to convert to the internal value.
  // So we get the base::TimeDelta by subtracting epoch first, then convert it
  // to the microseconds.
  return (base::TimeTicks::Now() - base::TimeTicks::UnixEpoch())
      .InMicroseconds();
}

Metrics::CellularConnectResult ConvertErrorToCellularConnectResult(
    const Error::Type& error) {
  switch (error) {
    case Error::kSuccess:
      return Metrics::CellularConnectResult::kCellularConnectResultSuccess;
    case Error::kWrongState:
      return Metrics::CellularConnectResult::kCellularConnectResultWrongState;
    case Error::kOperationFailed:
      return Metrics::CellularConnectResult::
          kCellularConnectResultOperationFailed;
    case Error::kAlreadyConnected:
      return Metrics::CellularConnectResult::
          kCellularConnectResultAlreadyConnected;
    case Error::kNotRegistered:
      return Metrics::CellularConnectResult::
          kCellularConnectResultNotRegistered;
    case Error::kNotOnHomeNetwork:
      return Metrics::CellularConnectResult::
          kCellularConnectResultNotOnHomeNetwork;
    case Error::kIncorrectPin:
      return Metrics::CellularConnectResult::kCellularConnectResultIncorrectPin;
    case Error::kPinRequired:
      return Metrics::CellularConnectResult::kCellularConnectResultPinRequired;
    case Error::kPinBlocked:
      return Metrics::CellularConnectResult::kCellularConnectResultPinBlocked;
    case Error::kInvalidApn:
      return Metrics::CellularConnectResult::kCellularConnectResultInvalidApn;
    case Error::kInternalError:
      return Metrics::CellularConnectResult::
          kCellularConnectResultInternalError;
    default:
      LOG(WARNING) << "Unexpected error type: " << error;
      return Metrics::CellularConnectResult::kCellularConnectResultUnknown;
  }
}

// Converts VPN types to strings used in a metric name.
std::string VPNTypeToMetricString(VPNType type) {
  switch (type) {
    case VPNType::kARC:
      return "ARC";
    case VPNType::kIKEv2:
      return "Ikev2";
    case VPNType::kL2TPIPsec:
      return "L2tpIpsec";
    case VPNType::kOpenVPN:
      return "OpenVPN";
    case VPNType::kThirdParty:
      return "ThirdParty";
    case VPNType::kWireGuard:
      return "WireGuard";
  }
  NOTREACHED();
  return "";
}

std::string GetApnTypeString(
    Metrics::DetailedCellularConnectionResult::APNType apn_type) {
  switch (apn_type) {
    case Metrics::DetailedCellularConnectionResult::APNType::kDefault:
      return kApnTypeDefault;
    case Metrics::DetailedCellularConnectionResult::APNType::kAttach:
      return kApnTypeIA;
    case Metrics::DetailedCellularConnectionResult::APNType::kDUN:
      return kApnTypeDun;
  }
}

}  // namespace

Metrics::Metrics()
    : library_(&metrics_library_),
      time_suspend_actions_timer(new chromeos_metrics::Timer),
      time_between_rekey_and_connection_failure_timer_(
          new chromeos_metrics::Timer) {
  char salt[kPseudoTagSaltLen];
  crypto::RandBytes(salt, kPseudoTagSaltLen);
  pseudo_tag_salt_ = std::string(salt, kPseudoTagSaltLen);
}

Metrics::~Metrics() = default;

void Metrics::SendEnumToUMA(const EnumMetric<FixedName>& metric, int sample) {
  // The std::string conversion should be removed once MetricsLibraryInterface
  // is improved with std::string_view variants of Send*ToUMA.
  library_->SendEnumToUMA(std::string(metric.n.name), sample, metric.max);
}

void Metrics::SendEnumToUMA(const EnumMetric<NameByApnType>& metric,
                            DetailedCellularConnectionResult::APNType type,
                            int sample) {
  // Using the format Network.Shill.Cellular.{MetricName}.{ApnType} to make it
  // easier to find the metrics using autocomplete in UMA.
  const std::string name =
      base::StrCat({kMetricPrefix, ".Cellular.", metric.n.name, ".",
                    GetApnTypeString(type)});
  library_->SendEnumToUMA(name, sample, metric.max);
}

void Metrics::SendEnumToUMA(const EnumMetric<NameByTechnology>& metric,
                            Technology tech,
                            int sample) {
  library_->SendEnumToUMA(
      GetFullMetricName(metric.n.name, tech, metric.n.location), sample,
      metric.max);
}

void Metrics::SendEnumToUMA(const EnumMetric<NameByVPNType>& metric,
                            VPNType type,
                            int sample) {
  const std::string name =
      base::StrCat({kMetricPrefix, ".Vpn.", VPNTypeToMetricString(type), ".",
                    metric.n.name});
  library_->SendEnumToUMA(name, sample, metric.max);
}

void Metrics::SendEnumToUMA(const EnumMetric<PrefixName>& metric,
                            const std::string& suffix,
                            int sample) {
  library_->SendEnumToUMA(base::StrCat({metric.n.prefix, suffix}), sample,
                          metric.max);
}

void Metrics::SendToUMA(const Metrics::HistogramMetric<FixedName>& metric,
                        int sample) {
  // The std::string conversion should be removed once MetricsLibraryInterface
  // is improved with std::string_view variants of Send*ToUMA.
  library_->SendToUMA(std::string(metric.n.name), sample, metric.min,
                      metric.max, metric.num_buckets);
}

void Metrics::SendToUMA(
    const Metrics::HistogramMetric<NameByTechnology>& metric,
    Technology tech,
    int sample) {
  library_->SendToUMA(GetFullMetricName(metric.n.name, tech, metric.n.location),
                      sample, metric.min, metric.max, metric.num_buckets);
}

void Metrics::SendToUMA(const Metrics::HistogramMetric<PrefixName>& metric,
                        const std::string& suffix,
                        int sample) {
  library_->SendToUMA(base::StrCat({metric.n.prefix, suffix}), sample,
                      metric.min, metric.max, metric.num_buckets);
}

void Metrics::SendSparseToUMA(const SparseMetric<FixedName>& metric,
                              int sample) {
  // The std::string conversion should be removed once MetricsLibraryInterface
  // is improved with std::string_view variants of Send*ToUMA.
  library_->SendSparseToUMA(std::string(metric.n.name), sample);
}

void Metrics::SendSparseToUMA(const SparseMetric<NameByTechnology>& metric,
                              Technology technology,
                              int sample) {
  library_->SendSparseToUMA(
      GetFullMetricName(metric.n.name, technology, metric.n.location), sample);
}

// static
Metrics::WiFiChannel Metrics::WiFiFrequencyToChannel(uint16_t frequency) {
  WiFiChannel channel = kWiFiChannelUndef;
  if (kWiFiFrequency2412 <= frequency && frequency <= kWiFiFrequency2472) {
    if (((frequency - kWiFiFrequency2412) % kWiFiBandwidth5MHz) == 0)
      channel = static_cast<WiFiChannel>(kWiFiChannel2412 +
                                         (frequency - kWiFiFrequency2412) /
                                             kWiFiBandwidth5MHz);
  } else if (frequency == kWiFiFrequency2484) {
    channel = kWiFiChannel2484;
  } else if (kWiFiFrequency5170 <= frequency &&
             frequency <= kWiFiFrequency5230) {
    if ((frequency % kWiFiBandwidth20MHz) == 0)
      channel = static_cast<WiFiChannel>(kWiFiChannel5180 +
                                         (frequency - kWiFiFrequency5180) /
                                             kWiFiBandwidth20MHz);
    if ((frequency % kWiFiBandwidth20MHz) == 10)
      channel = static_cast<WiFiChannel>(kWiFiChannel5170 +
                                         (frequency - kWiFiFrequency5170) /
                                             kWiFiBandwidth20MHz);
  } else if (kWiFiFrequency5240 <= frequency &&
             frequency <= kWiFiFrequency5320) {
    if (((frequency - kWiFiFrequency5180) % kWiFiBandwidth20MHz) == 0)
      channel = static_cast<WiFiChannel>(kWiFiChannel5180 +
                                         (frequency - kWiFiFrequency5180) /
                                             kWiFiBandwidth20MHz);
  } else if (kWiFiFrequency5500 <= frequency &&
             frequency <= kWiFiFrequency5700) {
    if (((frequency - kWiFiFrequency5500) % kWiFiBandwidth20MHz) == 0)
      channel = static_cast<WiFiChannel>(kWiFiChannel5500 +
                                         (frequency - kWiFiFrequency5500) /
                                             kWiFiBandwidth20MHz);
  } else if (kWiFiFrequency5745 <= frequency &&
             frequency <= kWiFiFrequency5825) {
    if (((frequency - kWiFiFrequency5745) % kWiFiBandwidth20MHz) == 0)
      channel = static_cast<WiFiChannel>(kWiFiChannel5745 +
                                         (frequency - kWiFiFrequency5745) /
                                             kWiFiBandwidth20MHz);
  } else if (kWiFiFrequency5955 <= frequency &&
             frequency <= kWiFiFrequency7115) {
    if (((frequency - kWiFiFrequency5955) % kWiFiBandwidth20MHz) == 0)
      channel = static_cast<WiFiChannel>(kWiFiChannel5955 +
                                         (frequency - kWiFiFrequency5955) /
                                             kWiFiBandwidth20MHz);
  }
  CHECK(kWiFiChannelUndef <= channel && channel < kWiFiChannelMax);

  if (channel == kWiFiChannelUndef)
    LOG(WARNING) << "no mapping for frequency " << frequency;
  else
    SLOG(3) << "mapped frequency " << frequency << " to enum bucket "
            << channel;

  return channel;
}

// static
Metrics::WiFiFrequencyRange Metrics::WiFiChannelToFrequencyRange(
    Metrics::WiFiChannel channel) {
  if (channel >= kWiFiChannelMin24 && channel <= kWiFiChannelMax24) {
    return kWiFiFrequencyRange24;
  } else if (channel >= kWiFiChannelMin5 && channel <= kWiFiChannelMax5) {
    return kWiFiFrequencyRange5;
  } else if (channel >= kWiFiChannelMin6 && channel <= kWiFiChannelMax6) {
    return kWiFiFrequencyRange6;
  } else {
    return kWiFiFrequencyRangeUndef;
  }
}

// static
Metrics::EapOuterProtocol Metrics::EapOuterProtocolStringToEnum(
    const std::string& outer) {
  if (outer == kEapMethodPEAP) {
    return kEapOuterProtocolPeap;
  } else if (outer == kEapMethodTLS) {
    return kEapOuterProtocolTls;
  } else if (outer == kEapMethodTTLS) {
    return kEapOuterProtocolTtls;
  } else if (outer == kEapMethodLEAP) {
    return kEapOuterProtocolLeap;
  } else {
    return kEapOuterProtocolUnknown;
  }
}

// static
Metrics::EapInnerProtocol Metrics::EapInnerProtocolStringToEnum(
    const std::string& inner) {
  if (inner.empty()) {
    return kEapInnerProtocolNone;
  } else if (inner == kEapPhase2AuthPEAPMD5) {
    return kEapInnerProtocolPeapMd5;
  } else if (inner == kEapPhase2AuthPEAPMSCHAPV2) {
    return kEapInnerProtocolPeapMschapv2;
  } else if (inner == kEapPhase2AuthTTLSEAPMD5) {
    return kEapInnerProtocolTtlsEapMd5;
  } else if (inner == kEapPhase2AuthTTLSEAPMSCHAPV2) {
    return kEapInnerProtocolTtlsEapMschapv2;
  } else if (inner == kEapPhase2AuthTTLSMSCHAPV2) {
    return kEapInnerProtocolTtlsMschapv2;
  } else if (inner == kEapPhase2AuthTTLSMSCHAP) {
    return kEapInnerProtocolTtlsMschap;
  } else if (inner == kEapPhase2AuthTTLSPAP) {
    return kEapInnerProtocolTtlsPap;
  } else if (inner == kEapPhase2AuthTTLSCHAP) {
    return kEapInnerProtocolTtlsChap;
  } else {
    return kEapInnerProtocolUnknown;
  }
}

// static
std::string Metrics::GetFullMetricName(std::string_view metric_name,
                                       Technology technology_id,
                                       TechnologyLocation location) {
  std::string technology = TechnologyName(technology_id);
  technology[0] = base::ToUpperASCII(technology[0]);
  if (location == TechnologyLocation::kBeforeName) {
    return base::StrCat({kMetricPrefix, ".", technology, ".", metric_name});
  } else {
    return base::StrCat({kMetricPrefix, ".", metric_name, ".", technology});
  }
}

void Metrics::NotifySuspendActionsStarted() {
  if (time_suspend_actions_timer->HasStarted())
    return;
  time_suspend_actions_timer->Start();
}

void Metrics::NotifySuspendActionsCompleted(bool success) {
  if (!time_suspend_actions_timer->HasStarted())
    return;

  base::TimeDelta elapsed_time;
  time_suspend_actions_timer->GetElapsedTime(&elapsed_time);
  time_suspend_actions_timer->Reset();
  SendToUMA(kMetricSuspendActionTimeTaken, elapsed_time.InMilliseconds());
}

void Metrics::NotifyApChannelSwitch(uint16_t frequency,
                                    uint16_t new_frequency) {
  WiFiChannel channel = WiFiFrequencyToChannel(frequency);
  WiFiChannel new_channel = WiFiFrequencyToChannel(new_frequency);
  WiFiFrequencyRange range = WiFiChannelToFrequencyRange(channel);
  WiFiFrequencyRange new_range = WiFiChannelToFrequencyRange(new_channel);
  WiFiApChannelSwitch channel_switch = kWiFiApChannelSwitchUndef;
  if (range == kWiFiFrequencyRange24 && new_range == kWiFiFrequencyRange24) {
    channel_switch = kWiFiApChannelSwitch24To24;
  } else if (range == kWiFiFrequencyRange24 &&
             new_range == kWiFiFrequencyRange5) {
    channel_switch = kWiFiApChannelSwitch24To5;
  } else if (range == kWiFiFrequencyRange5 &&
             new_range == kWiFiFrequencyRange24) {
    channel_switch = kWiFiApChannelSwitch5To24;
  } else if (range == kWiFiFrequencyRange5 &&
             new_range == kWiFiFrequencyRange5) {
    channel_switch = kWiFiApChannelSwitch5To5;
  }
  SendEnumToUMA(kMetricApChannelSwitch, channel_switch);
}

void Metrics::NotifyAp80211kSupport(bool neighbor_list_supported) {
  SendBoolToUMA(kMetricAp80211kSupport, neighbor_list_supported);
}

void Metrics::NotifyAp80211rSupport(bool ota_ft_supported,
                                    bool otds_ft_supported) {
  WiFiAp80211rSupport support = kWiFiAp80211rNone;
  if (otds_ft_supported) {
    support = kWiFiAp80211rOTDS;
  } else if (ota_ft_supported) {
    support = kWiFiAp80211rOTA;
  }
  SendEnumToUMA(kMetricAp80211rSupport, support);
}

void Metrics::NotifyAp80211vDMSSupport(bool dms_supported) {
  SendBoolToUMA(kMetricAp80211vDMSSupport, dms_supported);
}

void Metrics::NotifyAp80211vBSSMaxIdlePeriodSupport(
    bool bss_max_idle_period_supported) {
  SendBoolToUMA(kMetricAp80211vBSSMaxIdlePeriodSupport,
                bss_max_idle_period_supported);
}

void Metrics::NotifyAp80211vBSSTransitionSupport(
    bool bss_transition_supported) {
  SendBoolToUMA(kMetricAp80211vBSSTransitionSupport, bss_transition_supported);
}

void Metrics::NotifyCiscoAdaptiveFTSupport(bool adaptive_ft_supported) {
  SendBoolToUMA(kMetricCiscoAdaptiveFTSupport, adaptive_ft_supported);
}

void Metrics::Notify80211Disconnect(WiFiDisconnectByWhom by_whom,
                                    IEEE_80211::WiFiReasonCode reason) {
  EnumMetric<FixedName> metric_disconnect_reason;
  EnumMetric<FixedName> metric_disconnect_type;
  WiFiReasonType type;

  if (by_whom == kDisconnectedByAp) {
    metric_disconnect_reason = kMetricLinkApDisconnectReason;
    metric_disconnect_type = kMetricLinkApDisconnectType;
    type = kReasonCodeTypeByAp;
  } else {
    metric_disconnect_reason = kMetricLinkClientDisconnectReason;
    metric_disconnect_type = kMetricLinkClientDisconnectType;
    switch (reason) {
      case IEEE_80211::kReasonCodeSenderHasLeft:
      case IEEE_80211::kReasonCodeDisassociatedHasLeft:
        type = kReasonCodeTypeByUser;
        break;

      case IEEE_80211::kReasonCodeInactivity:
        type = kReasonCodeTypeConsideredDead;
        break;

      default:
        type = kReasonCodeTypeByClient;
        break;
    }
  }
  SendEnumToUMA(metric_disconnect_reason, reason);
  SendEnumToUMA(metric_disconnect_type, type);
}

void Metrics::RegisterDevice(int interface_index, Technology technology) {
  SLOG(2) << __func__ << ": " << interface_index;

  if (IsPrimaryConnectivityTechnology(technology)) {
    bootstat::BootStat().LogEvent(
        base::StringPrintf("network-%s-registered",
                           TechnologyName(technology).c_str())
            .c_str());
  }

  auto device_metrics = std::make_unique<DeviceMetrics>();
  device_metrics->technology = technology;
  auto histogram =
      GetFullMetricName(kMetricTimeToInitializeMillisecondsSuffix, technology);
  device_metrics->initialization_timer.reset(
      new chromeos_metrics::TimerReporter(
          histogram, kMetricTimeToInitializeMillisecondsMin,
          kMetricTimeToInitializeMillisecondsMax,
          kMetricTimeToInitializeMillisecondsNumBuckets));
  device_metrics->initialization_timer->Start();
  histogram =
      GetFullMetricName(kMetricTimeToEnableMillisecondsSuffix, technology);
  device_metrics->enable_timer.reset(new chromeos_metrics::TimerReporter(
      histogram, kMetricTimeToEnableMillisecondsMin,
      kMetricTimeToEnableMillisecondsMax,
      kMetricTimeToEnableMillisecondsNumBuckets));
  histogram =
      GetFullMetricName(kMetricTimeToDisableMillisecondsSuffix, technology);
  device_metrics->disable_timer.reset(new chromeos_metrics::TimerReporter(
      histogram, kMetricTimeToDisableMillisecondsMin,
      kMetricTimeToDisableMillisecondsMax,
      kMetricTimeToDisableMillisecondsNumBuckets));
  histogram =
      GetFullMetricName(kMetricTimeToScanMillisecondsSuffix, technology);
  device_metrics->scan_timer.reset(new chromeos_metrics::TimerReporter(
      histogram, kMetricTimeToScanMillisecondsMin,
      kMetricTimeToScanMillisecondsMax,
      kMetricTimeToScanMillisecondsNumBuckets));
  histogram =
      GetFullMetricName(kMetricTimeToConnectMillisecondsSuffix, technology);
  device_metrics->connect_timer.reset(new chromeos_metrics::TimerReporter(
      histogram, kMetricTimeToConnectMillisecondsMin,
      kMetricTimeToConnectMillisecondsMax,
      kMetricTimeToConnectMillisecondsNumBuckets));
  histogram = GetFullMetricName(kMetricTimeToScanAndConnectMillisecondsSuffix,
                                technology);
  device_metrics->scan_connect_timer.reset(new chromeos_metrics::TimerReporter(
      histogram, kMetricTimeToScanMillisecondsMin,
      kMetricTimeToScanMillisecondsMax + kMetricTimeToConnectMillisecondsMax,
      kMetricTimeToScanMillisecondsNumBuckets +
          kMetricTimeToConnectMillisecondsNumBuckets));
  devices_metrics_[interface_index] = std::move(device_metrics);
}

bool Metrics::IsDeviceRegistered(int interface_index, Technology technology) {
  SLOG(2) << __func__ << ": interface index: " << interface_index
          << ", technology: " << technology;
  DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
  if (device_metrics == nullptr)
    return false;
  // Make sure the device technologies match.
  return (technology == device_metrics->technology);
}

void Metrics::DeregisterDevice(int interface_index) {
  SLOG(2) << __func__ << ": interface index: " << interface_index;
  devices_metrics_.erase(interface_index);
}

void Metrics::NotifyDeviceInitialized(int interface_index) {
  DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
  if (device_metrics == nullptr) {
    return;
  }
  if (!device_metrics->initialization_timer->Stop()) {
    return;
  }
  ReportMilliseconds(*device_metrics->initialization_timer);
}

void Metrics::NotifyDeviceEnableStarted(int interface_index) {
  DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
  if (device_metrics == nullptr)
    return;
  device_metrics->enable_timer->Start();
}

void Metrics::NotifyDeviceEnableFinished(int interface_index) {
  DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
  if (device_metrics == nullptr) {
    return;
  }
  if (!device_metrics->enable_timer->Stop()) {
    return;
  }
  ReportMilliseconds(*device_metrics->enable_timer);
}

void Metrics::NotifyDeviceDisableStarted(int interface_index) {
  DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
  if (device_metrics == nullptr) {
    return;
  }
  device_metrics->disable_timer->Start();
}

void Metrics::NotifyDeviceDisableFinished(int interface_index) {
  DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
  if (device_metrics == nullptr) {
    return;
  }
  if (!device_metrics->disable_timer->Stop()) {
    return;
  }
  ReportMilliseconds(*device_metrics->disable_timer);
}

void Metrics::NotifyDeviceScanStarted(int interface_index) {
  DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
  if (device_metrics == nullptr) {
    return;
  }
  device_metrics->scan_timer->Start();
  device_metrics->scan_connect_timer->Start();
}

void Metrics::NotifyDeviceScanFinished(int interface_index) {
  DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
  if (device_metrics == nullptr) {
    return;
  }
  if (!device_metrics->scan_timer->Stop()) {
    return;
  }
  // Don't send TimeToScan metrics if the elapsed time exceeds the max metrics
  // value.  Huge scan times usually mean something's gone awry; for cellular,
  // for instance, this usually means that the modem is in an area without
  // service and we're not interested in this scenario.
  base::TimeDelta elapsed_time;
  device_metrics->scan_timer->GetElapsedTime(&elapsed_time);
  if (elapsed_time.InMilliseconds() <= kMetricTimeToScanMillisecondsMax) {
    ReportMilliseconds(*device_metrics->scan_timer);
  }
}

void Metrics::ReportDeviceScanResultToUma(Metrics::WiFiScanResult result) {
  SendEnumToUMA(kMetricScanResult, result);
}

void Metrics::ResetScanTimer(int interface_index) {
  DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
  if (device_metrics == nullptr)
    return;
  device_metrics->scan_timer->Reset();
}

void Metrics::NotifyDeviceConnectStarted(int interface_index) {
  DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
  if (device_metrics == nullptr)
    return;
  device_metrics->connect_timer->Start();
}

void Metrics::NotifyDeviceConnectFinished(int interface_index) {
  DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
  if (device_metrics == nullptr) {
    return;
  }
  if (!device_metrics->connect_timer->Stop()) {
    return;
  }
  ReportMilliseconds(*device_metrics->connect_timer);

  if (!device_metrics->scan_connect_timer->Stop())
    return;
  ReportMilliseconds(*device_metrics->scan_connect_timer);
}

void Metrics::ResetConnectTimer(int interface_index) {
  DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
  if (device_metrics == nullptr) {
    return;
  }
  device_metrics->connect_timer->Reset();
  device_metrics->scan_connect_timer->Reset();
}

void Metrics::NotifyCellularDeviceDrop(const std::string& network_technology,
                                       uint16_t signal_strength) {
  SLOG(2) << __func__ << ": " << network_technology << ", " << signal_strength;
  CellularDropTechnology drop_technology = kCellularDropTechnologyUnknown;
  if (network_technology == kNetworkTechnology1Xrtt) {
    drop_technology = kCellularDropTechnology1Xrtt;
  } else if (network_technology == kNetworkTechnologyEdge) {
    drop_technology = kCellularDropTechnologyEdge;
  } else if (network_technology == kNetworkTechnologyEvdo) {
    drop_technology = kCellularDropTechnologyEvdo;
  } else if (network_technology == kNetworkTechnologyGprs) {
    drop_technology = kCellularDropTechnologyGprs;
  } else if (network_technology == kNetworkTechnologyGsm) {
    drop_technology = kCellularDropTechnologyGsm;
  } else if (network_technology == kNetworkTechnologyHspa) {
    drop_technology = kCellularDropTechnologyHspa;
  } else if (network_technology == kNetworkTechnologyHspaPlus) {
    drop_technology = kCellularDropTechnologyHspaPlus;
  } else if (network_technology == kNetworkTechnologyLte) {
    drop_technology = kCellularDropTechnologyLte;
  } else if (network_technology == kNetworkTechnologyUmts) {
    drop_technology = kCellularDropTechnologyUmts;
  } else if (network_technology == kNetworkTechnology5gNr) {
    drop_technology = kCellularDropTechnology5gNr;
  }
  SendEnumToUMA(kMetricCellularDrop, drop_technology);
  SendToUMA(kMetricCellularSignalStrengthBeforeDrop, signal_strength);
}

void Metrics::NotifyCellularConnectionResult(
    Error::Type error, DetailedCellularConnectionResult::APNType apn_type) {
  SLOG(2) << __func__ << ": " << error;
  DCHECK(apn_type != DetailedCellularConnectionResult::APNType::kAttach)
      << "shill should not send this metric for Attach APNs";
  CellularConnectResult connect_result =
      ConvertErrorToCellularConnectResult(error);
  SendEnumToUMA(kMetricCellularConnectResult, apn_type,
                static_cast<int>(connect_result));
}

int64_t Metrics::HashApn(const std::string& uuid,
                         const std::string& apn_name,
                         const std::string& username,
                         const std::string& password) {
  std::string string1, string2;

  base::TrimString(uuid, " ", &string1);
  base::TrimString(apn_name, " ", &string2);
  string1 += string2;
  base::TrimString(username, " ", &string2);
  string1 += string2;
  base::TrimString(password, " ", &string2);
  string1 += string2;

  int64_t hash;
  crypto::SHA256HashString(string1, &hash, 8);
  return hash;
}

std::optional<int64_t> Metrics::IntGid1(const std::string& gid1) {
  // Ignore if GID1 not populated in the SIM card
  if (gid1.empty())
    return std::nullopt;
  // GID1 has no predefined max length defined, so limit it ourselves
  //   * Input string is in HEX (so 2 chars per byte).
  //   * Limit the input string to 8 bytes in order to fit it in a
  //     64bit integer value.
  //   * The most usual cases are 0, 1 or 2 bytes,
  int64_t parsed;
  if (!base::HexStringToInt64(gid1.substr(0, 2 * (sizeof(int64_t)) - 1),
                              &parsed)) {
    LOG(ERROR) << "Failed to parse GID1 as an integer: " << gid1;
    return std::nullopt;
  }
  return parsed;
}

void Metrics::NotifyDetailedCellularConnectionResult(
    const DetailedCellularConnectionResult& result) {
  int64_t home, serving, detailed_error_hash;
  CellularApnSource apn_source = kCellularApnSourceUi;
  std::string apn_name;
  std::string username;
  std::string password;
  CellularRoamingState roaming =
      CellularRoamingState::kCellularRoamingStateUnknown;
  CellularConnectResult connect_result =
      ConvertErrorToCellularConnectResult(result.error);
  uint32_t connect_time = 0;
  uint32_t scan_connect_time = 0;
  DeviceMetrics* device_metrics = GetDeviceMetrics(result.interface_index);

  base::StringToInt64(result.home_mccmnc, &home);
  base::StringToInt64(result.serving_mccmnc, &serving);
  crypto::SHA256HashString(result.detailed_error, &detailed_error_hash, 8);

  if (result.roaming_state == kRoamingStateHome)
    roaming = kCellularRoamingStateHome;
  else if (result.roaming_state == kRoamingStateRoaming)
    roaming = kCellularRoamingStateRoaming;

  DCHECK(base::Contains(result.apn_info, kApnSourceProperty));
  if (base::Contains(result.apn_info, kApnSourceProperty)) {
    if (result.apn_info.at(kApnSourceProperty) == cellular::kApnSourceMoDb)
      apn_source = kCellularApnSourceMoDb;
    else if (result.apn_info.at(kApnSourceProperty) == kApnSourceUi)
      apn_source = kCellularApnSourceUi;
    else if (result.apn_info.at(kApnSourceProperty) ==
             cellular::kApnSourceModem)
      apn_source = kCellularApnSourceModem;
    else if (result.apn_info.at(kApnSourceProperty) ==
             cellular::kApnSourceFallback)
      apn_source = kCellularApnSourceFallback;

    if (result.apn_info.at(kApnSourceProperty) == cellular::kApnSourceMoDb ||
        result.apn_info.at(kApnSourceProperty) == cellular::kApnSourceModem) {
      if (base::Contains(result.apn_info, kApnProperty))
        apn_name = result.apn_info.at(kApnProperty);
      if (base::Contains(result.apn_info, kApnUsernameProperty))
        username = result.apn_info.at(kApnUsernameProperty);
      if (base::Contains(result.apn_info, kApnPasswordProperty))
        password = result.apn_info.at(kApnPasswordProperty);
    }
  }
  // apn_types is represented by a bit mask.
  uint32_t apn_types = 0;
  if (ApnList::IsDefaultApn(result.apn_info)) {
    apn_types |= static_cast<uint32_t>(
        Metrics::CellularApnType::kCellularApnTypeDefault);
  }
  if (ApnList::IsAttachApn(result.apn_info)) {
    apn_types |= static_cast<uint32_t>(CellularApnType::kCellularApnTypeIA);
  }
  if (ApnList::IsTetheringApn(result.apn_info)) {
    apn_types |= static_cast<uint32_t>(CellularApnType::kCellularApnTypeDun);
  }
  // Each APN type in connection_apn_types is represented by a digit, and the
  // order of the digits represent the connection order from first on the left,
  // to last on the right. For example, a value of 23 indicates that a Default
  // connection exists, and a new connection attempt is tried with a DUN APN.
  uint32_t connection_apn_types = 0;
  for (auto& apn_type : result.connection_apn_types) {
    uint digit = 0;
    switch (apn_type) {
      case DetailedCellularConnectionResult::APNType::kAttach:
        digit = 1;
        break;
      case DetailedCellularConnectionResult::APNType::kDefault:
        digit = 2;
        break;
      case DetailedCellularConnectionResult::APNType::kDUN:
        digit = 3;
        break;
    }
    connection_apn_types = connection_apn_types * 10 + digit;
  }

  if (device_metrics != nullptr) {
    base::TimeDelta elapsed_time;
    device_metrics->connect_timer->GetElapsedTime(&elapsed_time);
    connect_time = elapsed_time.InMilliseconds();
    device_metrics->scan_connect_timer->GetElapsedTime(&elapsed_time);
    scan_connect_time = elapsed_time.InMilliseconds();
  }

  SLOG(3) << __func__ << ": error:" << result.error << " uuid:" << result.uuid
          << " apn:" << apn_name << " apn_source:" << apn_source
          << " use_apn_revamp_ui: " << result.use_apn_revamp_ui
          << " apn_types: " << apn_types
          << " connection_apn_types: " << connection_apn_types
          << " ipv4:" << static_cast<int>(result.ipv4_config_method)
          << " ipv6:" << static_cast<int>(result.ipv6_config_method)
          << " home_mccmnc:" << result.home_mccmnc
          << " serving_mccmnc:" << result.serving_mccmnc
          << " roaming_state:" << result.roaming_state
          << " tech_used:" << result.tech_used
          << " iccid_length:" << result.iccid_length
          << " sim_type:" << result.sim_type << " gid1:" << result.gid1
          << " modem_state:" << result.modem_state
          << " connect_time:" << connect_time
          << " scan_connect_time:" << scan_connect_time
          << " detailed_error:" << result.detailed_error
          << " connection_attempt_type:"
          << static_cast<int>(result.connection_attempt_type)
          << " subscription_error_seen: " << result.subscription_error_seen;

  auto event =
      metrics::structured::events::cellular::CellularConnectionAttempt()
          .Setconnect_result(static_cast<int64_t>(connect_result))
          .Setapn_id(HashApn(result.uuid, apn_name, username, password))
          .Setipv4_config_method(static_cast<int>(result.ipv4_config_method))
          .Setipv6_config_method(static_cast<int>(result.ipv6_config_method))
          .Sethome_mccmnc(home)
          .Setserving_mccmnc(serving)
          .Setroaming_state(roaming)
          .Setapn_types(apn_types)
          .Setapn_source(static_cast<int64_t>(apn_source))
          .Settech_used(result.tech_used)
          .Seticcid_length(result.iccid_length)
          .Setsim_type(result.sim_type)
          .Setmodem_state(result.modem_state)
          .Setconnect_time(connect_time)
          .Setscan_connect_time(scan_connect_time)
          .Setdetailed_error(detailed_error_hash)
          .Setuse_apn_revamp_ui(result.use_apn_revamp_ui)
          .Setconnection_attempt_type(
              static_cast<int>(result.connection_attempt_type))
          .Setsubscription_error_seen(result.subscription_error_seen)
          .Setconnection_apn_types(connection_apn_types);

  std::optional<int64_t> gid1 = IntGid1(result.gid1);
  if (gid1.has_value()) {
    event.Setgid1(gid1.value());
  }

  event.Record();
}

void Metrics::NotifyCellularPowerOptimization(
    const CellularPowerOptimizationInfo& power_opt_info) {
  LOG(INFO) << __func__ << ": power optimization reason:  "
            << static_cast<int>(power_opt_info.reason);
  metrics::structured::events::cellular::PowerOptimization()
      .Setpower_state(static_cast<int>(power_opt_info.new_power_state))
      .Setreason(static_cast<int>(power_opt_info.reason))
      .Setsince_last_online_hours(power_opt_info.since_last_online_hours)
      .Record();
}

void Metrics::NotifyCellularEntitlementCheckResult(
    Metrics::CellularEntitlementCheck result) {
  SendEnumToUMA(kMetricCellularEntitlementCheck, result);
}

bool Metrics::SendEnumToUMA(const std::string& name, int sample, int max) {
  SLOG(5) << "Sending enum " << name << " with value " << sample << ".";
  return library_->SendEnumToUMA(name, sample, max);
}

bool Metrics::SendBoolToUMA(const std::string& name, bool b) {
  SLOG(5) << "Sending bool " << name << " with value " << b << ".";
  return library_->SendBoolToUMA(name, b);
}

bool Metrics::SendToUMA(
    const std::string& name, int sample, int min, int max, int num_buckets) {
  SLOG(5) << "Sending metric " << name << " with value " << sample << ".";
  return library_->SendToUMA(name, sample, min, max, num_buckets);
}

bool Metrics::SendSparseToUMA(const std::string& name, int sample) {
  SLOG(5) << "Sending sparse metric " << name << " with value " << sample
          << ".";
  return library_->SendSparseToUMA(name, sample);
}

void Metrics::ReportMilliseconds(const chromeos_metrics::TimerReporter& timer) {
  base::TimeDelta elapsed_time;
  if (timer.GetElapsedTime(&elapsed_time)) {
    SendToUMA(timer.histogram_name(), elapsed_time.InMilliseconds(),
              timer.min(), timer.max(), timer.num_buckets());
  }
}

void Metrics::NotifyConnectionDiagnosticsIssue(const std::string& issue) {
  ConnectionDiagnosticsIssue issue_enum;
  if (issue == ConnectionDiagnostics::kIssueIPCollision) {
    issue_enum = kConnectionDiagnosticsIssueIPCollision;
  } else if (issue == ConnectionDiagnostics::kIssueRouting) {
    issue_enum = kConnectionDiagnosticsIssueRouting;
  } else if (issue == ConnectionDiagnostics::kIssueHTTP) {
    issue_enum = kConnectionDiagnosticsIssueHTTP;
  } else if (issue == ConnectionDiagnostics::kIssueDNSServerMisconfig) {
    issue_enum = kConnectionDiagnosticsIssueDNSServerMisconfig;
  } else if (issue == ConnectionDiagnostics::kIssueDNSServerNoResponse) {
    issue_enum = kConnectionDiagnosticsIssueDNSServerNoResponse;
  } else if (issue == ConnectionDiagnostics::kIssueNoDNSServersConfigured) {
    issue_enum = kConnectionDiagnosticsIssueNoDNSServersConfigured;
  } else if (issue == ConnectionDiagnostics::kIssueDNSServersInvalid) {
    issue_enum = kConnectionDiagnosticsIssueDNSServersInvalid;
  } else if (issue == ConnectionDiagnostics::kIssueNone) {
    issue_enum = kConnectionDiagnosticsIssueNone;
  } else if (issue == ConnectionDiagnostics::kIssueGatewayUpstream) {
    issue_enum = kConnectionDiagnosticsIssueGatewayUpstream;
  } else if (issue == ConnectionDiagnostics::kIssueGatewayNotResponding) {
    issue_enum = kConnectionDiagnosticsIssueGatewayNotResponding;
  } else if (issue == ConnectionDiagnostics::kIssueServerNotResponding) {
    issue_enum = kConnectionDiagnosticsIssueServerNotResponding;
  } else if (issue == ConnectionDiagnostics::kIssueGatewayArpFailed) {
    issue_enum = kConnectionDiagnosticsIssueGatewayArpFailed;
  } else if (issue == ConnectionDiagnostics::kIssueServerArpFailed) {
    issue_enum = kConnectionDiagnosticsIssueServerArpFailed;
  } else if (issue == ConnectionDiagnostics::kIssueInternalError) {
    issue_enum = kConnectionDiagnosticsIssueInternalError;
  } else if (issue == ConnectionDiagnostics::kIssueGatewayNoNeighborEntry) {
    issue_enum = kConnectionDiagnosticsIssueGatewayNoNeighborEntry;
  } else if (issue == ConnectionDiagnostics::kIssueServerNoNeighborEntry) {
    issue_enum = kConnectionDiagnosticsIssueServerNoNeighborEntry;
  } else if (issue ==
             ConnectionDiagnostics::kIssueGatewayNeighborEntryNotConnected) {
    issue_enum = kConnectionDiagnosticsIssueGatewayNeighborEntryNotConnected;
  } else if (issue ==
             ConnectionDiagnostics::kIssueServerNeighborEntryNotConnected) {
    issue_enum = kConnectionDiagnosticsIssueServerNeighborEntryNotConnected;
  } else {
    LOG(ERROR) << __func__ << ": Invalid issue: " << issue;
    return;
  }

  SendEnumToUMA(kMetricConnectionDiagnosticsIssue, issue_enum);
}

void Metrics::NotifyHS20Support(bool hs20_supported, int hs20_version_number) {
  if (!hs20_supported) {
    SendEnumToUMA(kMetricHS20Support, kHS20Unsupported);
    return;
  }
  int hotspot_version = kHS20VersionInvalid;
  switch (hs20_version_number) {
    // Valid values.
    case 1:
      hotspot_version = kHS20Version1;
      break;
    case 2:
      hotspot_version = kHS20Version2;
      break;
    case 3:
      hotspot_version = kHS20Version3;
      break;
    // Invalid values.
    default:
      break;
  }
  SendEnumToUMA(kMetricHS20Support, hotspot_version);
}

void Metrics::NotifyMBOSupport(bool mbo_support) {
  SendBoolToUMA(kMetricMBOSupport, mbo_support);
}

void Metrics::NotifyStreamClassificationSupport(bool scs_supported,
                                                bool mscs_supported) {
  int sc_support = kWiFiApSCUnsupported;
  if (scs_supported && mscs_supported) {
    sc_support = kWiFiApSCBoth;
  } else if (scs_supported) {
    sc_support = kWiFiApSCS;
  } else if (mscs_supported) {
    sc_support = kWiFiApMSCS;
  }
  SendEnumToUMA(kMetricApSCSupport, sc_support);
}

void Metrics::NotifyAlternateEDCASupport(bool alternate_edca_supported) {
  SendBoolToUMA(kMetricApAlternateEDCASupport, alternate_edca_supported);
}

void Metrics::NotifyWiFiConnectionUnreliable() {
  // Report the results of the metric associated with tracking the
  // time between rekey and unreliable connection,
  // TimeFromRekeyToFailureSeconds.
  auto& rekey_timer = time_between_rekey_and_connection_failure_timer_;
  base::TimeDelta elapsed;
  int seconds;
  if (!rekey_timer->HasStarted()) {
    return;
  }
  rekey_timer->GetElapsedTime(&elapsed);
  seconds = elapsed.InSeconds();
  if (seconds < kMetricTimeFromRekeyToFailureSeconds.max) {
    // We only send the metric if the unreliable connection happens shortly
    // after the rekey started on the same BSSID.
    LOG(INFO) << "Connection became unreliable shortly after rekey, "
              << "seconds between rekey and connection failure: " << seconds;
    SendToUMA(kMetricTimeFromRekeyToFailureSeconds, seconds);
  }
  rekey_timer->Reset();
}

void Metrics::NotifyBSSIDChanged() {
  // Rekey cancelled/BSSID changed, so we reset the timer
  // associated with the metric for TimeFromRekeyToFailureSeconds.
  time_between_rekey_and_connection_failure_timer_->Reset();
}

void Metrics::NotifyRekeyStart() {
  // Start the timer associated with the metric tracking time
  // between rekey and unreliable connection,
  // TimeFromRekeyToFailureSeconds.
  auto& rekey_timer = time_between_rekey_and_connection_failure_timer_;
  if (!rekey_timer->HasStarted()) {
    rekey_timer->Start();
  }
}

void Metrics::NotifyWiFiBadPassphrase(bool ever_connected, bool user_initiate) {
  WiFiBadPassphraseServiceType type;
  if (user_initiate) {
    type = ever_connected ? kUserInitiatedConnectedBefore
                          : kUserInitiatedNeverConnected;
  } else {
    type = ever_connected ? kNonUserInitiatedConnectedBefore
                          : kNonUserInitiatedNeverConnected;
  }
  SendEnumToUMA(kMetricWiFiBadPassphraseServiceType, type);
}

void Metrics::NotifyWiFiAdapterStateChanged(bool enabled,
                                            const WiFiAdapterInfo& info) {
  metrics::structured::events::wi_fi_chipset::WiFiChipsetInfo()
      .SetEventVersion(kWiFiStructuredMetricsVersion)
      .SetVendorId(info.vendor_id)
      .SetProductId(info.product_id)
      .SetSubsystemId(info.subsystem_id)
      .Record();

  bool adapter_supported = WiFiMetricsUtils::CanReportAdapterInfo(info);
  if (enabled) {
    // Monitor through UMA how often adapters are not in the allowlist.
    WiFiAdapterInAllowlist allowed =
        adapter_supported ? kInAVL : kNotInAllowlist;
    SendEnumToUMA(kMetricAdapterInfoAllowlisted, allowed);
  }

  int v_id = adapter_supported ? info.vendor_id
                               : Metrics::kWiFiStructuredMetricsErrorValue;
  int p_id = adapter_supported ? info.product_id
                               : Metrics::kWiFiStructuredMetricsErrorValue;
  int s_id = adapter_supported ? info.subsystem_id
                               : Metrics::kWiFiStructuredMetricsErrorValue;
  metrics::structured::events::wi_fi::WiFiAdapterStateChanged()
      .SetBootId(WiFiMetricsUtils::GetBootId())
      .SetSystemTime(GetMicroSecondsMonotonic())
      .SetEventVersion(kWiFiStructuredMetricsVersion)
      .SetAdapterState(enabled)
      .SetVendorId(v_id)
      .SetProductId(p_id)
      .SetSubsystemId(s_id)
      .Record();
}

void Metrics::NotifyWiFiConnectionAttempt(const WiFiConnectionAttemptInfo& info,
                                          uint64_t session_tag) {
  metrics::structured::events::wi_fi_ap::WiFiAPInfo()
      .SetEventVersion(kWiFiStructuredMetricsVersion)
      .SetAPOUI(info.ap_oui)
      .Record();

  int oui = shill::WiFiMetricsUtils::CanReportOUI(info.ap_oui) ? info.ap_oui
                                                               : 0xFFFFFFFF;
  // Do NOT modify the verbosity of the Session Tag log without a privacy
  // review.
  SLOG(WiFiService::kSessionTagMinimumLogVerbosity)
      << __func__ << ": Session Tag 0x" << PseudonymizeTag(session_tag);
  metrics::structured::events::wi_fi::WiFiConnectionAttempt()
      .SetBootId(WiFiMetricsUtils::GetBootId())
      .SetSystemTime(GetMicroSecondsMonotonic())
      .SetEventVersion(kWiFiStructuredMetricsVersion)
      .SetSessionTag(session_tag)
      .SetAttemptType(info.type)
      .SetAPPhyMode(info.mode)
      .SetAPSecurityMode(info.security)
      .SetAPSecurityEAPInnerProtocol(info.eap_inner)
      .SetAPSecurityEAPOuterProtocol(info.eap_outer)
      .SetAPBand(info.band)
      .SetAPChannel(info.channel)
      .SetRSSI(info.rssi)
      .SetSSID(info.ssid)
      .SetSSIDProvisioningMode(info.provisioning_mode)
      .SetSSIDHidden(info.ssid_hidden)
      .SetBSSID(info.bssid)
      .SetAPOUI(oui)
      .SetAP_80211krv_NLSSupport(
          info.ap_features.krv_info.neighbor_list_supported)
      .SetAP_80211krv_OTA_FTSupport(info.ap_features.krv_info.ota_ft_supported)
      .SetAP_80211krv_OTDS_FTSupport(
          info.ap_features.krv_info.otds_ft_supported)
      .SetAP_80211krv_DMSSupport(info.ap_features.krv_info.dms_supported)
      .SetAP_80211krv_BSSMaxIdleSupport(
          info.ap_features.krv_info.bss_max_idle_period_supported)
      .SetAP_80211krv_BSSTMSupport(
          info.ap_features.krv_info.bss_transition_supported)
      .SetAP_HS20Support(info.ap_features.hs20_info.supported)
      .SetAP_HS20Version(info.ap_features.hs20_info.version)
      .SetAP_MBOSupport(info.ap_features.mbo_supported)
      .Record();
}

void Metrics::NotifyWiFiConnectionAttemptResult(
    Metrics::NetworkServiceError result_code, uint64_t session_tag) {
  // Do NOT modify the verbosity of the Session Tag log without a privacy
  // review.
  SLOG(WiFiService::kSessionTagMinimumLogVerbosity)
      << __func__ << ": Session Tag 0x" << PseudonymizeTag(session_tag);
  SLOG(2) << __func__ << ": ResultCode " << result_code;
  metrics::structured::events::wi_fi::WiFiConnectionAttemptResult()
      .SetBootId(WiFiMetricsUtils::GetBootId())
      .SetSystemTime(GetMicroSecondsMonotonic())
      .SetEventVersion(kWiFiStructuredMetricsVersion)
      .SetSessionTag(session_tag)
      .SetResultCode(result_code)
      .Record();
}

void Metrics::NotifyWiFiDisconnection(WiFiDisconnectionType type,
                                      IEEE_80211::WiFiReasonCode reason,
                                      uint64_t session_tag) {
  // Do NOT modify the verbosity of the Session Tag log without a privacy
  // review.
  SLOG(WiFiService::kSessionTagMinimumLogVerbosity)
      << __func__ << ": Session Tag 0x" << PseudonymizeTag(session_tag);
  SLOG(2) << __func__ << ": Type " << type << " Reason " << reason;
  metrics::structured::events::wi_fi::WiFiConnectionEnd()
      .SetBootId(WiFiMetricsUtils::GetBootId())
      .SetSystemTime(GetMicroSecondsMonotonic())
      .SetEventVersion(kWiFiStructuredMetricsVersion)
      .SetSessionTag(session_tag)
      .SetDisconnectionType(type)
      .SetDisconnectionReasonCode(reason)
      .Record();
}

void Metrics::NotifyWiFiLinkQualityTrigger(WiFiLinkQualityTrigger trigger,
                                           uint64_t session_tag) {
  // Do NOT modify the verbosity of the Session Tag log without a privacy
  // review.
  SLOG(WiFiService::kSessionTagMinimumLogVerbosity)
      << __func__ << ": Session Tag 0x" << PseudonymizeTag(session_tag);
  SLOG(2) << __func__ << ": Trigger " << trigger;
  metrics::structured::events::wi_fi::WiFiLinkQualityTrigger()
      .SetBootId(WiFiMetricsUtils::GetBootId())
      .SetSystemTime(GetMicroSecondsMonotonic())
      .SetEventVersion(kWiFiStructuredMetricsVersion)
      .SetSessionTag(session_tag)
      .SetType(trigger)
      .Record();
}

void Metrics::NotifyWiFiLinkQualityReport(const WiFiLinkQualityReport& report,
                                          uint64_t session_tag) {
  // Do NOT modify the verbosity of the Session Tag log without a privacy
  // review.
  SLOG(WiFiService::kSessionTagMinimumLogVerbosity)
      << __func__ << ": Session Tag 0x" << PseudonymizeTag(session_tag);

  // Note: RXChannelWidth and TXChannelWidth have identical values but we have
  // 2 separate fields for backward compatibility reasons.
  metrics::structured::events::wi_fi::WiFiLinkQualityReport sm_report =
      metrics::structured::events::wi_fi::WiFiLinkQualityReport();
  sm_report.SetBootId(WiFiMetricsUtils::GetBootId())
      .SetSystemTime(GetMicroSecondsMonotonic())
      .SetEventVersion(kWiFiStructuredMetricsVersion)
      .SetSessionTag(session_tag)
      .SetRXPackets(report.rx.packets)
      .SetRXBytes(report.rx.bytes)
      .SetTXPackets(report.tx.packets)
      .SetTXBytes(report.tx.bytes)
      .SetTXRetries(report.tx_retries)
      .SetTXFailures(report.tx_failures)
      .SetRXDrops(report.rx_drops)
      .SetChain0Signal(report.chain0_signal)
      .SetChain0SignalAvg(report.chain0_signal_avg)
      .SetChain1Signal(report.chain1_signal)
      .SetChain1SignalAvg(report.chain1_signal_avg)
      .SetBeaconSignalAvg(report.beacon_signal_avg)
      .SetBeaconsReceived(report.beacons_received)
      .SetBeaconsLost(report.beacons_lost)
      .SetExpectedThroughput(report.expected_throughput)
      .SetRXRate(report.rx.bitrate)
      .SetRXMCS(report.rx.mcs)
      .SetRXChannelWidth(report.width)
      .SetRXMode(report.rx.mode)
      .SetRXGuardInterval(report.rx.gi)
      .SetRXNSS(report.rx.nss)
      .SetRXDCM(report.rx.dcm)
      .SetTXRate(report.tx.bitrate)
      .SetTXMCS(report.tx.mcs)
      .SetTXChannelWidth(report.width)
      .SetTXMode(report.tx.mode)
      .SetTXGuardInterval(report.tx.gi)
      .SetTXNSS(report.tx.nss)
      .SetTXDCM(report.tx.dcm)
      .SetFCSErrors(report.fcs_errors)
      .SetRXMPDUS(report.rx_mpdus)
      .SetInactiveTime(report.inactive_time)
      .SetNoise(report.noise)
      .SetAckSignalAverage(report.ack_signal_avg)
      .SetLastAckSignal(report.last_ack_signal)
      .SetSignal(report.signal)
      .SetSignalAverage(report.signal_avg);
#if !defined(DISABLE_FLOSS)
  sm_report.SetBTEnabled(report.bt_enabled)
      .SetBTStack(report.bt_stack)
      .SetBTHFP(report.bt_hfp)
      .SetBTA2DP(report.bt_a2dp)
      .SetBTActivelyScanning(report.bt_active_scanning);
#else   // DISABLE_FLOSS
  sm_report.SetBTStack(kBTStackBlueZ);
#endif  // DISABLE_FLOSS

  sm_report.Record();
}

// static
int Metrics::GetRegulatoryDomainValue(std::string country_code) {
  // Convert country code to upper case before checking validity.
  country_code = base::ToUpperASCII(country_code);

  // Check if alpha2 attribute is a valid ISO / IEC 3166 alpha2 country code.
  // "00", "99", "98" and "97" are special codes defined in
  // linux/include/net/regulatory.h.
  // According to https://www.iso.org/glossary-for-iso-3166.html, a subdivision
  // code is based on the two-letter code element from ISO 3166-1 followed by
  // a separator and up to three alphanumeric characters. ath10k uses '#' as
  // the separator, as reported in b/217761687. New separators may be added
  // if shown in reports. Currently, these country codes are valid:
  // 1. Special code: 00, 99, 98, 97
  // 2. Two-letter alpha 2 code, such as "US", "FR"
  // 3. Subdivision code, two-letter alpha 2 code + '#' + up to three
  // alphanumeric characters, such as "US#001", "JM#001", while the characters
  // after '#' are ignored

  if (country_code == "00") {
    return kRegDom00;
  } else if (country_code == "97") {
    return kRegDom97;
  } else if (country_code == "98") {
    return kRegDom98;
  } else if (country_code == "99") {
    return kRegDom99;
  } else if (country_code.length() < 2 || !std::isupper(country_code[0]) ||
             !std::isupper(country_code[1]) || country_code.length() > 6 ||
             (country_code.length() > 2 && country_code[2] != '#')) {
    return kCountryCodeInvalid;
  } else {
    // Calculate corresponding country code value for UMA histogram.
    return ((static_cast<int>(country_code[0]) - static_cast<int>('A')) * 26) +
           (static_cast<int>(country_code[1]) - static_cast<int>('A') + 2);
  }
}

Metrics::DeviceMetrics* Metrics::GetDeviceMetrics(int interface_index) const {
  DeviceMetricsLookupMap::const_iterator it =
      devices_metrics_.find(interface_index);
  if (it == devices_metrics_.end()) {
    SLOG(2) << __func__ << ": device " << interface_index << " not found";
    return nullptr;
  }
  return it->second.get();
}

std::string Metrics::PseudonymizeTag(uint64_t tag) {
  if (pseudo_tag_salt_.empty()) {
    return "INVALID SALT";
  }
  if (IsInvalidTag(tag)) {
    return "INVALID TAG";
  }
  uint8_t hash[kPseudoTagHashLen];
  std::string salted_tag =
      base::StrCat({pseudo_tag_salt_, base::NumberToString(tag)});
  crypto::SHA256HashString(salted_tag, hash, std::size(hash));
  return base::HexEncode(base::span<uint8_t>(hash, std::size(hash)));
}

void Metrics::SetLibraryForTesting(MetricsLibraryInterface* library) {
  library_ = library;
}

}  // namespace shill
