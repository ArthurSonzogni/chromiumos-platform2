// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/wake_on_wifi.h"

#include <errno.h>
#include <linux/nl80211.h>
#include <stdio.h>
#include <sys/timerfd.h>

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/cancelable_callback.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/strings/string_number_conversions.h>

#include <chromeos/dbus/service_constants.h>

#include "shill/error.h"
#include "shill/event_dispatcher.h"
#include "shill/ip_address_store.h"
#include "shill/logging.h"
#include "shill/metrics.h"
#include "shill/net/event_history.h"
#include "shill/net/netlink_manager.h"
#include "shill/net/nl80211_message.h"
#include "shill/property_accessor.h"
#include "shill/wifi/wifi.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kWiFi;
static std::string ObjectID(const WakeOnWiFi* w) {
  return "(wake_on_wifi)";
}
}  // namespace Logging

const char WakeOnWiFi::kWakeOnIPAddressPatternsNotSupported[] =
    "Wake on IP address patterns not supported by this WiFi device";
const char WakeOnWiFi::kWakeOnPatternsNotSupported[] =
    "Wake on patterns not supported by this WiFi device";
const char WakeOnWiFi::kMaxWakeOnPatternsReached[] =
    "Max number of patterns already registered";
const char WakeOnWiFi::kWakeOnWiFiNotAllowed[] = "Wake on WiFi not allowed";
const int WakeOnWiFi::kVerifyWakeOnWiFiSettingsDelayMilliseconds = 300;
const int WakeOnWiFi::kMaxSetWakeOnPacketRetries = 2;
const int WakeOnWiFi::kMetricsReportingFrequencySeconds = 600;
const uint32_t WakeOnWiFi::kDefaultWakeToScanPeriodSeconds = 900;
const uint32_t WakeOnWiFi::kDefaultNetDetectScanPeriodSeconds = 120;
const uint32_t WakeOnWiFi::kImmediateDHCPLeaseRenewalThresholdSeconds = 60;
// We tolerate no more than 3 dark resumes per minute and 10 dark resumes per
// 10 minutes  before we disable wake on WiFi on the NIC.
const int WakeOnWiFi::kDarkResumeFrequencySamplingPeriodShortMinutes = 1;
const int WakeOnWiFi::kDarkResumeFrequencySamplingPeriodLongMinutes = 10;
const int WakeOnWiFi::kMaxDarkResumesPerPeriodShort = 3;
const int WakeOnWiFi::kMaxDarkResumesPerPeriodLong = 10;
// If a connection is not established during dark resume, give up and prepare
// the system to wake on SSID 1 second before suspending again.
// TODO(samueltan): link this to
// Manager::kTerminationActionsTimeoutMilliseconds rather than hard-coding
// this value.
int64_t WakeOnWiFi::DarkResumeActionsTimeoutMilliseconds = 18500;
// Scanning 1 frequency takes ~100ms, so retrying 5 times on 8 frequencies will
// take about 4 seconds, which is how long a full scan typically takes.
const int WakeOnWiFi::kMaxFreqsForDarkResumeScanRetries = 8;
const int WakeOnWiFi::kMaxDarkResumeScanRetries = 5;

WakeOnWiFi::WakeOnWiFi(NetlinkManager* netlink_manager,
                       EventDispatcher* dispatcher,
                       Metrics* metrics,
                       const std::string& mac_address,
                       RecordWakeReasonCallback record_wake_reason_callback)
    : dispatcher_(dispatcher),
      netlink_manager_(netlink_manager),
      metrics_(metrics),
      report_metrics_callback_(
          base::Bind(&WakeOnWiFi::ReportMetrics, base::Unretained(this))),
      num_set_wake_on_packet_retries_(0),
      wake_on_wifi_max_patterns_(0),
      wake_on_wifi_max_ssids_(0),
      wiphy_index_(0),
      wiphy_index_received_(false),
      wake_on_wifi_allowed_(false),
      // Wake on WiFi features disabled by default at run-time for boards that
      // support wake on WiFi. Rely on Chrome to enable appropriate features via
      // DBus.
      wake_on_wifi_features_enabled_(kWakeOnWiFiFeaturesEnabledNone),
      in_dark_resume_(false),
      wake_to_scan_period_seconds_(kDefaultWakeToScanPeriodSeconds),
      net_detect_scan_period_seconds_(kDefaultNetDetectScanPeriodSeconds),
      last_wake_reason_(kWakeTriggerUnsupported),
      force_wake_to_scan_timer_(false),
      dark_resume_scan_retries_left_(0),
      connected_before_suspend_(false),
      mac_address_(mac_address),
      min_pattern_len_(0),
      record_wake_reason_callback_(record_wake_reason_callback),
      weak_ptr_factory_(this) {
  netlink_handler_ = base::Bind(&WakeOnWiFi::OnWakeupReasonReceived,
                                weak_ptr_factory_.GetWeakPtr());
  netlink_manager_->AddBroadcastHandler(netlink_handler_);
  dhcp_lease_renewal_timer_ = timers::SimpleAlarmTimer::Create();
  wake_to_scan_timer_ = timers::SimpleAlarmTimer::Create();
}

WakeOnWiFi::~WakeOnWiFi() {
  netlink_manager_->RemoveBroadcastHandler(netlink_handler_);
}

void WakeOnWiFi::InitPropertyStore(PropertyStore* store) {
  store->RegisterDerivedBool(kWakeOnWiFiAllowedProperty,
                             BoolAccessor(new CustomAccessor<WakeOnWiFi, bool>(
                                 this, &WakeOnWiFi::GetWakeOnWiFiAllowed,
                                 &WakeOnWiFi::SetWakeOnWiFiAllowed)));
  store->RegisterDerivedString(
      kWakeOnWiFiFeaturesEnabledProperty,
      StringAccessor(new CustomAccessor<WakeOnWiFi, std::string>(
          this, &WakeOnWiFi::GetWakeOnWiFiFeaturesEnabled,
          &WakeOnWiFi::SetWakeOnWiFiFeaturesEnabled)));
  store->RegisterUint32(kWakeToScanPeriodSecondsProperty,
                        &wake_to_scan_period_seconds_);
  store->RegisterUint32(kNetDetectScanPeriodSecondsProperty,
                        &net_detect_scan_period_seconds_);
  store->RegisterBool(kForceWakeToScanTimerProperty,
                      &force_wake_to_scan_timer_);
  store->RegisterDerivedString(
      kLastWakeReasonProperty,
      StringAccessor(new CustomAccessor<WakeOnWiFi, std::string>(
          this, &WakeOnWiFi::GetLastWakeReason, nullptr)));
}

void WakeOnWiFi::StartMetricsTimer() {
  dispatcher_->PostDelayedTask(FROM_HERE, report_metrics_callback_.callback(),
                               kMetricsReportingFrequencySeconds * 1000);
}

void WakeOnWiFi::Start() {
  StartMetricsTimer();
}

bool WakeOnWiFi::GetWakeOnWiFiAllowed(Error* /*error*/) {
  return wake_on_wifi_allowed_;
}

bool WakeOnWiFi::SetWakeOnWiFiAllowed(const bool& allowed, Error* error) {
  if (wake_on_wifi_allowed_ == allowed) {
    return false;
  }
  // Disable all WiFi features first.
  if (!allowed) {
    SetWakeOnWiFiFeaturesEnabled(kWakeOnWiFiFeaturesEnabledNone, error);
  }
  wake_on_wifi_allowed_ = allowed;
  return true;
}

std::string WakeOnWiFi::GetWakeOnWiFiFeaturesEnabled(Error* error) {
  return wake_on_wifi_features_enabled_;
}

bool WakeOnWiFi::SetWakeOnWiFiFeaturesEnabled(const std::string& enabled,
                                              Error* error) {
  if (!wake_on_wifi_allowed_) {
    error->Populate(Error::kNotSupported, kWakeOnWiFiNotAllowed);
    SLOG(this, 7) << __func__ << ": " << kWakeOnWiFiNotAllowed;
    return false;
  }
  if (wake_on_wifi_features_enabled_ == enabled) {
    return false;
  }
  if (enabled != kWakeOnWiFiFeaturesEnabledPacket &&
      enabled != kWakeOnWiFiFeaturesEnabledDarkConnect &&
      enabled != kWakeOnWiFiFeaturesEnabledPacketDarkConnect &&
      enabled != kWakeOnWiFiFeaturesEnabledNone) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Invalid Wake on WiFi feature");
    return false;
  }
  wake_on_wifi_features_enabled_ = enabled;
  return true;
}

std::string WakeOnWiFi::GetLastWakeReason(Error* /*error*/) {
  switch (last_wake_reason_) {
    case kWakeTriggerDisconnect:
      return kWakeOnWiFiReasonDisconnect;
    case kWakeTriggerSSID:
      return kWakeOnWiFiReasonSSID;
    case kWakeTriggerPattern:
      return kWakeOnWiFiReasonPattern;
    default:
      return kWakeOnWiFiReasonUnknown;
  }
}

void WakeOnWiFi::RunAndResetSuspendActionsDoneCallback(const Error& error) {
  if (!suspend_actions_done_callback_.is_null()) {
    suspend_actions_done_callback_.Run(error);
    suspend_actions_done_callback_.Reset();
  }
}

// static
bool WakeOnWiFi::ByteStringPairIsLessThan(
    const std::pair<ByteString, ByteString>& lhs,
    const std::pair<ByteString, ByteString>& rhs) {
  // Treat the first value of the pair as the key.
  return ByteString::IsLessThan(lhs.first, rhs.first);
}

// static
void WakeOnWiFi::SetMask(
    ByteString* mask,
    const std::vector<LengthOffset>& patternlen_offset_pair,
    uint32_t expected_pat_len_bits) {
  // Round up number of bytes required for the mask.
  int result_mask_len = (expected_pat_len_bits + 8 - 1) / 8;
  std::vector<unsigned char> result_mask(result_mask_len, 0);
  // Set mask bits from offset to (pattern_len - 1)
  int mask_index;
  for (const auto& current_pair : patternlen_offset_pair) {
    for (uint32_t curr_mask_bit = current_pair.offset;
         curr_mask_bit < current_pair.length; ++curr_mask_bit) {
      mask_index = curr_mask_bit / 8;
      CHECK_LT(mask_index, result_mask_len);
      result_mask[mask_index] |= 1 << (curr_mask_bit % 8);
    }
  }
  mask->Clear();
  mask->Append(ByteString(result_mask));
}

