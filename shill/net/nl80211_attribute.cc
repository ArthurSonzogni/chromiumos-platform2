// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/net/nl80211_attribute.h"

#include <netlink/attr.h>

#include <string>

#include <base/bind.h>
#include <base/format_macros.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>

using base::Bind;
using base::StringAppendF;
using base::StringPrintf;
using std::string;

namespace shill {

const int Nl80211AttributeCookie::kName = NL80211_ATTR_COOKIE;
const char Nl80211AttributeCookie::kNameString[] = "NL80211_ATTR_COOKIE";

const int Nl80211AttributeBss::kName = NL80211_ATTR_BSS;
const char Nl80211AttributeBss::kNameString[] = "NL80211_ATTR_BSS";
const int Nl80211AttributeBss::kChannelsAttributeId = 0x24;
const int Nl80211AttributeBss::kChallengeTextAttributeId = 0x10;
const int Nl80211AttributeBss::kCountryInfoAttributeId = 0x07;
const int Nl80211AttributeBss::kDSParameterSetAttributeId = 0x03;
const int Nl80211AttributeBss::kErpAttributeId = 0x2a;
const int Nl80211AttributeBss::kExtendedRatesAttributeId = 0x32;
const int Nl80211AttributeBss::kHtCapAttributeId = 0x2d;
const int Nl80211AttributeBss::kHtInfoAttributeId = 0x3d;
const int Nl80211AttributeBss::kPowerCapabilityAttributeId = 0x21;
const int Nl80211AttributeBss::kPowerConstraintAttributeId = 0x20;
const int Nl80211AttributeBss::kRequestAttributeId = 0x0a;
const int Nl80211AttributeBss::kRsnAttributeId = 0x30;
const int Nl80211AttributeBss::kSsidAttributeId = 0x00;
const int Nl80211AttributeBss::kSupportedRatesAttributeId = 0x01;
const int Nl80211AttributeBss::kTcpReportAttributeId = 0x23;
const int Nl80211AttributeBss::kVendorSpecificAttributeId = 0xdd;

static const char kSsidString[] = "SSID";
static const char kRatesString[] = "Rates";

Nl80211AttributeBss::Nl80211AttributeBss()
      : NetlinkNestedAttribute(kName, kNameString) {
  nested_template_.push_back(NestedData(NLA_U32, "__NL80211_BSS_INVALID",
                                        false));
  nested_template_.push_back(NestedData(NLA_UNSPEC, "NL80211_BSS_BSSID",
                                        false));
  nested_template_.push_back(NestedData(NLA_U32, "NL80211_BSS_FREQUENCY",
                                        false));
  nested_template_.push_back(NestedData(NLA_U64, "NL80211_BSS_TSF", false));
  nested_template_.push_back(NestedData(NLA_U16, "NL80211_BSS_BEACON_INTERVAL",
                                        false));
  nested_template_.push_back(NestedData(NLA_U16, "NL80211_BSS_CAPABILITY",
                                        false));
  nested_template_.push_back(NestedData(
      NLA_UNSPEC, "NL80211_BSS_INFORMATION_ELEMENTS", false,
      Bind(&Nl80211AttributeBss::ParseInformationElements)));
  nested_template_.push_back(NestedData(NLA_U32, "NL80211_BSS_SIGNAL_MBM",
                                        false));
  nested_template_.push_back(NestedData(NLA_U8, "NL80211_BSS_SIGNAL_UNSPEC",
                                        false));
  nested_template_.push_back(NestedData(NLA_U32, "NL80211_BSS_STATUS",
                                        false));
  nested_template_.push_back(NestedData(NLA_U32, "NL80211_BSS_SEEN_MS_AGO",
                                        false));
  nested_template_.push_back(NestedData(NLA_UNSPEC, "NL80211_BSS_BEACON_IES",
                                        false));
}

bool Nl80211AttributeBss::ParseInformationElements(
    AttributeList *attribute_list, size_t id, const string &attribute_name,
    ByteString data) {
  if (!attribute_list) {
    LOG(ERROR) << "NULL |attribute_list| parameter";
    return false;
  }
  attribute_list->CreateNestedAttribute(id, attribute_name.c_str());

  // Now, handle the nested data.
  AttributeListRefPtr ie_attribute;
  if (!attribute_list->GetNestedAttributeList(id, &ie_attribute) ||
      !ie_attribute) {
    LOG(ERROR) << "Couldn't get attribute " << attribute_name
               << " which we just created.";
    return false;
  }
  while (data.GetLength()) {
    const uint8_t *sub_attribute = data.GetConstData();
    const size_t kHeaderBytes = 2;
    uint8_t type = sub_attribute[0];
    uint8_t payload_bytes = sub_attribute[1];
    const uint8_t *payload = &sub_attribute[kHeaderBytes];
    // See http://dox.ipxe.org/ieee80211_8h_source.html for more info on types
    // and data inside information elements.
    switch (type) {
      case kSsidAttributeId: {
        ie_attribute->CreateSsidAttribute(type, kSsidString);
        if (payload_bytes == 0) {
          ie_attribute->SetStringAttributeValue(type, "");
        } else {
          ie_attribute->SetStringAttributeValue(type, string(
              reinterpret_cast<const char *>(payload), payload_bytes));
        }
        break;
      }
      case kSupportedRatesAttributeId:
      case kExtendedRatesAttributeId: {
        ie_attribute->CreateNestedAttribute(type, kRatesString);
        AttributeListRefPtr rates_attribute;
        if (!ie_attribute->GetNestedAttributeList(type, &rates_attribute) ||
            !rates_attribute) {
          LOG(ERROR) << "Couldn't get attribute " << attribute_name
                     << " which we just created.";
          break;
        }
        // Extract each rate, add it to the list.
        for (size_t i = 0; i < payload_bytes; ++i) {
          string rate_name = StringPrintf("Rate-%zu", i);
          rates_attribute->CreateU8Attribute(i, rate_name.c_str());
          rates_attribute->SetU8AttributeValue(i, payload[i]);
        }
        ie_attribute->SetNestedAttributeHasAValue(type);
        break;
      }
      case kDSParameterSetAttributeId:
      case kCountryInfoAttributeId:
      case kRequestAttributeId:
      case kChallengeTextAttributeId:
      case kPowerConstraintAttributeId:
      case kPowerCapabilityAttributeId:
      case kTcpReportAttributeId:
      case kChannelsAttributeId:
      case kErpAttributeId:
      case kHtCapAttributeId:
      case kRsnAttributeId:
      case kHtInfoAttributeId:
      case kVendorSpecificAttributeId:
      default:
        break;
    }
    data.RemovePrefix(kHeaderBytes + payload_bytes);
  }
  attribute_list->SetNestedAttributeHasAValue(id);
  return true;
}

const int Nl80211AttributeWiphyBands::kName = NL80211_ATTR_WIPHY_BANDS;
const char Nl80211AttributeWiphyBands::kNameString[] =
  "NL80211_ATTR_WIPHY_BANDS";

Nl80211AttributeWiphyBands::Nl80211AttributeWiphyBands()
      : NetlinkNestedAttribute(kName, kNameString) {
  // Frequencies
  NestedData freq(NLA_NESTED, "NL80211_BAND_ATTR_FREQ", true);
  freq.deeper_nesting.push_back(
      NestedData(NLA_U32, "__NL80211_FREQUENCY_ATTR_INVALID", false));
  freq.deeper_nesting.push_back(
      NestedData(NLA_U32, "NL80211_FREQUENCY_ATTR_FREQ", false));
  freq.deeper_nesting.push_back(
      NestedData(NLA_FLAG, "NL80211_FREQUENCY_ATTR_DISABLED", false));
  freq.deeper_nesting.push_back(
      NestedData(NLA_FLAG, "NL80211_FREQUENCY_ATTR_PASSIVE_SCAN", false));
  freq.deeper_nesting.push_back(
      NestedData(NLA_FLAG, "NL80211_FREQUENCY_ATTR_NO_IBSS", false));
  freq.deeper_nesting.push_back(
      NestedData(NLA_FLAG, "NL80211_FREQUENCY_ATTR_RADAR", false));
  freq.deeper_nesting.push_back(
      NestedData(NLA_U32, "NL80211_FREQUENCY_ATTR_MAX_TX_POWER", false));

  NestedData freqs(NLA_NESTED, "NL80211_BAND_ATTR_FREQS", false);
  freqs.deeper_nesting.push_back(freq);

  // Rates
  NestedData rate(NLA_NESTED, "NL80211_BAND_ATTR_RATE", true);
  rate.deeper_nesting.push_back(
      NestedData(NLA_U32, "__NL80211_BITRATE_ATTR_INVALID", false));
  rate.deeper_nesting.push_back(
      NestedData(NLA_U32, "NL80211_BITRATE_ATTR_RATE", false));
  rate.deeper_nesting.push_back(
      NestedData(NLA_FLAG, "NL80211_BITRATE_ATTR_2GHZ_SHORTPREAMBLE", false));

  NestedData rates(NLA_NESTED, "NL80211_BAND_ATTR_RATES", true);
  rates.deeper_nesting.push_back(rate);

  // Main body of attribute
  NestedData bands(NLA_NESTED, "NL80211_ATTR_BANDS", true);
  bands.deeper_nesting.push_back(
      NestedData(NLA_U32, "__NL80211_BAND_ATTR_INVALID,", false));
  bands.deeper_nesting.push_back(freqs);
  bands.deeper_nesting.push_back(rates);
  bands.deeper_nesting.push_back(
      NestedData(NLA_UNSPEC, "NL80211_BAND_ATTR_HT_MCS_SET", false));
  bands.deeper_nesting.push_back(
      NestedData(NLA_U16, "NL80211_BAND_ATTR_HT_CAPA", false));
  bands.deeper_nesting.push_back(
      NestedData(NLA_U8, "NL80211_BAND_ATTR_HT_AMPDU_FACTOR", false));
  bands.deeper_nesting.push_back(
      NestedData(NLA_U8, "NL80211_BAND_ATTR_HT_AMPDU_DENSITY", false));

  nested_template_.push_back(bands);
}

const int Nl80211AttributeWowlanTriggers::kName = NL80211_ATTR_WOWLAN_TRIGGERS;
const char Nl80211AttributeWowlanTriggers::kNameString[] =
    "NL80211_ATTR_WOWLAN_TRIGGERS";

Nl80211AttributeWowlanTriggers::Nl80211AttributeWowlanTriggers()
    : NetlinkNestedAttribute(kName, kNameString) {
  NestedData patterns(NLA_NESTED, "NL80211_WOWLAN_TRIG_PKT_PATTERN", false);
  NestedData individual_pattern(NLA_NESTED,
                                "NL80211_PACKET_PATTERN_ATTR", true);
  individual_pattern.deeper_nesting.push_back(
      NestedData(NLA_U32, "__NL80211_PKTPAT_INVALID", false));
  individual_pattern.deeper_nesting.push_back(
      NestedData(NLA_UNSPEC, "NL80211_PKTPAT_MASK", false));
  individual_pattern.deeper_nesting.push_back(
      NestedData(NLA_UNSPEC, "NL80211_PKTPAT_PATTERN", false));
  individual_pattern.deeper_nesting.push_back(
      NestedData(NLA_U32, "NL80211_PKTPAT_OFFSET", false));

  patterns.deeper_nesting.push_back(individual_pattern);
  nested_template_.push_back(
      NestedData(NLA_U32, "__NL80211_WOWLAN_TRIG_INVALID", false));
  nested_template_.push_back(
      NestedData(NLA_FLAG, "NL80211_WOWLAN_TRIG_ANY", false));
  nested_template_.push_back(
      NestedData(NLA_FLAG, "NL80211_WOWLAN_TRIG_DISCONNECT", false));
  nested_template_.push_back(
      NestedData(NLA_FLAG, "NL80211_WOWLAN_TRIG_MAGIC_PKT", false));
  nested_template_.push_back(patterns);
}

const int Nl80211AttributeWowlanTriggersSupported::kName =
    NL80211_ATTR_WOWLAN_TRIGGERS_SUPPORTED;
const char Nl80211AttributeWowlanTriggersSupported::kNameString[] =
    "NL80211_ATTR_WOWLAN_TRIGGERS_SUPPORTED";

Nl80211AttributeWowlanTriggersSupported::
    Nl80211AttributeWowlanTriggersSupported()
    : NetlinkNestedAttribute(kName, kNameString) {
  nested_template_.push_back(
      NestedData(NLA_U32, "__NL80211_WOWLAN_TRIG_INVALID", false));
  nested_template_.push_back(
      NestedData(NLA_FLAG, "NL80211_WOWLAN_TRIG_ANY", false));
  nested_template_.push_back(
      NestedData(NLA_FLAG, "NL80211_WOWLAN_TRIG_DISCONNECT", false));
  nested_template_.push_back(
      NestedData(NLA_FLAG, "NL80211_WOWLAN_TRIG_MAGIC_PKT", false));
  nested_template_.push_back(
      NestedData(NLA_UNSPEC, "NL80211_WOWLAN_TRIG_PKT_PATTERN", false));
}

const int Nl80211AttributeCipherSuites::kName = NL80211_ATTR_CIPHER_SUITES;
const char Nl80211AttributeCipherSuites::kNameString[] =
    "NL80211_ATTR_CIPHER_SUITES";

const int Nl80211AttributeControlPortEthertype::kName =
    NL80211_ATTR_CONTROL_PORT_ETHERTYPE;
const char Nl80211AttributeControlPortEthertype::kNameString[] =
    "NL80211_ATTR_CONTROL_PORT_ETHERTYPE";

const int Nl80211AttributeCqm::kName = NL80211_ATTR_CQM;
const char Nl80211AttributeCqm::kNameString[] = "NL80211_ATTR_CQM";

Nl80211AttributeCqm::Nl80211AttributeCqm()
      : NetlinkNestedAttribute(kName, kNameString) {
  nested_template_.push_back(
      NestedData(NLA_U32, "__NL80211_ATTR_CQM_INVALID", false));
  nested_template_.push_back(
      NestedData(NLA_U32, "NL80211_ATTR_CQM_RSSI_THOLD", false));
  nested_template_.push_back(
      NestedData(NLA_U32, "NL80211_ATTR_CQM_RSSI_HYST", false));
  nested_template_.push_back(
      NestedData(NLA_U32, "NL80211_ATTR_CQM_RSSI_THRESHOLD_EVENT", false));
  nested_template_.push_back(
      NestedData(NLA_U32, "NL80211_ATTR_CQM_PKT_LOSS_EVENT", false));
}

const int Nl80211AttributeDeviceApSme::kName = NL80211_ATTR_DEVICE_AP_SME;
const char Nl80211AttributeDeviceApSme::kNameString[] =
    "NL80211_ATTR_DEVICE_AP_SME";

const int Nl80211AttributeDisconnectedByAp::kName =
    NL80211_ATTR_DISCONNECTED_BY_AP;
const char Nl80211AttributeDisconnectedByAp::kNameString[] =
    "NL80211_ATTR_DISCONNECTED_BY_AP";

const int Nl80211AttributeDuration::kName = NL80211_ATTR_DURATION;
const char Nl80211AttributeDuration::kNameString[] = "NL80211_ATTR_DURATION";

const int Nl80211AttributeFeatureFlags::kName = NL80211_ATTR_FEATURE_FLAGS;
const char Nl80211AttributeFeatureFlags::kNameString[] =
    "NL80211_ATTR_FEATURE_FLAGS";

const int Nl80211AttributeFrame::kName = NL80211_ATTR_FRAME;
const char Nl80211AttributeFrame::kNameString[] = "NL80211_ATTR_FRAME";

const int Nl80211AttributeGeneration::kName = NL80211_ATTR_GENERATION;
const char Nl80211AttributeGeneration::kNameString[] =
    "NL80211_ATTR_GENERATION";

const int Nl80211AttributeHtCapabilityMask::kName =
    NL80211_ATTR_HT_CAPABILITY_MASK;
const char Nl80211AttributeHtCapabilityMask::kNameString[] =
    "NL80211_ATTR_HT_CAPABILITY_MASK";

const int Nl80211AttributeIfindex::kName = NL80211_ATTR_IFINDEX;
const char Nl80211AttributeIfindex::kNameString[] = "NL80211_ATTR_IFINDEX";

const int Nl80211AttributeIftype::kName = NL80211_ATTR_IFTYPE;
const char Nl80211AttributeIftype::kNameString[] = "NL80211_ATTR_IFTYPE";

const int Nl80211AttributeKeyIdx::kName = NL80211_ATTR_KEY_IDX;
const char Nl80211AttributeKeyIdx::kNameString[] = "NL80211_ATTR_KEY_IDX";

const int Nl80211AttributeKeySeq::kName = NL80211_ATTR_KEY_SEQ;
const char Nl80211AttributeKeySeq::kNameString[] = "NL80211_ATTR_KEY_SEQ";

const int Nl80211AttributeKeyType::kName = NL80211_ATTR_KEY_TYPE;
const char Nl80211AttributeKeyType::kNameString[] = "NL80211_ATTR_KEY_TYPE";

const int Nl80211AttributeMac::kName = NL80211_ATTR_MAC;
const char Nl80211AttributeMac::kNameString[] = "NL80211_ATTR_MAC";

bool Nl80211AttributeMac::ToString(std::string *value) const {
  if (!value) {
    LOG(ERROR) << "Null |value| parameter";
    return false;
  }
  *value = StringFromMacAddress(data_.GetConstData());
  return true;
}

// static
string Nl80211AttributeMac::StringFromMacAddress(const uint8_t *arg) {
  string output;

  if (!arg) {
    static const char kBogusMacAddress[] = "XX:XX:XX:XX:XX:XX";
    output = kBogusMacAddress;
    LOG(ERROR) << "|arg| parameter is NULL.";
  } else {
    output = StringPrintf("%02x:%02x:%02x:%02x:%02x:%02x",
                          arg[0], arg[1], arg[2], arg[3], arg[4], arg[5]);
  }
  return output;
}

const int Nl80211AttributeMaxMatchSets::kName = NL80211_ATTR_MAX_MATCH_SETS;
const char Nl80211AttributeMaxMatchSets::kNameString[] =
    "NL80211_ATTR_MAX_MATCH_SETS";

const int Nl80211AttributeMaxNumPmkids::kName = NL80211_ATTR_MAX_NUM_PMKIDS;
const char Nl80211AttributeMaxNumPmkids::kNameString[] =
    "NL80211_ATTR_MAX_NUM_PMKIDS";

const int Nl80211AttributeMaxNumScanSsids::kName =
    NL80211_ATTR_MAX_NUM_SCAN_SSIDS;
const char Nl80211AttributeMaxNumScanSsids::kNameString[] =
    "NL80211_ATTR_MAX_NUM_SCAN_SSIDS";

const int Nl80211AttributeMaxNumSchedScanSsids::kName =
    NL80211_ATTR_MAX_NUM_SCHED_SCAN_SSIDS;
const char Nl80211AttributeMaxNumSchedScanSsids::kNameString[] =
    "NL80211_ATTR_MAX_NUM_SCHED_SCAN_SSIDS";

const int Nl80211AttributeMaxRemainOnChannelDuration::kName =
    NL80211_ATTR_MAX_REMAIN_ON_CHANNEL_DURATION;
const char Nl80211AttributeMaxRemainOnChannelDuration::kNameString[] =
    "NL80211_ATTR_MAX_REMAIN_ON_CHANNEL_DURATION";

const int Nl80211AttributeMaxScanIeLen::kName = NL80211_ATTR_MAX_SCAN_IE_LEN;
const char Nl80211AttributeMaxScanIeLen::kNameString[] =
    "NL80211_ATTR_MAX_SCAN_IE_LEN";

const int Nl80211AttributeMaxSchedScanIeLen::kName =
    NL80211_ATTR_MAX_SCHED_SCAN_IE_LEN;
const char Nl80211AttributeMaxSchedScanIeLen::kNameString[] =
    "NL80211_ATTR_MAX_SCHED_SCAN_IE_LEN";

const int Nl80211AttributeOffchannelTxOk::kName = NL80211_ATTR_OFFCHANNEL_TX_OK;
const char Nl80211AttributeOffchannelTxOk::kNameString[] =
    "NL80211_ATTR_OFFCHANNEL_TX_OK";

const int Nl80211AttributeProbeRespOffload::kName =
    NL80211_ATTR_PROBE_RESP_OFFLOAD;
const char Nl80211AttributeProbeRespOffload::kNameString[] =
    "NL80211_ATTR_PROBE_RESP_OFFLOAD";

const int Nl80211AttributeReasonCode::kName =
    NL80211_ATTR_REASON_CODE;
const char Nl80211AttributeReasonCode::kNameString[] =
    "NL80211_ATTR_REASON_CODE";

const int Nl80211AttributeRegAlpha2::kName = NL80211_ATTR_REG_ALPHA2;
const char Nl80211AttributeRegAlpha2::kNameString[] = "NL80211_ATTR_REG_ALPHA2";

const int Nl80211AttributeRegInitiator::kName =
    NL80211_ATTR_REG_INITIATOR;
const char Nl80211AttributeRegInitiator::kNameString[] =
    "NL80211_ATTR_REG_INITIATOR";

const int Nl80211AttributeRegType::kName = NL80211_ATTR_REG_TYPE;
const char Nl80211AttributeRegType::kNameString[] = "NL80211_ATTR_REG_TYPE";

const int Nl80211AttributeRespIe::kName = NL80211_ATTR_RESP_IE;
const char Nl80211AttributeRespIe::kNameString[] = "NL80211_ATTR_RESP_IE";

const int Nl80211AttributeRoamSupport::kName = NL80211_ATTR_ROAM_SUPPORT;
const char Nl80211AttributeRoamSupport::kNameString[] =
    "NL80211_ATTR_ROAM_SUPPORT";

const int Nl80211AttributeScanFrequencies::kName =
    NL80211_ATTR_SCAN_FREQUENCIES;
const char Nl80211AttributeScanFrequencies::kNameString[] =
    "NL80211_ATTR_SCAN_FREQUENCIES";

Nl80211AttributeScanFrequencies::Nl80211AttributeScanFrequencies()
      : NetlinkNestedAttribute(kName, kNameString) {
  nested_template_.push_back(
      NestedData(NLA_U32, "NL80211_SCAN_FREQ", true));
}

const int Nl80211AttributeScanSsids::kName = NL80211_ATTR_SCAN_SSIDS;
const char Nl80211AttributeScanSsids::kNameString[] = "NL80211_ATTR_SCAN_SSIDS";

Nl80211AttributeScanSsids::Nl80211AttributeScanSsids()
      : NetlinkNestedAttribute(kName, kNameString) {
  nested_template_.push_back(
      NestedData(NLA_STRING, "NL80211_SCAN_SSID", true));
}

const int Nl80211AttributeStaInfo::kName = NL80211_ATTR_STA_INFO;
const char Nl80211AttributeStaInfo::kNameString[] = "NL80211_ATTR_STA_INFO";

Nl80211AttributeStaInfo::Nl80211AttributeStaInfo()
      : NetlinkNestedAttribute(kName, kNameString) {
  NestedData tx_rates(NLA_NESTED, "NL80211_STA_INFO_TX_BITRATE", false);
  tx_rates.deeper_nesting.push_back(
      NestedData(NLA_U32, "__NL80211_RATE_INFO_INVALID", false));
  tx_rates.deeper_nesting.push_back(
      NestedData(NLA_U16, "NL80211_RATE_INFO_BITRATE", false));
  tx_rates.deeper_nesting.push_back(
      NestedData(NLA_U8, "NL80211_RATE_INFO_MCS", false));
  tx_rates.deeper_nesting.push_back(
      NestedData(NLA_FLAG, "NL80211_RATE_INFO_40_MHZ_WIDTH", false));
  tx_rates.deeper_nesting.push_back(
      NestedData(NLA_FLAG, "NL80211_RATE_INFO_SHORT_GI", false));
  tx_rates.deeper_nesting.push_back(
      NestedData(NLA_U32, "NL80211_RATE_INFO_BITRATE32", false));
  tx_rates.deeper_nesting.push_back(
      NestedData(NLA_U8, "NL80211_RATE_INFO_VHT_MCS", false));
  tx_rates.deeper_nesting.push_back(
      NestedData(NLA_U8, "NL80211_RATE_INFO_VHT_NSS", false));
  tx_rates.deeper_nesting.push_back(
      NestedData(NLA_FLAG, "NL80211_RATE_INFO_80_MHZ_WIDTH", false));
  tx_rates.deeper_nesting.push_back(
      NestedData(NLA_FLAG, "NL80211_RATE_INFO_80P80_MHZ_WIDTH", false));
  tx_rates.deeper_nesting.push_back(
      NestedData(NLA_FLAG, "NL80211_RATE_INFO_160_MHZ_WIDTH", false));

  NestedData rx_rates(NLA_NESTED, "NL80211_STA_INFO_RX_BITRATE", false);
  rx_rates.deeper_nesting = tx_rates.deeper_nesting;

  NestedData bss(NLA_NESTED, "NL80211_STA_INFO_BSS_PARAM", false);
  bss.deeper_nesting.push_back(
      NestedData(NLA_U32, "__NL80211_STA_BSS_PARAM_INVALID", false));
  bss.deeper_nesting.push_back(
      NestedData(NLA_FLAG, "NL80211_STA_BSS_PARAM_CTS_PROT", false));
  bss.deeper_nesting.push_back(
      NestedData(NLA_FLAG, "NL80211_STA_BSS_PARAM_SHORT_PREAMBLE",
                           false));
  bss.deeper_nesting.push_back(
      NestedData(NLA_FLAG, "NL80211_STA_BSS_PARAM_SHORT_SLOT_TIME",
                           false));
  bss.deeper_nesting.push_back(
      NestedData(NLA_U8, "NL80211_STA_BSS_PARAM_DTIM_PERIOD", false));
  bss.deeper_nesting.push_back(
      NestedData(NLA_U16, "NL80211_STA_BSS_PARAM_BEACON_INTERVAL", false));

  nested_template_.push_back(
      NestedData(NLA_U32, "__NL80211_STA_INFO_INVALID", false));
  nested_template_.push_back(
      NestedData(NLA_U32, "NL80211_STA_INFO_INACTIVE_TIME", false));
  nested_template_.push_back(
      NestedData(NLA_U32, "NL80211_STA_INFO_RX_BYTES", false));
  nested_template_.push_back(
      NestedData(NLA_U32, "NL80211_STA_INFO_TX_BYTES", false));
  nested_template_.push_back(
      NestedData(NLA_U16, "NL80211_STA_INFO_LLID", false));
  nested_template_.push_back(
      NestedData(NLA_U16, "NL80211_STA_INFO_PLID", false));
  nested_template_.push_back(
      NestedData(NLA_U8, "NL80211_STA_INFO_PLINK_STATE", false));
  nested_template_.push_back(
      NestedData(NLA_U8, "NL80211_STA_INFO_SIGNAL", false));
  nested_template_.push_back(tx_rates);
  nested_template_.push_back(
      NestedData(NLA_U32, "NL80211_STA_INFO_RX_PACKETS", false));
  nested_template_.push_back(
      NestedData(NLA_U32, "NL80211_STA_INFO_TX_PACKETS", false));
  nested_template_.push_back(
      NestedData(NLA_U32, "NL80211_STA_INFO_TX_RETRIES", false));
  nested_template_.push_back(
      NestedData(NLA_U32, "NL80211_STA_INFO_TX_FAILED", false));
  nested_template_.push_back(
      NestedData(NLA_U8, "NL80211_STA_INFO_SIGNAL_AVG", false));
  nested_template_.push_back(rx_rates);
  nested_template_.push_back(bss);
  nested_template_.push_back(
      NestedData(NLA_U32, "NL80211_STA_INFO_CONNECTED_TIME", false));
  nested_template_.push_back(
      NestedData(NLA_U64, "NL80211_STA_INFO_STA_FLAGS", false));
  nested_template_.push_back(
      NestedData(NLA_U32, "NL80211_STA_INFO_BEACON_LOSS", false));
}

const int Nl80211AttributeStatusCode::kName =
    NL80211_ATTR_STATUS_CODE;
const char Nl80211AttributeStatusCode::kNameString[] =
    "NL80211_ATTR_STATUS_CODE";

const int Nl80211AttributeSupportApUapsd::kName = NL80211_ATTR_SUPPORT_AP_UAPSD;
const char Nl80211AttributeSupportApUapsd::kNameString[] =
    "NL80211_ATTR_SUPPORT_AP_UAPSD";

const int Nl80211AttributeSupportIbssRsn::kName = NL80211_ATTR_SUPPORT_IBSS_RSN;
const char Nl80211AttributeSupportIbssRsn::kNameString[] =
    "NL80211_ATTR_SUPPORT_IBSS_RSN";

const int Nl80211AttributeSupportMeshAuth::kName =
    NL80211_ATTR_SUPPORT_MESH_AUTH;
const char Nl80211AttributeSupportMeshAuth::kNameString[] =
    "NL80211_ATTR_SUPPORT_MESH_AUTH";

const int Nl80211AttributeTdlsExternalSetup::kName =
    NL80211_ATTR_TDLS_EXTERNAL_SETUP;
const char Nl80211AttributeTdlsExternalSetup::kNameString[] =
    "NL80211_ATTR_TDLS_EXTERNAL_SETUP";

const int Nl80211AttributeTdlsSupport::kName = NL80211_ATTR_TDLS_SUPPORT;
const char Nl80211AttributeTdlsSupport::kNameString[] =
    "NL80211_ATTR_TDLS_SUPPORT";

const int Nl80211AttributeTimedOut::kName = NL80211_ATTR_TIMED_OUT;
const char Nl80211AttributeTimedOut::kNameString[] = "NL80211_ATTR_TIMED_OUT";

const int Nl80211AttributeWiphyAntennaAvailRx::kName =
    NL80211_ATTR_WIPHY_ANTENNA_AVAIL_RX;
const char Nl80211AttributeWiphyAntennaAvailRx::kNameString[] =
    "NL80211_ATTR_WIPHY_ANTENNA_AVAIL_RX";

const int Nl80211AttributeWiphyAntennaAvailTx::kName =
    NL80211_ATTR_WIPHY_ANTENNA_AVAIL_TX;
const char Nl80211AttributeWiphyAntennaAvailTx::kNameString[] =
    "NL80211_ATTR_WIPHY_ANTENNA_AVAIL_TX";

const int Nl80211AttributeWiphyAntennaRx::kName = NL80211_ATTR_WIPHY_ANTENNA_RX;
const char Nl80211AttributeWiphyAntennaRx::kNameString[] =
    "NL80211_ATTR_WIPHY_ANTENNA_RX";

const int Nl80211AttributeWiphyAntennaTx::kName = NL80211_ATTR_WIPHY_ANTENNA_TX;
const char Nl80211AttributeWiphyAntennaTx::kNameString[] =
    "NL80211_ATTR_WIPHY_ANTENNA_TX";

const int Nl80211AttributeWiphyCoverageClass::kName =
    NL80211_ATTR_WIPHY_COVERAGE_CLASS;
const char Nl80211AttributeWiphyCoverageClass::kNameString[] =
    "NL80211_ATTR_WIPHY_COVERAGE_CLASS";

const int Nl80211AttributeWiphyFragThreshold::kName =
    NL80211_ATTR_WIPHY_FRAG_THRESHOLD;
const char Nl80211AttributeWiphyFragThreshold::kNameString[] =
    "NL80211_ATTR_WIPHY_FRAG_THRESHOLD";

const int Nl80211AttributeWiphyFreq::kName = NL80211_ATTR_WIPHY_FREQ;
const char Nl80211AttributeWiphyFreq::kNameString[] = "NL80211_ATTR_WIPHY_FREQ";

const int Nl80211AttributeWiphy::kName = NL80211_ATTR_WIPHY;
const char Nl80211AttributeWiphy::kNameString[] = "NL80211_ATTR_WIPHY";

const int Nl80211AttributeWiphyName::kName = NL80211_ATTR_WIPHY_NAME;
const char Nl80211AttributeWiphyName::kNameString[] = "NL80211_ATTR_WIPHY_NAME";

const int Nl80211AttributeWiphyRetryLong::kName = NL80211_ATTR_WIPHY_RETRY_LONG;
const char Nl80211AttributeWiphyRetryLong::kNameString[] =
    "NL80211_ATTR_WIPHY_RETRY_LONG";

const int Nl80211AttributeWiphyRetryShort::kName =
    NL80211_ATTR_WIPHY_RETRY_SHORT;
const char Nl80211AttributeWiphyRetryShort::kNameString[] =
    "NL80211_ATTR_WIPHY_RETRY_SHORT";

const int Nl80211AttributeWiphyRtsThreshold::kName =
    NL80211_ATTR_WIPHY_RTS_THRESHOLD;
const char Nl80211AttributeWiphyRtsThreshold::kNameString[] =
    "NL80211_ATTR_WIPHY_RTS_THRESHOLD";

}  // namespace shill