// static
bool WakeOnWiFi::CreateIPAddressPatternAndMask(const IPAddress& ip_addr,
                                               uint32_t min_pattern_len,
                                               ByteString* pattern,
                                               ByteString* mask) {
  if (ip_addr.family() == IPAddress::kFamilyIPv4) {
    WakeOnWiFi::CreateIPV4PatternAndMask(ip_addr, min_pattern_len, pattern,
                                         mask);
    return true;
  } else if (ip_addr.family() == IPAddress::kFamilyIPv6) {
    WakeOnWiFi::CreateIPV6PatternAndMask(ip_addr, pattern, mask,
                                         min_pattern_len);
    return true;
  } else {
    LOG(ERROR) << "Unrecognized IP Address type.";
    return false;
  }
}

// static
bool WakeOnWiFi::ConvertIPProtoStrtoEnum(
    const std::vector<std::string>& ip_proto_strs,
    std::set<uint8_t>* ip_proto_enums,
    Error* error) {
  for (const auto& ip_proto : ip_proto_strs) {
    if (ip_proto == kWakeOnIP) {
      ip_proto_enums->insert(IPPROTO_IP);
    } else if (ip_proto == kWakeOnICMP) {
      ip_proto_enums->insert(IPPROTO_ICMP);
    } else if (ip_proto == kWakeOnIGMP) {
      ip_proto_enums->insert(IPPROTO_IGMP);
    } else if (ip_proto == kWakeOnIPIP) {
      ip_proto_enums->insert(IPPROTO_IPIP);
    } else if (ip_proto == kWakeOnTCP) {
      ip_proto_enums->insert(IPPROTO_TCP);
    } else if (ip_proto == kWakeOnUDP) {
      ip_proto_enums->insert(IPPROTO_UDP);
    } else if (ip_proto == kWakeOnIDP) {
      ip_proto_enums->insert(IPPROTO_IDP);
    } else {
      Error::PopulateAndLog(
          FROM_HERE, error, Error::kInvalidArguments,
          "Wake on Packet of type " + ip_proto + " not supported");
      return false;
    }
  }
  return true;
}

// static
std::string WakeOnWiFi::ConvertIPProtoEnumtoStr(uint8_t ip_proto_enum) {
  switch (ip_proto_enum) {
    case IPPROTO_IP:
      return kWakeOnIP;
    case IPPROTO_ICMP:
      return kWakeOnICMP;
    case IPPROTO_IGMP:
      return kWakeOnIGMP;
    case IPPROTO_IPIP:
      return kWakeOnIPIP;
    case IPPROTO_TCP:
      return kWakeOnTCP;
    case IPPROTO_UDP:
      return kWakeOnUDP;
    case IPPROTO_IDP:
      return kWakeOnIDP;
    default:
      return "";
  }
}

// static
void WakeOnWiFi::CreateIPV4PatternAndMask(const IPAddress& ip_addr,
                                          uint32_t min_pattern_len,
                                          ByteString* pattern,
                                          ByteString* mask) {
  struct {
    struct ethhdr eth_hdr;
    struct iphdr ipv4_hdr;
  } __attribute__((__packed__)) pattern_bytes;
  memset(&pattern_bytes, 0, sizeof(pattern_bytes));
  CHECK_EQ(sizeof(pattern_bytes.ipv4_hdr.saddr), ip_addr.GetLength());
  memcpy(&pattern_bytes.ipv4_hdr.saddr, ip_addr.GetConstData(),
         ip_addr.GetLength());
  int src_ip_offset =
      reinterpret_cast<unsigned char*>(&pattern_bytes.ipv4_hdr.saddr) -
      reinterpret_cast<unsigned char*>(&pattern_bytes);
  uint32_t pattern_len = src_ip_offset + ip_addr.GetLength();
  // If the length of the final pattern is less than min pattern length,
  // fill the rest with zeros.
  uint32_t expected_pattern_len = std::max(min_pattern_len, pattern_len);
  pattern->Clear();
  pattern->Append(
      ByteString(reinterpret_cast<const unsigned char*>(&pattern_bytes),
                 expected_pattern_len));
  std::vector<LengthOffset> patternlen_offset_pair;
  patternlen_offset_pair.emplace_back(pattern_len, src_ip_offset);
  WakeOnWiFi::SetMask(mask, patternlen_offset_pair, expected_pattern_len);
}

// static
void WakeOnWiFi::CreateIPV6PatternAndMask(const IPAddress& ip_addr,
                                          ByteString* pattern,
                                          ByteString* mask,
                                          uint32_t min_pattern_len) {
  struct {
    struct ethhdr eth_hdr;
    struct ip6_hdr ipv6_hdr;
  } __attribute__((__packed__)) pattern_bytes;
  memset(&pattern_bytes, 0, sizeof(pattern_bytes));
  CHECK_EQ(sizeof(pattern_bytes.ipv6_hdr.ip6_src), ip_addr.GetLength());
  memcpy(&pattern_bytes.ipv6_hdr.ip6_src, ip_addr.GetConstData(),
         ip_addr.GetLength());
  int src_ip_offset =
      reinterpret_cast<unsigned char*>(&pattern_bytes.ipv6_hdr.ip6_src) -
      reinterpret_cast<unsigned char*>(&pattern_bytes);
  uint32_t pattern_len = src_ip_offset + ip_addr.GetLength();
  uint32_t expected_pattern_len = std::max(min_pattern_len, pattern_len);
  pattern->Clear();
  pattern->Append(
      ByteString(reinterpret_cast<const unsigned char*>(&pattern_bytes),
                 expected_pattern_len));
  std::vector<LengthOffset> patternlen_offset_pair;
  patternlen_offset_pair.emplace_back(pattern_len, src_ip_offset);
  WakeOnWiFi::SetMask(mask, patternlen_offset_pair, expected_pattern_len);
}

// static
void WakeOnWiFi::CreatePacketTypePatternAndMaskforIPV4(
    const std::string& mac_address,
    uint32_t min_pattern_len,
    uint8_t ip_protocol,
    ByteString* pattern,
    ByteString* mask) {
  struct Pattern {
    struct ethhdr eth_hdr;
    struct iphdr ipv4_hdr;
  } __attribute__((__packed__)) pattern_bytes;
  std::vector<LengthOffset> patternlen_offset_pair;
  memset(&pattern_bytes, 0, sizeof(pattern_bytes));
  std::vector<uint8_t> address_bytes;
  static_assert(std::is_standard_layout<Pattern>::value,
                "Pattern must be a standard layout type");
  CHECK(base::HexStringToBytes(mac_address, &address_bytes));
  CHECK_EQ(sizeof(pattern_bytes.eth_hdr.h_dest), address_bytes.size());
  std::copy(address_bytes.begin(), address_bytes.end(),
            pattern_bytes.eth_hdr.h_dest);
  uint32_t dst_hardware_offset = offsetof(Pattern, eth_hdr.h_dest);
  uint32_t pattern_len = dst_hardware_offset + address_bytes.size();
  patternlen_offset_pair.emplace_back(pattern_len, dst_hardware_offset);
  uint32_t eth_protocol_offset = offsetof(Pattern, eth_hdr.h_proto);
  pattern_bytes.eth_hdr.h_proto = htons(ETH_P_IP);
  uint32_t eth_protocol_len =
      eth_protocol_offset + sizeof(pattern_bytes.eth_hdr.h_proto);
  patternlen_offset_pair.emplace_back(eth_protocol_len, eth_protocol_offset);
  pattern_len = std::max(pattern_len, eth_protocol_len);
  pattern_bytes.ipv4_hdr.protocol = ip_protocol;
  uint32_t ip_protocol_offset = offsetof(Pattern, ipv4_hdr.protocol);
  uint32_t ip_protocol_pattern_len =
      ip_protocol_offset + sizeof(pattern_bytes.ipv4_hdr.protocol);
  patternlen_offset_pair.emplace_back(ip_protocol_pattern_len,
                                      ip_protocol_offset);
  pattern_len = std::max(pattern_len, ip_protocol_pattern_len);
  uint32_t expected_pattern_len = std::max(min_pattern_len, pattern_len);
  pattern->Clear();
  pattern->Append(
      ByteString(reinterpret_cast<const unsigned char*>(&pattern_bytes),
                 expected_pattern_len));
  WakeOnWiFi::SetMask(mask, patternlen_offset_pair, expected_pattern_len);
}

// static
void WakeOnWiFi::CreatePacketTypePatternAndMaskforIPV6(
    const std::string& mac_address,
    uint32_t min_pattern_len,
    uint8_t ip_protocol,
    ByteString* pattern,
    ByteString* mask) {
  struct Pattern {
    struct ethhdr eth_hdr;
    struct ip6_hdr ipv6_hdr;
  } __attribute__((__packed__)) pattern_bytes;
  std::vector<LengthOffset> patternlen_offset_pair;
  static_assert(std::is_standard_layout<Pattern>::value,
                "Pattern must be a standard layout type");
  memset(&pattern_bytes, 0, sizeof(pattern_bytes));
  std::vector<uint8_t> address_bytes;
  CHECK(base::HexStringToBytes(mac_address, &address_bytes));
  CHECK_EQ(sizeof(pattern_bytes.eth_hdr.h_dest), address_bytes.size());
  std::copy(address_bytes.begin(), address_bytes.end(),
            pattern_bytes.eth_hdr.h_dest);
  uint32_t dst_hardware_offset = offsetof(Pattern, eth_hdr.h_dest);
  uint32_t pattern_len = dst_hardware_offset + address_bytes.size();
  patternlen_offset_pair.emplace_back(pattern_len, dst_hardware_offset);
  uint32_t eth_protocol_offset = offsetof(Pattern, eth_hdr.h_proto);
  pattern_bytes.eth_hdr.h_proto = htons(ETH_P_IPV6);
  uint32_t eth_protocol_len =
      eth_protocol_offset + sizeof(pattern_bytes.eth_hdr.h_proto);
  patternlen_offset_pair.emplace_back(eth_protocol_len, eth_protocol_offset);
  pattern_len = std::max(pattern_len, eth_protocol_len);
  pattern_bytes.ipv6_hdr.ip6_ctlun.ip6_un1.ip6_un1_nxt = ip_protocol;
  uint32_t ip_protocol_offset =
      offsetof(Pattern, ipv6_hdr.ip6_ctlun.ip6_un1.ip6_un1_nxt);
  uint32_t ip_protocol_pattern_len =
      ip_protocol_offset +
      sizeof(pattern_bytes.ipv6_hdr.ip6_ctlun.ip6_un1.ip6_un1_nxt);
  patternlen_offset_pair.emplace_back(ip_protocol_pattern_len,
                                      ip_protocol_offset);
  pattern_len = std::max(pattern_len, ip_protocol_pattern_len);
  uint32_t expected_pattern_len = std::max(min_pattern_len, pattern_len);
  pattern->Clear();
  pattern->Append(
      ByteString(reinterpret_cast<const unsigned char*>(&pattern_bytes),
                 expected_pattern_len));
  WakeOnWiFi::SetMask(mask, patternlen_offset_pair, expected_pattern_len);
}

// static
bool WakeOnWiFi::ConfigureWiphyIndex(Nl80211Message* msg, int32_t index) {
  if (!msg->attributes()->CreateU32Attribute(NL80211_ATTR_WIPHY,
                                             "WIPHY index")) {
    return false;
  }
  if (!msg->attributes()->SetU32AttributeValue(NL80211_ATTR_WIPHY, index)) {
    return false;
  }
  return true;
}

// static
bool WakeOnWiFi::ConfigureDisableWakeOnWiFiMessage(
    SetWakeOnPacketConnMessage* msg, uint32_t wiphy_index, Error* error) {
  if (!ConfigureWiphyIndex(msg, wiphy_index)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          "Failed to configure Wiphy index.");
    return false;
  }
  return true;
}

// static
bool WakeOnWiFi::ConfigureSetWakeOnWiFiSettingsMessage(
    SetWakeOnPacketConnMessage* msg,
    const std::set<WakeOnWiFiTrigger>& trigs,
    const IPAddressStore& addrs,
    uint32_t wiphy_index,
    const std::set<uint8_t>& wake_on_packet_types,
    const std::string& mac_address,
    uint32_t pattern_min_len,
    uint32_t net_detect_scan_period_seconds,
    const std::vector<ByteString>& allowed_ssids,
    Error* error) {
  if (trigs.empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "No triggers to configure.");
    return false;
  }
  if (base::Contains(trigs, kWakeTriggerPattern) && addrs.Empty() &&
      wake_on_packet_types.empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "No IP addresses to configure.");
    return false;
  }
  if (!ConfigureWiphyIndex(msg, wiphy_index)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          "Failed to configure Wiphy index.");
    return false;
  }
  if (!msg->attributes()->CreateNestedAttribute(NL80211_ATTR_WOWLAN_TRIGGERS,
                                                "WoWLAN Triggers")) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          "Could not create nested attribute "
                          "NL80211_ATTR_WOWLAN_TRIGGERS");
    return false;
  }
  if (!msg->attributes()->SetNestedAttributeHasAValue(
          NL80211_ATTR_WOWLAN_TRIGGERS)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          "Could not set nested attribute "
                          "NL80211_ATTR_WOWLAN_TRIGGERS");
    return false;
  }

  AttributeListRefPtr triggers;
  if (!msg->attributes()->GetNestedAttributeList(NL80211_ATTR_WOWLAN_TRIGGERS,
                                                 &triggers)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          "Could not get nested attribute list "
                          "NL80211_ATTR_WOWLAN_TRIGGERS");
    return false;
  }
  // Add triggers.
  for (WakeOnWiFiTrigger t : trigs) {
    switch (t) {
      case kWakeTriggerDisconnect: {
        if (!triggers->CreateFlagAttribute(NL80211_WOWLAN_TRIG_DISCONNECT,
                                           "Wake on Disconnect")) {
          LOG(ERROR) << __func__
                     << "Could not create flag attribute "
                        "NL80211_WOWLAN_TRIG_DISCONNECT";
          return false;
        }
        if (!triggers->SetFlagAttributeValue(NL80211_WOWLAN_TRIG_DISCONNECT,
                                             true)) {
          LOG(ERROR) << __func__
                     << "Could not set flag attribute "
                        "NL80211_WOWLAN_TRIG_DISCONNECT";
          return false;
        }
        break;
      }
      case kWakeTriggerPattern: {
        if (!triggers->CreateNestedAttribute(NL80211_WOWLAN_TRIG_PKT_PATTERN,
                                             "Pattern trigger")) {
          Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                                "Could not create nested attribute "
                                "NL80211_WOWLAN_TRIG_PKT_PATTERN");
          return false;
        }
        if (!triggers->SetNestedAttributeHasAValue(
                NL80211_WOWLAN_TRIG_PKT_PATTERN)) {
          Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                                "Could not set nested attribute "
                                "NL80211_WOWLAN_TRIG_PKT_PATTERN");
          return false;
        }
        AttributeListRefPtr patterns;
        if (!triggers->GetNestedAttributeList(NL80211_WOWLAN_TRIG_PKT_PATTERN,
                                              &patterns)) {
          Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                                "Could not get nested attribute list "
                                "NL80211_WOWLAN_TRIG_PKT_PATTERN");
          return false;
        }
        uint8_t patnum = 1;
        ByteString pattern;
        ByteString mask;
        for (const IPAddress& addr : addrs.GetIPAddresses()) {
          CreateIPAddressPatternAndMask(addr, pattern_min_len, &pattern, &mask);
          if (!CreateSingleAttribute(pattern, mask, patterns, patnum++,
                                     error)) {
            return false;
          }
        }
        if (!wake_on_packet_types.empty()) {
          for (auto packet_type : wake_on_packet_types) {
            CreatePacketTypePatternAndMaskforIPV4(mac_address, pattern_min_len,
                                                  packet_type, &pattern, &mask);
            if (!CreateSingleAttribute(pattern, mask, patterns, patnum++,
                                       error)) {
              return false;
            }
            CreatePacketTypePatternAndMaskforIPV6(mac_address, pattern_min_len,
                                                  packet_type, &pattern, &mask);
            if (!CreateSingleAttribute(pattern, mask, patterns, patnum++,
                                       error)) {
              return false;
            }
          }
        }
        break;
      }
      case kWakeTriggerSSID: {
        if (!triggers->CreateNestedAttribute(NL80211_WOWLAN_TRIG_NET_DETECT,
                                             "Wake on SSID trigger")) {
          Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                                "Could not create nested attribute "
                                "NL80211_WOWLAN_TRIG_NET_DETECT");
          return false;
        }
        if (!triggers->SetNestedAttributeHasAValue(
                NL80211_WOWLAN_TRIG_NET_DETECT)) {
          Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                                "Could not set nested attribute "
                                "NL80211_WOWLAN_TRIG_NET_DETECT");
          return false;
        }
        AttributeListRefPtr scan_attributes;
        if (!triggers->GetNestedAttributeList(NL80211_WOWLAN_TRIG_NET_DETECT,
                                              &scan_attributes)) {
          Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                                "Could not get nested attribute list "
                                "NL80211_WOWLAN_TRIG_NET_DETECT");
          return false;
        }
        if (!scan_attributes->CreateU32Attribute(
                NL80211_ATTR_SCHED_SCAN_INTERVAL,
                "NL80211_ATTR_SCHED_SCAN_INTERVAL")) {
          Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                                "Could not get create U32 attribute "
                                "NL80211_ATTR_SCHED_SCAN_INTERVAL");
          return false;
        }
        if (!scan_attributes->SetU32AttributeValue(
                NL80211_ATTR_SCHED_SCAN_INTERVAL,
                net_detect_scan_period_seconds * 1000)) {
          Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                                "Could not get set U32 attribute "
                                "NL80211_ATTR_SCHED_SCAN_INTERVAL");
          return false;
        }
        if (!scan_attributes->CreateNestedAttribute(
                NL80211_ATTR_SCHED_SCAN_MATCH,
                "NL80211_ATTR_SCHED_SCAN_MATCH")) {
          Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                                "Could not create nested attribute list "
                                "NL80211_ATTR_SCHED_SCAN_MATCH");
          return false;
        }
        if (!scan_attributes->SetNestedAttributeHasAValue(
                NL80211_ATTR_SCHED_SCAN_MATCH)) {
          Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                                "Could not set nested attribute "
                                "NL80211_ATTR_SCAN_SSIDS");
          return false;
        }
        AttributeListRefPtr ssids;
        if (!scan_attributes->GetNestedAttributeList(
                NL80211_ATTR_SCHED_SCAN_MATCH, &ssids)) {
          Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                                "Could not get nested attribute list "
                                "NL80211_ATTR_SCHED_SCAN_MATCH");
          return false;
        }
        int ssid_num = 0;
        for (const ByteString& ssid_bytes : allowed_ssids) {
          if (!ssids->CreateNestedAttribute(
                  ssid_num, "NL80211_ATTR_SCHED_SCAN_MATCH_SINGLE")) {
            Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                                  "Could not create nested attribute list "
                                  "NL80211_ATTR_SCHED_SCAN_MATCH_SINGLE");
            return false;
          }
          if (!ssids->SetNestedAttributeHasAValue(ssid_num)) {
            Error::PopulateAndLog(
                FROM_HERE, error, Error::kOperationFailed,
                "Could not set value for nested attribute list "
                "NL80211_ATTR_SCHED_SCAN_MATCH_SINGLE");
            return false;
          }
          AttributeListRefPtr single_ssid;
          if (!ssids->GetNestedAttributeList(ssid_num, &single_ssid)) {
            Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                                  "Could not get nested attribute list "
                                  "NL80211_ATTR_SCHED_SCAN_MATCH_SINGLE");
            return false;
          }
          if (!single_ssid->CreateRawAttribute(
                  NL80211_SCHED_SCAN_MATCH_ATTR_SSID,
                  "NL80211_SCHED_SCAN_MATCH_ATTR_SSID")) {
            Error::PopulateAndLog(
                FROM_HERE, error, Error::kOperationFailed,
                "Could not create NL80211_SCHED_SCAN_MATCH_ATTR_SSID");
            return false;
          }
          if (!single_ssid->SetRawAttributeValue(
                  NL80211_SCHED_SCAN_MATCH_ATTR_SSID, ssid_bytes)) {
            Error::PopulateAndLog(
                FROM_HERE, error, Error::kOperationFailed,
                "Could not set NL80211_SCHED_SCAN_MATCH_ATTR_SSID");
            return false;
          }
          ++ssid_num;
        }
        break;
      }
      default: {
        LOG(ERROR) << __func__ << ": Unrecognized trigger";
        return false;
      }
    }
  }
  return true;
}

// static
bool WakeOnWiFi::CreateSingleAttribute(const ByteString& pattern,
                                       const ByteString& mask,
                                       AttributeListRefPtr patterns,
                                       uint8_t patnum,
                                       Error* error) {
  if (!patterns->CreateNestedAttribute(patnum, "Pattern info")) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          "Could not create nested attribute "
                          "patnum for SetWakeOnPacketConnMessage.");
    return false;
  }
  if (!patterns->SetNestedAttributeHasAValue(patnum)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          "Could not set nested attribute "
                          "patnum for SetWakeOnPacketConnMessage.");
    return false;
  }
  AttributeListRefPtr pattern_info;
  if (!patterns->GetNestedAttributeList(patnum, &pattern_info)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          "Could not get nested attribute list "
                          "patnum for SetWakeOnPacketConnMessage.");
    return false;
  }
  // Add mask.
  if (!pattern_info->CreateRawAttribute(NL80211_PKTPAT_MASK, "Mask")) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          "Could not add attribute NL80211_PKTPAT_MASK to "
                          "pattern_info.");
    return false;
  }
  if (!pattern_info->SetRawAttributeValue(NL80211_PKTPAT_MASK, mask)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          "Could not set attribute NL80211_PKTPAT_MASK in "
                          "pattern_info.");
    return false;
  }
  // Add pattern.
  if (!pattern_info->CreateRawAttribute(NL80211_PKTPAT_PATTERN, "Pattern")) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          "Could not add attribute NL80211_PKTPAT_PATTERN to "
                          "pattern_info.");
    return false;
  }
  if (!pattern_info->SetRawAttributeValue(NL80211_PKTPAT_PATTERN, pattern)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          "Could not set attribute NL80211_PKTPAT_PATTERN in "
                          "pattern_info.");
    return false;
  }
  // Add offset.
  if (!pattern_info->CreateU32Attribute(NL80211_PKTPAT_OFFSET, "Offset")) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          "Could not add attribute NL80211_PKTPAT_OFFSET to "
                          "pattern_info.");
    return false;
  }
  if (!pattern_info->SetU32AttributeValue(NL80211_PKTPAT_OFFSET, 0)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          "Could not set attribute NL80211_PKTPAT_OFFSET in "
                          "pattern_info.");
    return false;
  }
  return true;
}

// static
bool WakeOnWiFi::ConfigureGetWakeOnWiFiSettingsMessage(
    GetWakeOnPacketConnMessage* msg, uint32_t wiphy_index, Error* error) {
  if (!ConfigureWiphyIndex(msg, wiphy_index)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          "Failed to configure Wiphy index.");
    return false;
  }
  return true;
}

// static
bool WakeOnWiFi::WakeOnWiFiSettingsMatch(
    const Nl80211Message& msg,
    const std::set<WakeOnWiFiTrigger>& trigs,
    const IPAddressStore& addrs,
    uint32_t net_detect_scan_period_seconds,
    const std::set<uint8_t>& wake_on_packet_types,
    const std::string& mac_address,
    uint32_t min_pattern_len,
    const std::vector<ByteString>& allowed_ssids) {
  if (msg.command() != NL80211_CMD_GET_WOWLAN &&
      msg.command() != NL80211_CMD_SET_WOWLAN) {
    LOG(ERROR) << __func__ << ": "
               << "Invalid message command";
    return false;
  }
  AttributeListConstRefPtr triggers;
  if (!msg.const_attributes()->ConstGetNestedAttributeList(
          NL80211_ATTR_WOWLAN_TRIGGERS, &triggers)) {
    // No triggers in the returned message, which is valid iff we expect there
    // to be no triggers programmed into the NIC.
    return trigs.empty();
  }
  // If we find a trigger in |msg| that we do not have a corresponding flag
  // for in |trigs|, we have a mismatch.
  bool unused_flag;
  AttributeListConstRefPtr unused_list;
  if (triggers->GetFlagAttributeValue(NL80211_WOWLAN_TRIG_DISCONNECT,
                                      &unused_flag) &&
      !base::Contains(trigs, kWakeTriggerDisconnect)) {
    SLOG(WiFi, nullptr, 3)
        << __func__ << "Wake on disconnect trigger not expected but found";
    return false;
  }
  if (triggers->ConstGetNestedAttributeList(NL80211_WOWLAN_TRIG_PKT_PATTERN,
                                            &unused_list) &&
      !base::Contains(trigs, kWakeTriggerPattern)) {
    SLOG(WiFi, nullptr, 3) << __func__
                           << "Wake on pattern trigger not expected but found";
    return false;
  }
  if (triggers->ConstGetNestedAttributeList(NL80211_WOWLAN_TRIG_NET_DETECT,
                                            &unused_list) &&
      !base::Contains(trigs, kWakeTriggerSSID)) {
    SLOG(WiFi, nullptr, 3) << __func__
                           << "Wake on SSID trigger not expected but found";
    return false;
  }
  // Check that each expected trigger is present in |msg| with matching
  // setting values.
  for (WakeOnWiFiTrigger t : trigs) {
    switch (t) {
      case kWakeTriggerDisconnect: {
        bool wake_on_disconnect;
        if (!triggers->GetFlagAttributeValue(NL80211_WOWLAN_TRIG_DISCONNECT,
                                             &wake_on_disconnect)) {
          LOG(ERROR) << __func__ << ": "
                     << "Could not get the flag NL80211_WOWLAN_TRIG_DISCONNECT";
          return false;
        }
        if (!wake_on_disconnect) {
          SLOG(WiFi, nullptr, 3)
              << __func__ << "Wake on disconnect flag not set.";
          return false;
        }
        break;
      }
      case kWakeTriggerPattern: {
        // Create pattern and masks that we expect to find in |msg|.
        std::set<std::pair<ByteString, ByteString>,
                 decltype(&ByteStringPairIsLessThan)>
            expected_patt_mask_pairs(ByteStringPairIsLessThan);
        ByteString temp_pattern;
        ByteString temp_mask;
        for (const IPAddress& addr : addrs.GetIPAddresses()) {
          CreateIPAddressPatternAndMask(addr, min_pattern_len, &temp_pattern,
                                        &temp_mask);
          expected_patt_mask_pairs.emplace(temp_pattern, temp_mask);
        }
        if (!wake_on_packet_types.empty()) {
          for (auto packet_type : wake_on_packet_types) {
            CreatePacketTypePatternAndMaskforIPV4(mac_address, min_pattern_len,
                                                  packet_type, &temp_pattern,
                                                  &temp_mask);
            expected_patt_mask_pairs.emplace(temp_pattern, temp_mask);
            CreatePacketTypePatternAndMaskforIPV6(mac_address, min_pattern_len,
                                                  packet_type, &temp_pattern,
                                                  &temp_mask);
            expected_patt_mask_pairs.emplace(temp_pattern, temp_mask);
          }
        }
        // Check these expected pattern and masks against those actually
        // contained in |msg|.
        AttributeListConstRefPtr patterns;
        if (!triggers->ConstGetNestedAttributeList(
                NL80211_WOWLAN_TRIG_PKT_PATTERN, &patterns)) {
          LOG(ERROR) << __func__ << ": "
                     << "Could not get nested attribute list "
                        "NL80211_WOWLAN_TRIG_PKT_PATTERN";
          return false;
        }
        bool pattern_mismatch_found = false;
        size_t pattern_num_mismatch = expected_patt_mask_pairs.size();
        int pattern_index;
        AttributeIdIterator pattern_iter(*patterns);
        AttributeListConstRefPtr pattern_info;
        ByteString returned_mask;
        ByteString returned_pattern;
        while (!pattern_iter.AtEnd()) {
          returned_mask.Clear();
          returned_pattern.Clear();
          pattern_index = pattern_iter.GetId();
          if (!patterns->ConstGetNestedAttributeList(pattern_index,
                                                     &pattern_info)) {
            LOG(ERROR) << __func__ << ": "
                       << "Could not get nested pattern attribute list #"
                       << pattern_index;
            return false;
          }
          if (!pattern_info->GetRawAttributeValue(NL80211_PKTPAT_MASK,
                                                  &returned_mask)) {
            LOG(ERROR) << __func__ << ": "
                       << "Could not get attribute NL80211_PKTPAT_MASK";
            return false;
          }
          if (!pattern_info->GetRawAttributeValue(NL80211_PKTPAT_PATTERN,
                                                  &returned_pattern)) {
            LOG(ERROR) << __func__ << ": "
                       << "Could not get attribute NL80211_PKTPAT_PATTERN";
            return false;
          }
          if (!base::Contains(
                  expected_patt_mask_pairs,
                  std::make_pair(returned_pattern, returned_mask))) {
            pattern_mismatch_found = true;
            break;
          } else {
            --pattern_num_mismatch;
          }
          pattern_iter.Advance();
        }
        if (pattern_mismatch_found || pattern_num_mismatch) {
          SLOG(WiFi, nullptr, 3)
              << __func__ << "Wake on pattern pattern/mask mismatch";
          return false;
        }
        break;
      }
      case kWakeTriggerSSID: {
        std::set<ByteString, decltype(&ByteString::IsLessThan)> expected_ssids(
            allowed_ssids.begin(), allowed_ssids.end(), ByteString::IsLessThan);
        AttributeListConstRefPtr scan_attributes;
        if (!triggers->ConstGetNestedAttributeList(
                NL80211_WOWLAN_TRIG_NET_DETECT, &scan_attributes)) {
          LOG(ERROR) << __func__ << ": "
                     << "Could not get nested attribute list "
                        "NL80211_WOWLAN_TRIG_NET_DETECT";
          return false;
        }
        uint32_t interval;
        if (!scan_attributes->GetU32AttributeValue(
                NL80211_ATTR_SCHED_SCAN_INTERVAL, &interval)) {
          LOG(ERROR) << __func__ << ": "
                     << "Could not get set U32 attribute "
                        "NL80211_ATTR_SCHED_SCAN_INTERVAL";
          return false;
        }
        if (interval != net_detect_scan_period_seconds * 1000) {
          SLOG(WiFi, nullptr, 3)
              << __func__ << "Net Detect scan period mismatch";
          return false;
        }
        AttributeListConstRefPtr ssids;
        if (!scan_attributes->ConstGetNestedAttributeList(
                NL80211_ATTR_SCHED_SCAN_MATCH, &ssids)) {
          LOG(ERROR) << __func__ << ": "
                     << "Could not get nested attribute list "
                        "NL80211_ATTR_SCHED_SCAN_MATCH";
          return false;
        }
        bool ssid_mismatch_found = false;
        size_t ssid_num_mismatch = expected_ssids.size();
        AttributeIdIterator ssid_iter(*ssids);
        AttributeListConstRefPtr single_ssid;
        ByteString ssid;
        int ssid_index;
        while (!ssid_iter.AtEnd()) {
          ssid.Clear();
          ssid_index = ssid_iter.GetId();
          if (!ssids->ConstGetNestedAttributeList(ssid_index, &single_ssid)) {
            LOG(ERROR) << __func__ << ": "
                       << "Could not get nested ssid attribute list #"
                       << ssid_index;
            return false;
          }
          if (!single_ssid->GetRawAttributeValue(
                  NL80211_SCHED_SCAN_MATCH_ATTR_SSID, &ssid)) {
            LOG(ERROR) << __func__ << ": "
                       << "Could not get attribute "
                          "NL80211_SCHED_SCAN_MATCH_ATTR_SSID";
            return false;
          }
          if (!base::Contains(expected_ssids, ssid)) {
            ssid_mismatch_found = true;
            break;
          } else {
            --ssid_num_mismatch;
          }
          ssid_iter.Advance();
        }
        if (ssid_mismatch_found || ssid_num_mismatch) {
          SLOG(WiFi, nullptr, 3) << __func__ << "Net Detect SSID mismatch";
          return false;
        }
        break;
      }
      default: {
        LOG(ERROR) << __func__ << ": Unrecognized trigger";
        return false;
      }
    }
  }
  return true;
}

void WakeOnWiFi::AddWakeOnPacketConnection(const std::string& ip_endpoint,
                                           Error* error) {
  if (!base::Contains(wake_on_wifi_triggers_supported_, kWakeTriggerPattern)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported,
                          kWakeOnIPAddressPatternsNotSupported);
    return;
  }
  IPAddress ip_addr(ip_endpoint);
  if (!ip_addr.IsValid()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Invalid ip_address " + ip_endpoint);
    return;
  }

  if (wake_on_packet_connections_.Count() + wake_on_packet_types_.size() >=
      wake_on_wifi_max_patterns_) {
    Error::PopulateAndLog(
        FROM_HERE, error, Error::kOperationFailed,
        "Max number of IP address patterns already registered");
    return;
  }
  wake_on_packet_connections_.AddUnique(ip_addr);
}

void WakeOnWiFi::AddWakeOnPacketOfTypes(
    const std::vector<std::string>& packet_types, Error* error) {
  std::set<uint8_t> ip_proto_enums;
  if (!base::Contains(wake_on_wifi_triggers_supported_, kWakeTriggerPattern)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported,
                          kWakeOnPatternsNotSupported);
    return;
  }
  // We should add 1 pattern for IPv4 and one for IPv6 for each packet type.
  int num_patterns_for_packet_types = packet_types.size() * 2;
  if (wake_on_packet_connections_.Count() + num_patterns_for_packet_types >=
      wake_on_wifi_max_patterns_) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          kMaxWakeOnPatternsReached);
    return;
  }
  if (!ConvertIPProtoStrtoEnum(packet_types, &ip_proto_enums, error))
    return;
  wake_on_packet_types_.insert(ip_proto_enums.begin(), ip_proto_enums.end());
}

void WakeOnWiFi::RemoveWakeOnPacketConnection(const std::string& ip_endpoint,
                                              Error* error) {
  if (!base::Contains(wake_on_wifi_triggers_supported_, kWakeTriggerPattern)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported,
                          kWakeOnIPAddressPatternsNotSupported);
    return;
  }
  IPAddress ip_addr(ip_endpoint);
  if (!ip_addr.IsValid()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Invalid ip_address " + ip_endpoint);
    return;
  }
  if (!wake_on_packet_connections_.Contains(ip_addr)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kNotFound,
                          "No such IP address match registered to wake device");
    return;
  }
  wake_on_packet_connections_.Remove(ip_addr);
}

void WakeOnWiFi::RemoveWakeOnPacketOfTypes(
    const std::vector<std::string>& packet_types, Error* error) {
  if (!base::Contains(wake_on_wifi_triggers_supported_, kWakeTriggerPattern)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported,
                          kWakeOnPatternsNotSupported);
    return;
  }

  std::set<uint8_t> ip_proto_enums;
  if (!ConvertIPProtoStrtoEnum(packet_types, &ip_proto_enums, error))
    return;

  for (auto ip_proto_enum : ip_proto_enums) {
    if (base::Contains(wake_on_packet_types_, ip_proto_enum))
      wake_on_packet_types_.erase(ip_proto_enum);
  }
}

void WakeOnWiFi::RemoveAllWakeOnPacketConnections(Error* error) {
  if (!base::Contains(wake_on_wifi_triggers_supported_, kWakeTriggerPattern)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported,
                          kWakeOnIPAddressPatternsNotSupported);
    return;
  }
  wake_on_packet_connections_.Clear();
  wake_on_packet_types_.clear();
}

void WakeOnWiFi::OnWakeOnWiFiSettingsErrorResponse(
    NetlinkManager::AuxilliaryMessageType type,
    const NetlinkMessage* raw_message) {
  Error error(Error::kOperationFailed);
  switch (type) {
    case NetlinkManager::kErrorFromKernel:
      if (!raw_message) {
        error.Populate(Error::kOperationFailed, "Unknown error from kernel");
        break;
      }
      if (raw_message->message_type() == ErrorAckMessage::GetMessageType()) {
        const ErrorAckMessage* error_ack_message =
            static_cast<const ErrorAckMessage*>(raw_message);
        if (error_ack_message->error() == EOPNOTSUPP) {
          error.Populate(Error::kNotSupported);
        }
      }
      break;

    case NetlinkManager::kUnexpectedResponseType:
      error.Populate(Error::kNotRegistered,
                     "Message not handled by regular message handler:");
      break;

    case NetlinkManager::kTimeoutWaitingForResponse:
      // CMD_SET_WOWLAN messages do not receive responses, so this error type
      // is received when NetlinkManager times out the message handler. Return
      // immediately rather than run the done callback since this event does
      // not signify the completion of suspend actions.
      return;
      break;

    default:
      error.Populate(
          Error::kOperationFailed,
          "Unexpected auxilliary message type: " + std::to_string(type));
      break;
  }
  RunAndResetSuspendActionsDoneCallback(error);
}

// static
void WakeOnWiFi::OnSetWakeOnPacketConnectionResponse(
    const Nl80211Message& nl80211_message) {
  // NOP because kernel does not send a response to NL80211_CMD_SET_WOWLAN
  // requests.
}

void WakeOnWiFi::RequestWakeOnPacketSettings() {
  SLOG(this, 3) << __func__;
  Error e;
  GetWakeOnPacketConnMessage get_wowlan_msg;
  CHECK(wiphy_index_received_);
  if (!ConfigureGetWakeOnWiFiSettingsMessage(&get_wowlan_msg, wiphy_index_,
                                             &e)) {
    LOG(ERROR) << e.message();
    return;
  }
  netlink_manager_->SendNl80211Message(
      &get_wowlan_msg,
      base::Bind(&WakeOnWiFi::VerifyWakeOnWiFiSettings,
                 weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&NetlinkManager::OnAckDoNothing),
      base::Bind(&NetlinkManager::OnNetlinkMessageError));
}

void WakeOnWiFi::VerifyWakeOnWiFiSettings(
    const Nl80211Message& nl80211_message) {
  SLOG(this, 3) << __func__;
  if (WakeOnWiFiSettingsMatch(
          nl80211_message, wake_on_wifi_triggers_, wake_on_packet_connections_,
          net_detect_scan_period_seconds_, wake_on_packet_types_, mac_address_,
          min_pattern_len_, wake_on_allowed_ssids_)) {
    SLOG(this, 2) << __func__ << ": "
                  << "Wake on WiFi settings successfully verified";
    metrics_->NotifyVerifyWakeOnWiFiSettingsResult(
        Metrics::kVerifyWakeOnWiFiSettingsResultSuccess);
    RunAndResetSuspendActionsDoneCallback(Error(Error::kSuccess));
  } else {
    LOG(ERROR) << __func__
               << " failed: discrepancy between wake-on-packet "
                  "settings on NIC and those in local data "
                  "structure detected";
    metrics_->NotifyVerifyWakeOnWiFiSettingsResult(
        Metrics::kVerifyWakeOnWiFiSettingsResultFailure);
    RetrySetWakeOnPacketConnections();
  }
}

void WakeOnWiFi::ApplyWakeOnWiFiSettings() {
  SLOG(this, 3) << __func__;
  if (!wiphy_index_received_) {
    LOG(ERROR) << "Interface index not yet received";
    return;
  }
  if (wake_on_wifi_triggers_.empty()) {
    SLOG(this, 1) << "No triggers to be programmed, so disable wake on WiFi";
    DisableWakeOnWiFi();
    return;
  }
  Error error;
  SetWakeOnPacketConnMessage set_wowlan_msg;
  if (!ConfigureSetWakeOnWiFiSettingsMessage(
          &set_wowlan_msg, wake_on_wifi_triggers_, wake_on_packet_connections_,
          wiphy_index_, wake_on_packet_types_, mac_address_, min_pattern_len_,
          net_detect_scan_period_seconds_, wake_on_allowed_ssids_, &error)) {
    LOG(ERROR) << error.message();
    RunAndResetSuspendActionsDoneCallback(
        Error(Error::kOperationFailed, error.message()));
    return;
  }
  if (!netlink_manager_->SendNl80211Message(
          &set_wowlan_msg,
          base::Bind(&WakeOnWiFi::OnSetWakeOnPacketConnectionResponse),
          base::Bind(&NetlinkManager::OnAckDoNothing),
          base::Bind(&WakeOnWiFi::OnWakeOnWiFiSettingsErrorResponse,
                     weak_ptr_factory_.GetWeakPtr()))) {
    RunAndResetSuspendActionsDoneCallback(
        Error(Error::kOperationFailed, "SendNl80211Message failed"));
    return;
  }

  verify_wake_on_packet_settings_callback_.Reset(
      base::Bind(&WakeOnWiFi::RequestWakeOnPacketSettings,
                 weak_ptr_factory_.GetWeakPtr()));
  dispatcher_->PostDelayedTask(
      FROM_HERE, verify_wake_on_packet_settings_callback_.callback(),
      kVerifyWakeOnWiFiSettingsDelayMilliseconds);
}

void WakeOnWiFi::DisableWakeOnWiFi() {
  SLOG(this, 3) << __func__;
  Error error;
  SetWakeOnPacketConnMessage disable_wowlan_msg;
  CHECK(wiphy_index_received_);
  if (!ConfigureDisableWakeOnWiFiMessage(&disable_wowlan_msg, wiphy_index_,
                                         &error)) {
    LOG(ERROR) << error.message();
    RunAndResetSuspendActionsDoneCallback(
        Error(Error::kOperationFailed, error.message()));
    return;
  }
  wake_on_wifi_triggers_.clear();
  if (!netlink_manager_->SendNl80211Message(
          &disable_wowlan_msg,
          base::Bind(&WakeOnWiFi::OnSetWakeOnPacketConnectionResponse),
          base::Bind(&NetlinkManager::OnAckDoNothing),
          base::Bind(&WakeOnWiFi::OnWakeOnWiFiSettingsErrorResponse,
                     weak_ptr_factory_.GetWeakPtr()))) {
    RunAndResetSuspendActionsDoneCallback(
        Error(Error::kOperationFailed, "SendNl80211Message failed"));
    return;
  }

  verify_wake_on_packet_settings_callback_.Reset(
      base::Bind(&WakeOnWiFi::RequestWakeOnPacketSettings,
                 weak_ptr_factory_.GetWeakPtr()));
  dispatcher_->PostDelayedTask(
      FROM_HERE, verify_wake_on_packet_settings_callback_.callback(),
      kVerifyWakeOnWiFiSettingsDelayMilliseconds);
}

void WakeOnWiFi::RetrySetWakeOnPacketConnections() {
  SLOG(this, 3) << __func__;
  if (num_set_wake_on_packet_retries_ < kMaxSetWakeOnPacketRetries) {
    ApplyWakeOnWiFiSettings();
    ++num_set_wake_on_packet_retries_;
  } else {
    SLOG(this, 3) << __func__ << ": max retry attempts reached";
    num_set_wake_on_packet_retries_ = 0;
    RunAndResetSuspendActionsDoneCallback(Error(Error::kOperationFailed));
  }
}

bool WakeOnWiFi::WakeOnWiFiDisabled() {
  return wake_on_wifi_features_enabled_ == kWakeOnWiFiFeaturesEnabledNone;
}

bool WakeOnWiFi::WakeOnWiFiPacketEnabledAndSupported() {
  if (wake_on_wifi_features_enabled_ == kWakeOnWiFiFeaturesEnabledNone ||
      wake_on_wifi_features_enabled_ == kWakeOnWiFiFeaturesEnabledDarkConnect) {
    return false;
  }
  if (!base::Contains(wake_on_wifi_triggers_supported_, kWakeTriggerPattern)) {
    return false;
  }
  return true;
}

bool WakeOnWiFi::WakeOnWiFiDarkConnectEnabledAndSupported() {
  if (wake_on_wifi_features_enabled_ == kWakeOnWiFiFeaturesEnabledNone ||
      wake_on_wifi_features_enabled_ == kWakeOnWiFiFeaturesEnabledPacket) {
    return false;
  }
  if (!base::Contains(wake_on_wifi_triggers_supported_,
                      kWakeTriggerDisconnect) ||
      !base::Contains(wake_on_wifi_triggers_supported_, kWakeTriggerSSID)) {
    return false;
  }
  return true;
}

void WakeOnWiFi::ReportMetrics() {
  Metrics::WakeOnWiFiFeaturesEnabledState reported_state;
  if (wake_on_wifi_features_enabled_ == kWakeOnWiFiFeaturesEnabledNone) {
    reported_state = Metrics::kWakeOnWiFiFeaturesEnabledStateNone;
  } else if (wake_on_wifi_features_enabled_ ==
             kWakeOnWiFiFeaturesEnabledPacket) {
    reported_state = Metrics::kWakeOnWiFiFeaturesEnabledStatePacket;
  } else if (wake_on_wifi_features_enabled_ ==
             kWakeOnWiFiFeaturesEnabledDarkConnect) {
    reported_state = Metrics::kWakeOnWiFiFeaturesEnabledStateDarkConnect;
  } else if (wake_on_wifi_features_enabled_ ==
             kWakeOnWiFiFeaturesEnabledPacketDarkConnect) {
    reported_state = Metrics::kWakeOnWiFiFeaturesEnabledStatePacketDarkConnect;
  } else {
    LOG(ERROR) << __func__ << ": "
               << "Invalid wake on WiFi features state";
    return;
  }
  metrics_->NotifyWakeOnWiFiFeaturesEnabledState(reported_state);
  StartMetricsTimer();
}

void WakeOnWiFi::ParseWakeOnWiFiCapabilities(
    const Nl80211Message& nl80211_message) {
  // Verify NL80211_CMD_NEW_WIPHY.
  if (nl80211_message.command() != NewWiphyMessage::kCommand) {
    LOG(ERROR) << "Received unexpected command:" << nl80211_message.command();
    return;
  }
  AttributeListConstRefPtr triggers_supported;
  if (nl80211_message.const_attributes()->ConstGetNestedAttributeList(
          NL80211_ATTR_WOWLAN_TRIGGERS_SUPPORTED, &triggers_supported)) {
    bool disconnect_supported = false;
    if (triggers_supported->GetFlagAttributeValue(
            NL80211_WOWLAN_TRIG_DISCONNECT, &disconnect_supported)) {
      if (disconnect_supported) {
        wake_on_wifi_triggers_supported_.insert(
            WakeOnWiFi::kWakeTriggerDisconnect);
        SLOG(this, 7) << "Waking on disconnect supported by this WiFi device";
      }
    }
    ByteString pattern_data;
    if (triggers_supported->GetRawAttributeValue(
            NL80211_WOWLAN_TRIG_PKT_PATTERN, &pattern_data)) {
      struct nl80211_pattern_support* patt_support =
          reinterpret_cast<struct nl80211_pattern_support*>(
              pattern_data.GetData());
      // Determine the IPv4 and IPv6 pattern lengths we will use by
      // constructing fake patterns and getting their lengths.
      ByteString fake_pattern;
      ByteString fake_mask;
      // Currently intel WiFi chip doesn't wake on min len patterns(b/62726471).
      // Adding 1 as a hack to fix this issue.
      // TODO(ravisadineni): Remove this check after b/62726471 is fixed.
      min_pattern_len_ = patt_support->min_pattern_len + 1;
      WakeOnWiFi::CreateIPV4PatternAndMask(IPAddress("192.168.0.20"),
                                           min_pattern_len_, &fake_pattern,
                                           &fake_mask);
      size_t ipv4_pattern_len = fake_pattern.GetLength();
      WakeOnWiFi::CreateIPV6PatternAndMask(
          IPAddress("FEDC:BA98:7654:3210:FEDC:BA98:7654:3210"), &fake_pattern,
          &fake_mask, min_pattern_len_);
      size_t ipv6_pattern_len = fake_pattern.GetLength();
      WakeOnWiFi::CreatePacketTypePatternAndMaskforIPV4(
          mac_address_, min_pattern_len_, IPPROTO_TCP, &fake_pattern,
          &fake_mask);
      size_t ipv4_packet_type_pattern_len = fake_pattern.GetLength();
      WakeOnWiFi::CreatePacketTypePatternAndMaskforIPV6(
          mac_address_, min_pattern_len_, IPPROTO_TCP, &fake_pattern,
          &fake_mask);
      size_t ipv6_packet_type_pattern_len = fake_pattern.GetLength();
      size_t min_pattern_len = std::min({ipv4_pattern_len, ipv6_pattern_len,
                                         ipv4_packet_type_pattern_len,
                                         ipv6_packet_type_pattern_len});
      size_t max_pattern_len = std::max({ipv4_pattern_len, ipv6_pattern_len,
                                         ipv4_packet_type_pattern_len,
                                         ipv6_packet_type_pattern_len});
      // Check if the pattern matching capabilities of this WiFi device will
      // allow all possible patterns to be used.
      if (patt_support->min_pattern_len <= min_pattern_len &&
          patt_support->max_pattern_len >= max_pattern_len) {
        wake_on_wifi_triggers_supported_.insert(
            WakeOnWiFi::kWakeTriggerPattern);
        wake_on_wifi_max_patterns_ = patt_support->max_patterns;
        SLOG(this, 7) << "Waking on up to " << wake_on_wifi_max_patterns_
                      << " registered patterns of "
                      << patt_support->min_pattern_len << "-"
                      << patt_support->max_pattern_len
                      << " bytes supported by this WiFi device";
      }
    }
    if (triggers_supported->GetU32AttributeValue(NL80211_WOWLAN_TRIG_NET_DETECT,
                                                 &wake_on_wifi_max_ssids_)) {
      wake_on_wifi_triggers_supported_.insert(WakeOnWiFi::kWakeTriggerSSID);
      SLOG(this, 7) << "Waking on up to " << wake_on_wifi_max_ssids_
                    << " SSIDs supported by this WiFi device";
    }
  }
}

void WakeOnWiFi::OnWakeupReasonReceived(const NetlinkMessage& netlink_message) {
  // We only handle wakeup reason messages in this handler, which is are
  // nl80211 messages with the NL80211_CMD_SET_WOWLAN command.
  if (netlink_message.message_type() != Nl80211Message::GetMessageType()) {
    SLOG(this, 7) << __func__ << ": "
                  << "Not a NL80211 Message";
    return;
  }
  const Nl80211Message& wakeup_reason_msg =
      *reinterpret_cast<const Nl80211Message*>(&netlink_message);
  if (wakeup_reason_msg.command() != SetWakeOnPacketConnMessage::kCommand) {
    SLOG(this, 7) << __func__ << ": "
                  << "Not a NL80211_CMD_SET_WOWLAN message";
    return;
  }
  uint32_t wiphy_index;
  if (!wakeup_reason_msg.const_attributes()->GetU32AttributeValue(
          NL80211_ATTR_WIPHY, &wiphy_index)) {
    LOG(ERROR) << "NL80211_CMD_NEW_WIPHY had no NL80211_ATTR_WIPHY";
    return;
  }
  if (!wiphy_index_received_) {
    SLOG(this, 7) << __func__ << ": "
                  << "Interface index not yet received";
    return;
  }
  if (wiphy_index != wiphy_index_) {
    SLOG(this, 7) << __func__ << ": "
                  << "Wakeup reason not meant for this interface";
    return;
  }
  metrics_->NotifyWakeupReasonReceived();
  SLOG(this, 3) << __func__ << ": "
                << "Parsing wakeup reason";
  AttributeListConstRefPtr triggers;
  if (!wakeup_reason_msg.const_attributes()->ConstGetNestedAttributeList(
          NL80211_ATTR_WOWLAN_TRIGGERS, &triggers)) {
    SLOG(this, 3) << __func__ << ": "
                  << "Wakeup reason: Not wake on WiFi related";
    return;
  }
  bool wake_flag;
  if (triggers->GetFlagAttributeValue(NL80211_WOWLAN_TRIG_DISCONNECT,
                                      &wake_flag)) {
    SLOG(this, 3) << __func__ << ": "
                  << "Wakeup reason: Disconnect";
    last_wake_reason_ = kWakeTriggerDisconnect;
    record_wake_reason_callback_.Run(GetLastWakeReason(nullptr));
    return;
  }
  uint32_t wake_pattern_index;
  if (triggers->GetU32AttributeValue(NL80211_WOWLAN_TRIG_PKT_PATTERN,
                                     &wake_pattern_index)) {
    SLOG(this, 3) << __func__ << ": "
                  << "Wakeup reason: Pattern " << wake_pattern_index;
    last_wake_reason_ = kWakeTriggerPattern;
    record_wake_reason_callback_.Run(GetLastWakeReason(nullptr));
    return;
  }
  AttributeListConstRefPtr results_list;
  if (triggers->ConstGetNestedAttributeList(
          NL80211_WOWLAN_TRIG_NET_DETECT_RESULTS, &results_list)) {
    // It is possible that NL80211_WOWLAN_TRIG_NET_DETECT_RESULTS is present
    // along with another wake trigger attribute. What this means is that the
    // firmware has detected a network, but the platform did not actually wake
    // on the detection of that network. In these cases, we will not parse the
    // net detect results; we return after parsing and reporting the actual
    // wakeup reason above.
    SLOG(this, 3) << __func__ << ": "
                  << "Wakeup reason: SSID";
    last_wake_reason_ = kWakeTriggerSSID;
    record_wake_reason_callback_.Run(GetLastWakeReason(nullptr));
    last_ssid_match_freqs_ = ParseWakeOnSSIDResults(results_list);
    return;
  }
  SLOG(this, 3) << __func__ << ": "
                << "Wakeup reason: Not supported";
}

void WakeOnWiFi::OnBeforeSuspend(
    bool is_connected,
    const std::vector<ByteString>& allowed_ssids,
    const ResultCallback& done_callback,
    const base::Closure& renew_dhcp_lease_callback,
    const base::Closure& remove_supplicant_networks_callback,
    bool have_dhcp_lease,
    uint32_t time_to_next_lease_renewal) {
  connected_before_suspend_ = is_connected;
  if (WakeOnWiFiDisabled()) {
    // Wake on WiFi not supported or not enabled, so immediately report success.
    done_callback.Run(Error(Error::kSuccess));
    return;
  }
  LOG(INFO) << __func__ << ": Wake on WiFi features enabled: "
            << wake_on_wifi_features_enabled_;
  suspend_actions_done_callback_ = done_callback;
  wake_on_allowed_ssids_ = allowed_ssids;
  dark_resume_history_.Clear();
  if (have_dhcp_lease && is_connected &&
      time_to_next_lease_renewal < kImmediateDHCPLeaseRenewalThresholdSeconds) {
    // Renew DHCP lease immediately if we have one that is expiring soon.
    renew_dhcp_lease_callback.Run();
    dispatcher_->PostTask(
        FROM_HERE, base::Bind(&WakeOnWiFi::BeforeSuspendActions,
                              weak_ptr_factory_.GetWeakPtr(), is_connected,
                              false, time_to_next_lease_renewal,
                              remove_supplicant_networks_callback));
  } else {
    dispatcher_->PostTask(
        FROM_HERE, base::Bind(&WakeOnWiFi::BeforeSuspendActions,
                              weak_ptr_factory_.GetWeakPtr(), is_connected,
                              have_dhcp_lease, time_to_next_lease_renewal,
                              remove_supplicant_networks_callback));
  }
}

void WakeOnWiFi::OnAfterResume() {
  SLOG(this, 1) << __func__;
  wake_to_scan_timer_->Stop();
  dhcp_lease_renewal_timer_->Stop();
  if (WakeOnWiFiPacketEnabledAndSupported() ||
      WakeOnWiFiDarkConnectEnabledAndSupported()) {
    // Unconditionally disable wake on WiFi on resume if these features
    // were enabled before the last suspend.
    DisableWakeOnWiFi();
    metrics_->NotifySuspendWithWakeOnWiFiEnabledDone();
  }
}

void WakeOnWiFi::OnDarkResume(
    bool is_connected,
    const std::vector<ByteString>& allowed_ssids,
    const ResultCallback& done_callback,
    const base::Closure& renew_dhcp_lease_callback,
    const InitiateScanCallback& initiate_scan_callback,
    const base::Closure& remove_supplicant_networks_callback) {
  if (WakeOnWiFiDisabled()) {
    // Wake on WiFi not supported or not enabled, so immediately report success.
    done_callback.Run(Error(Error::kSuccess));
    return;
  }

  LOG(INFO) << __func__ << ": "
            << "Wake reason " << last_wake_reason_;
  metrics_->NotifyWakeOnWiFiOnDarkResume(last_wake_reason_);
  dark_resume_scan_retries_left_ = 0;
  suspend_actions_done_callback_ = done_callback;
  wake_on_allowed_ssids_ = allowed_ssids;

  if (last_wake_reason_ == kWakeTriggerSSID ||
      last_wake_reason_ == kWakeTriggerDisconnect ||
      (last_wake_reason_ == kWakeTriggerUnsupported && !is_connected)) {
    // We want to disable wake on WiFi in two specific cases of thrashing:
    //   1) Repeatedly waking on SSID in the presence of an AP that the WiFi
    //      device cannot connect to
    //   2) Repeatedly waking on disconnect because of a an AP that repeatedly
    //      disconnects the WiFi device but allows it to reconnect immediately
    // Therefore, we only count dark resumes caused by either of these wake
    // reasons when deciding whether or not to throttle wake on WiFi.
    //
    // In case the WiFi driver does not support wake reason reporting, we use
    // the WiFi device's connection status on dark resume as a proxy for these
    // wake reasons (i.e. when we wake on either SSID or disconnect, we should
    // be disconnected). This is not reliable for wake on disconnect, as the
    // WiFi device will report that it is connected as it enters dark
    // resume (crbug.com/505072).
    dark_resume_history_.RecordEvent();
  }
  if (dark_resume_history_.CountEventsWithinInterval(
          kDarkResumeFrequencySamplingPeriodShortMinutes * 60,
          EventHistory::kClockTypeBoottime) >= kMaxDarkResumesPerPeriodShort ||
      dark_resume_history_.CountEventsWithinInterval(
          kDarkResumeFrequencySamplingPeriodLongMinutes * 60,
          EventHistory::kClockTypeBoottime) >= kMaxDarkResumesPerPeriodLong) {
    LOG(ERROR) << __func__ << ": "
               << "Too many dark resumes; disabling wake on WiFi temporarily";
    // If too many dark resumes have triggered recently, we are probably
    // thrashing. Stop this by disabling wake on WiFi on the NIC, and
    // starting the wake to scan timer so that normal wake on WiFi behavior
    // resumes only |wake_to_scan_period_seconds_| later.
    dhcp_lease_renewal_timer_->Stop();
    wake_to_scan_timer_->Start(
        FROM_HERE, base::TimeDelta::FromSeconds(wake_to_scan_period_seconds_),
        base::Bind(&WakeOnWiFi::OnTimerWakeDoNothing, base::Unretained(this)));
    DisableWakeOnWiFi();
    dark_resume_history_.Clear();
    metrics_->NotifyWakeOnWiFiThrottled();
    last_ssid_match_freqs_.clear();
    return;
  }

  switch (last_wake_reason_) {
    case kWakeTriggerPattern: {
      // Go back to suspend immediately since packet would have been delivered
      // to userspace upon waking in dark resume. Do not reset the lease renewal
      // timer since we are not getting a new lease.
      dispatcher_->PostTask(
          FROM_HERE, base::Bind(&WakeOnWiFi::BeforeSuspendActions,
                                weak_ptr_factory_.GetWeakPtr(), is_connected,
                                false, 0, remove_supplicant_networks_callback));
      break;
    }
    case kWakeTriggerSSID:
    case kWakeTriggerDisconnect: {
      remove_supplicant_networks_callback.Run();
      metrics_->NotifyDarkResumeInitiateScan();
      InitiateScanInDarkResume(initiate_scan_callback,
                               last_wake_reason_ == kWakeTriggerSSID
                                   ? last_ssid_match_freqs_
                                   : WiFi::FreqSet());
      break;
    }
    case kWakeTriggerUnsupported:
    default: {
      if (is_connected) {
        renew_dhcp_lease_callback.Run();
      } else {
        remove_supplicant_networks_callback.Run();
        metrics_->NotifyDarkResumeInitiateScan();
        InitiateScanInDarkResume(initiate_scan_callback, WiFi::FreqSet());
      }
    }
  }

  // Only set dark resume to true after checking if we need to disable wake on
  // WiFi since calling WakeOnWiFi::DisableWakeOnWiFi directly bypasses
  // WakeOnWiFi::BeforeSuspendActions where |in_dark_resume_| is set to false.
  in_dark_resume_ = true;
  // Assume that we are disconnected if we time out. Consequently, we do not
  // need to start a DHCP lease renewal timer.
  dark_resume_actions_timeout_callback_.Reset(base::Bind(
      &WakeOnWiFi::BeforeSuspendActions, weak_ptr_factory_.GetWeakPtr(), false,
      false, 0, remove_supplicant_networks_callback));
  dispatcher_->PostDelayedTask(FROM_HERE,
                               dark_resume_actions_timeout_callback_.callback(),
                               DarkResumeActionsTimeoutMilliseconds);
}

void WakeOnWiFi::BeforeSuspendActions(
    bool is_connected,
    bool start_lease_renewal_timer,
    uint32_t time_to_next_lease_renewal,
    const base::Closure& remove_supplicant_networks_callback) {
  LOG(INFO) << __func__ << ": "
            << (is_connected ? "connected" : "not connected");
  // Note: No conditional compilation because all entry points to this functions
  // are already conditionally compiled based on DISABLE_WAKE_ON_WIFI.

  metrics_->NotifyBeforeSuspendActions(is_connected, in_dark_resume_);
  last_ssid_match_freqs_.clear();
  last_wake_reason_ = kWakeTriggerUnsupported;
  // Add relevant triggers to be programmed into the NIC.
  wake_on_wifi_triggers_.clear();
  if ((!wake_on_packet_connections_.Empty() ||
       !wake_on_packet_types_.empty()) &&
      WakeOnWiFiPacketEnabledAndSupported() && is_connected) {
    SLOG(this, 3) << __func__ << ": "
                  << "Enabling wake on pattern";
    wake_on_wifi_triggers_.insert(kWakeTriggerPattern);
  }
  if (WakeOnWiFiDarkConnectEnabledAndSupported()) {
    if (is_connected) {
      SLOG(this, 3) << __func__ << ": "
                    << "Enabling wake on disconnect";
      wake_on_wifi_triggers_.insert(kWakeTriggerDisconnect);
      wake_on_wifi_triggers_.erase(kWakeTriggerSSID);
      wake_to_scan_timer_->Stop();
      if (start_lease_renewal_timer) {
        // Timer callback is NO-OP since dark resume logic (the
        // kWakeTriggerUnsupported case) will initiate DHCP lease renewal.
        dhcp_lease_renewal_timer_->Start(
            FROM_HERE, base::TimeDelta::FromSeconds(time_to_next_lease_renewal),
            base::Bind(&WakeOnWiFi::OnTimerWakeDoNothing,
                       base::Unretained(this)));
      }
    } else {
      // Force a disconnect in case supplicant is currently in the process of
      // connecting, and remove all networks so scans triggered in dark resume
      // are passive.
      remove_supplicant_networks_callback.Run();
      dhcp_lease_renewal_timer_->Stop();
      wake_on_wifi_triggers_.erase(kWakeTriggerDisconnect);
      if (!wake_on_allowed_ssids_.empty()) {
        SLOG(this, 3) << __func__ << ": "
                      << "Enabling wake on SSID";
        wake_on_wifi_triggers_.insert(kWakeTriggerSSID);
      }
      int num_extra_ssids =
          wake_on_allowed_ssids_.size() - wake_on_wifi_max_ssids_;
      if (num_extra_ssids > 0 || force_wake_to_scan_timer_) {
        SLOG(this, 3) << __func__ << ": "
                      << "Starting wake to scan timer - "
                      << (num_extra_ssids > 0 ? "extra SSIDs" : "forced");
        if (num_extra_ssids > 0) {
          SLOG(this, 3) << __func__ << ": " << num_extra_ssids
                        << " extra SSIDs.";
        }
        // Start wake to scan timer in case the only SSIDs available for
        // auto-connect during suspend are the ones that we do not program our
        // NIC to wake on.
        // Timer callback is NO-OP since dark resume logic (the
        // kWakeTriggerUnsupported case) will initiate a passive scan.
        wake_to_scan_timer_->Start(
            FROM_HERE,
            base::TimeDelta::FromSeconds(wake_to_scan_period_seconds_),
            base::Bind(&WakeOnWiFi::OnTimerWakeDoNothing,
                       base::Unretained(this)));
        // Trim SSID list to the max size that the NIC supports.
        wake_on_allowed_ssids_.resize(wake_on_wifi_max_ssids_);
      }
    }
  }

  // Only call Cancel() here since it deallocates the underlying callback that
  // |remove_supplicant_networks_callback| references, which is invoked above.
  dark_resume_actions_timeout_callback_.Cancel();

  if (!in_dark_resume_ && wake_on_wifi_triggers_.empty()) {
    // No need program NIC on normal resume in this case since wake on WiFi
    // would already have been disabled on the last (non-dark) resume.
    SLOG(this, 1) << "No need to disable wake on WiFi on NIC in regular "
                     "suspend";
    RunAndResetSuspendActionsDoneCallback(Error(Error::kSuccess));
    return;
  }

  in_dark_resume_ = false;
  ApplyWakeOnWiFiSettings();
}

// static
WiFi::FreqSet WakeOnWiFi::ParseWakeOnSSIDResults(
    AttributeListConstRefPtr results_list) {
  WiFi::FreqSet freqs;
  AttributeIdIterator results_iter(*results_list);
  if (results_iter.AtEnd()) {
    SLOG(WiFi, nullptr, 3) << __func__ << ": "
                           << "Wake on SSID results not available";
    return freqs;
  }
  AttributeListConstRefPtr result;
  int ssid_num = 0;
  for (; !results_iter.AtEnd(); results_iter.Advance()) {
    if (!results_list->ConstGetNestedAttributeList(results_iter.GetId(),
                                                   &result)) {
      LOG(ERROR) << __func__ << ": "
                 << "Could not get result #" << results_iter.GetId()
                 << " in ssid_results";
      return freqs;
    }
    ByteString ssid_bytestring;
    if (!result->GetRawAttributeValue(NL80211_ATTR_SSID, &ssid_bytestring)) {
      // We assume that the SSID attribute must be present in each result.
      LOG(ERROR) << __func__ << ": "
                 << "No SSID available for result #" << results_iter.GetId();
      continue;
    }
    SLOG(WiFi, nullptr, 3) << "SSID " << ssid_num << ": "
                           << std::string(ssid_bytestring.GetConstData(),
                                          ssid_bytestring.GetConstData() +
                                              ssid_bytestring.GetLength());
    AttributeListConstRefPtr frequencies;
    uint32_t freq_value;
    if (result->ConstGetNestedAttributeList(NL80211_ATTR_SCAN_FREQUENCIES,
                                            &frequencies)) {
      AttributeIdIterator freq_iter(*frequencies);
      for (; !freq_iter.AtEnd(); freq_iter.Advance()) {
        if (frequencies->GetU32AttributeValue(freq_iter.GetId(), &freq_value)) {
          freqs.insert(freq_value);
          SLOG(WiFi, nullptr, 7) << "Frequency: " << freq_value;
        }
      }
    } else {
      SLOG(WiFi, nullptr, 3)
          << __func__ << ": "
          << "No frequencies available for result #" << results_iter.GetId();
    }
    ++ssid_num;
  }
  return freqs;
}

void WakeOnWiFi::InitiateScanInDarkResume(
    const InitiateScanCallback& initiate_scan_callback,
    const WiFi::FreqSet& freqs) {
  SLOG(this, 3) << __func__;
  if (!freqs.empty() && freqs.size() <= kMaxFreqsForDarkResumeScanRetries) {
    SLOG(this, 3) << __func__ << ": "
                  << "Allowing up to " << kMaxDarkResumeScanRetries
                  << " retries for passive scan on " << freqs.size()
                  << " frequencies";
    dark_resume_scan_retries_left_ = kMaxDarkResumeScanRetries;
  }
  initiate_scan_callback.Run(freqs);
}

void WakeOnWiFi::OnConnectedAndReachable(bool start_lease_renewal_timer,
                                         uint32_t time_to_next_lease_renewal) {
  SLOG(this, 3) << __func__;
  if (WakeOnWiFiDisabled()) {
    SLOG(this, 3) << "Wake on WiFi not enabled";
  }
  if (!in_dark_resume_) {
    SLOG(this, 3) << "Not in dark resume";
    return;
  }
  BeforeSuspendActions(true, start_lease_renewal_timer,
                       time_to_next_lease_renewal, base::Closure());
}

void WakeOnWiFi::ReportConnectedToServiceAfterWake(bool is_connected,
                                                   int seconds_in_suspend) {
  Metrics::WiFiConnectionStatusAfterWake status;
  if (WakeOnWiFiDarkConnectEnabledAndSupported()) {
    // Only logged if wake on WiFi is supported and wake on SSID was enabled to
    // maintain connectivity while suspended.
    status = is_connected
                 ? Metrics::kWiFiConnectionStatusAfterWakeWoWOnConnected
                 : Metrics::kWiFiConnectionStatusAfterWakeWoWOnDisconnected;
  } else {
    status = is_connected
                 ? Metrics::kWiFiConnectionStatusAfterWakeWoWOffConnected
                 : Metrics::kWiFiConnectionStatusAfterWakeWoWOffDisconnected;
  }
  metrics_->NotifyConnectedToServiceAfterWake(status);

  // Only log time spent in suspended state for each
  // connection status if wifi was connected before suspending
  if (connected_before_suspend_) {
    LOG(INFO) << "NotifySuspendDurationAfterWake: "
              << "status: " << status
              << "seconds_in_suspend: " << seconds_in_suspend;
    metrics_->NotifySuspendDurationAfterWake(status, seconds_in_suspend);
  }
}

void WakeOnWiFi::OnNoAutoConnectableServicesAfterScan(
    const std::vector<ByteString>& allowed_ssids,
    const base::Closure& remove_supplicant_networks_callback,
    const InitiateScanCallback& initiate_scan_callback) {
  SLOG(this, 3) << __func__ << ": "
                << (in_dark_resume_ ? "In dark resume" : "Not in dark resume");
  if (WakeOnWiFiDisabled()) {
    // The scan is not triggered by us, ignore.
    return;
  }
  if (!in_dark_resume_) {
    return;
  }
  if (dark_resume_scan_retries_left_) {
    --dark_resume_scan_retries_left_;
    SLOG(this, 3) << __func__ << ": "
                  << "Retrying dark resume scan ("
                  << dark_resume_scan_retries_left_ << " tries left)";
    metrics_->NotifyDarkResumeScanRetry();
    // Note: a scan triggered by supplicant in dark resume might cause a
    // retry, but we consider this acceptable.
    initiate_scan_callback.Run(last_ssid_match_freqs_);
  } else {
    wake_on_allowed_ssids_ = allowed_ssids;
    // Assume that if there are no services available for auto-connect, then we
    // cannot be connected. Therefore, no need for lease renewal parameters.
    BeforeSuspendActions(false, false, 0, remove_supplicant_networks_callback);
  }
}

void WakeOnWiFi::OnWiphyIndexReceived(uint32_t index) {
  wiphy_index_ = index;
  wiphy_index_received_ = true;
}

void WakeOnWiFi::OnScanStarted(bool is_active_scan) {
  if (!in_dark_resume_) {
    return;
  }
  if (last_wake_reason_ == kWakeTriggerUnsupported ||
      last_wake_reason_ == kWakeTriggerPattern) {
    // We don't expect active scans to be started when we wake on pattern or
    // RTC timers.
    if (is_active_scan) {
      LOG(ERROR) << "Unexpected active scan launched in dark resume";
    }
    metrics_->NotifyScanStartedInDarkResume(is_active_scan);
  }
}

void WakeOnWiFi::OnScanCompleted() {
  if (in_dark_resume_) {
    metrics_->NotifyDarkResumeScanResultsReceived();
  }
}

}  // namespace shill
