// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_NL80211_ATTRIBUTE_H_
#define SHILL_WIFI_NL80211_ATTRIBUTE_H_

#include <memory>
#include <string>

#include <base/containers/span.h>
#include <net-base/attribute_list.h>
#include <net-base/netlink_attribute.h>

#include "shill/wifi/nl80211_message.h"

namespace shill {

// Generates the appropriate attribute object from the raw nlattr data.
std::unique_ptr<net_base::NetlinkAttribute> NewNl80211AttributeFromId(
    const Nl80211Message::Context& context, int id);

// Helper function for creating nl80211 attribute inside AttributeList.
bool CreateNl80211Attribute(net_base::AttributeList* attribute_list, int id);

// U8.

class Nl80211AttributeDfsRegion : public net_base::NetlinkU8Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeDfsRegion()
      : net_base::NetlinkU8Attribute(kName, kNameString) {}
  Nl80211AttributeDfsRegion(const Nl80211AttributeDfsRegion&) = delete;
  Nl80211AttributeDfsRegion& operator=(const Nl80211AttributeDfsRegion&) =
      delete;
};

class Nl80211AttributeKeyIdx : public net_base::NetlinkU8Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeKeyIdx() : net_base::NetlinkU8Attribute(kName, kNameString) {}
  Nl80211AttributeKeyIdx(const Nl80211AttributeKeyIdx&) = delete;
  Nl80211AttributeKeyIdx& operator=(const Nl80211AttributeKeyIdx&) = delete;
};

class Nl80211AttributeMaxMatchSets : public net_base::NetlinkU8Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeMaxMatchSets()
      : net_base::NetlinkU8Attribute(kName, kNameString) {}
  Nl80211AttributeMaxMatchSets(const Nl80211AttributeMaxMatchSets&) = delete;
  Nl80211AttributeMaxMatchSets& operator=(const Nl80211AttributeMaxMatchSets&) =
      delete;
};

class Nl80211AttributeMaxNumPmkids : public net_base::NetlinkU8Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeMaxNumPmkids()
      : net_base::NetlinkU8Attribute(kName, kNameString) {}
  Nl80211AttributeMaxNumPmkids(const Nl80211AttributeMaxNumPmkids&) = delete;
  Nl80211AttributeMaxNumPmkids& operator=(const Nl80211AttributeMaxNumPmkids&) =
      delete;
};

class Nl80211AttributeMaxNumScanSsids : public net_base::NetlinkU8Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeMaxNumScanSsids()
      : net_base::NetlinkU8Attribute(kName, kNameString) {}
  Nl80211AttributeMaxNumScanSsids(const Nl80211AttributeMaxNumScanSsids&) =
      delete;
  Nl80211AttributeMaxNumScanSsids& operator=(
      const Nl80211AttributeMaxNumScanSsids&) = delete;
};

class Nl80211AttributeMaxNumSchedScanSsids
    : public net_base::NetlinkU8Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeMaxNumSchedScanSsids()
      : net_base::NetlinkU8Attribute(kName, kNameString) {}
  Nl80211AttributeMaxNumSchedScanSsids(
      const Nl80211AttributeMaxNumSchedScanSsids&) = delete;
  Nl80211AttributeMaxNumSchedScanSsids& operator=(
      const Nl80211AttributeMaxNumSchedScanSsids&) = delete;
};

class Nl80211AttributeRegType : public net_base::NetlinkU8Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeRegType()
      : net_base::NetlinkU8Attribute(kName, kNameString) {}
  Nl80211AttributeRegType(const Nl80211AttributeRegType&) = delete;
  Nl80211AttributeRegType& operator=(const Nl80211AttributeRegType&) = delete;
};

class Nl80211AttributeWiphyCoverageClass : public net_base::NetlinkU8Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeWiphyCoverageClass()
      : net_base::NetlinkU8Attribute(kName, kNameString) {}
  Nl80211AttributeWiphyCoverageClass(
      const Nl80211AttributeWiphyCoverageClass&) = delete;
  Nl80211AttributeWiphyCoverageClass& operator=(
      const Nl80211AttributeWiphyCoverageClass&) = delete;
};

class Nl80211AttributeWiphyRetryLong : public net_base::NetlinkU8Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeWiphyRetryLong()
      : net_base::NetlinkU8Attribute(kName, kNameString) {}
  Nl80211AttributeWiphyRetryLong(const Nl80211AttributeWiphyRetryLong&) =
      delete;
  Nl80211AttributeWiphyRetryLong& operator=(
      const Nl80211AttributeWiphyRetryLong&) = delete;
};

class Nl80211AttributeWiphyRetryShort : public net_base::NetlinkU8Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeWiphyRetryShort()
      : net_base::NetlinkU8Attribute(kName, kNameString) {}
  Nl80211AttributeWiphyRetryShort(const Nl80211AttributeWiphyRetryShort&) =
      delete;
  Nl80211AttributeWiphyRetryShort& operator=(
      const Nl80211AttributeWiphyRetryShort&) = delete;
};

// U16.

class Nl80211AttributeMaxScanIeLen : public net_base::NetlinkU16Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeMaxScanIeLen()
      : net_base::NetlinkU16Attribute(kName, kNameString) {}
  Nl80211AttributeMaxScanIeLen(const Nl80211AttributeMaxScanIeLen&) = delete;
  Nl80211AttributeMaxScanIeLen& operator=(const Nl80211AttributeMaxScanIeLen&) =
      delete;
};

class Nl80211AttributeMaxSchedScanIeLen : public net_base::NetlinkU16Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeMaxSchedScanIeLen()
      : net_base::NetlinkU16Attribute(kName, kNameString) {}
  Nl80211AttributeMaxSchedScanIeLen(const Nl80211AttributeMaxSchedScanIeLen&) =
      delete;
  Nl80211AttributeMaxSchedScanIeLen& operator=(
      const Nl80211AttributeMaxSchedScanIeLen&) = delete;
};

class Nl80211AttributeReasonCode : public net_base::NetlinkU16Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeReasonCode()
      : net_base::NetlinkU16Attribute(kName, kNameString) {}
  Nl80211AttributeReasonCode(const Nl80211AttributeReasonCode&) = delete;
  Nl80211AttributeReasonCode& operator=(const Nl80211AttributeReasonCode&) =
      delete;
};

class Nl80211AttributeStatusCode : public net_base::NetlinkU16Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeStatusCode()
      : net_base::NetlinkU16Attribute(kName, kNameString) {}
  Nl80211AttributeStatusCode(const Nl80211AttributeStatusCode&) = delete;
  Nl80211AttributeStatusCode& operator=(const Nl80211AttributeStatusCode&) =
      delete;
};

// U32.

class Nl80211AttributeDuration : public net_base::NetlinkU32Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeDuration()
      : net_base::NetlinkU32Attribute(kName, kNameString) {}
  Nl80211AttributeDuration(const Nl80211AttributeDuration&) = delete;
  Nl80211AttributeDuration& operator=(const Nl80211AttributeDuration&) = delete;
};

class Nl80211AttributeDeviceApSme : public net_base::NetlinkU32Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeDeviceApSme()
      : net_base::NetlinkU32Attribute(kName, kNameString) {}
  Nl80211AttributeDeviceApSme(const Nl80211AttributeDeviceApSme&) = delete;
  Nl80211AttributeDeviceApSme& operator=(const Nl80211AttributeDeviceApSme&) =
      delete;
};

class Nl80211AttributeFeatureFlags : public net_base::NetlinkU32Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeFeatureFlags()
      : net_base::NetlinkU32Attribute(kName, kNameString) {}
  Nl80211AttributeFeatureFlags(const Nl80211AttributeFeatureFlags&) = delete;
  Nl80211AttributeFeatureFlags& operator=(const Nl80211AttributeFeatureFlags&) =
      delete;
};

class Nl80211AttributeGeneration : public net_base::NetlinkU32Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeGeneration()
      : net_base::NetlinkU32Attribute(kName, kNameString) {}
  Nl80211AttributeGeneration(const Nl80211AttributeGeneration&) = delete;
  Nl80211AttributeGeneration& operator=(const Nl80211AttributeGeneration&) =
      delete;
};

class Nl80211AttributeIfindex : public net_base::NetlinkU32Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeIfindex()
      : net_base::NetlinkU32Attribute(kName, kNameString) {}
  Nl80211AttributeIfindex(const Nl80211AttributeIfindex&) = delete;
  Nl80211AttributeIfindex& operator=(const Nl80211AttributeIfindex&) = delete;
};

class Nl80211AttributeIftype : public net_base::NetlinkU32Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeIftype()
      : net_base::NetlinkU32Attribute(kName, kNameString) {}
  Nl80211AttributeIftype(const Nl80211AttributeIftype&) = delete;
  Nl80211AttributeIftype& operator=(const Nl80211AttributeIftype&) = delete;
};

class Nl80211AttributeKeyType : public net_base::NetlinkU32Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeKeyType()
      : net_base::NetlinkU32Attribute(kName, kNameString) {}
  Nl80211AttributeKeyType(const Nl80211AttributeKeyType&) = delete;
  Nl80211AttributeKeyType& operator=(const Nl80211AttributeKeyType&) = delete;
};

class Nl80211AttributeMaxRemainOnChannelDuration
    : public net_base::NetlinkU32Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeMaxRemainOnChannelDuration()
      : net_base::NetlinkU32Attribute(kName, kNameString) {}
  Nl80211AttributeMaxRemainOnChannelDuration(
      const Nl80211AttributeMaxRemainOnChannelDuration&) = delete;
  Nl80211AttributeMaxRemainOnChannelDuration& operator=(
      const Nl80211AttributeMaxRemainOnChannelDuration&) = delete;
};

class Nl80211AttributeProbeRespOffload : public net_base::NetlinkU32Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeProbeRespOffload()
      : net_base::NetlinkU32Attribute(kName, kNameString) {}
  Nl80211AttributeProbeRespOffload(const Nl80211AttributeProbeRespOffload&) =
      delete;
  Nl80211AttributeProbeRespOffload& operator=(
      const Nl80211AttributeProbeRespOffload&) = delete;
};

// Set  to allow unit tests to instantiate these.
class Nl80211AttributeRegInitiator : public net_base::NetlinkU32Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeRegInitiator()
      : net_base::NetlinkU32Attribute(kName, kNameString) {}
  Nl80211AttributeRegInitiator(const Nl80211AttributeRegInitiator&) = delete;
  Nl80211AttributeRegInitiator& operator=(const Nl80211AttributeRegInitiator&) =
      delete;

  bool InitFromValue(base::span<const uint8_t> input) override;
};

class Nl80211AttributeWiphy : public net_base::NetlinkU32Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeWiphy() : net_base::NetlinkU32Attribute(kName, kNameString) {}
  Nl80211AttributeWiphy(const Nl80211AttributeWiphy&) = delete;
  Nl80211AttributeWiphy& operator=(const Nl80211AttributeWiphy&) = delete;
};

class Nl80211AttributeWiphyAntennaAvailRx
    : public net_base::NetlinkU32Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeWiphyAntennaAvailRx()
      : net_base::NetlinkU32Attribute(kName, kNameString) {}
  Nl80211AttributeWiphyAntennaAvailRx(
      const Nl80211AttributeWiphyAntennaAvailRx&) = delete;
  Nl80211AttributeWiphyAntennaAvailRx& operator=(
      const Nl80211AttributeWiphyAntennaAvailRx&) = delete;
};

class Nl80211AttributeWiphyAntennaAvailTx
    : public net_base::NetlinkU32Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeWiphyAntennaAvailTx()
      : net_base::NetlinkU32Attribute(kName, kNameString) {}
  Nl80211AttributeWiphyAntennaAvailTx(
      const Nl80211AttributeWiphyAntennaAvailTx&) = delete;
  Nl80211AttributeWiphyAntennaAvailTx& operator=(
      const Nl80211AttributeWiphyAntennaAvailTx&) = delete;
};

class Nl80211AttributeWiphyAntennaRx : public net_base::NetlinkU32Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeWiphyAntennaRx()
      : net_base::NetlinkU32Attribute(kName, kNameString) {}
  Nl80211AttributeWiphyAntennaRx(const Nl80211AttributeWiphyAntennaRx&) =
      delete;
  Nl80211AttributeWiphyAntennaRx& operator=(
      const Nl80211AttributeWiphyAntennaRx&) = delete;
};

class Nl80211AttributeWiphyAntennaTx : public net_base::NetlinkU32Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeWiphyAntennaTx()
      : net_base::NetlinkU32Attribute(kName, kNameString) {}
  Nl80211AttributeWiphyAntennaTx(const Nl80211AttributeWiphyAntennaTx&) =
      delete;
  Nl80211AttributeWiphyAntennaTx& operator=(
      const Nl80211AttributeWiphyAntennaTx&) = delete;
};

class Nl80211AttributeWiphyFragThreshold
    : public net_base::NetlinkU32Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeWiphyFragThreshold()
      : net_base::NetlinkU32Attribute(kName, kNameString) {}
  Nl80211AttributeWiphyFragThreshold(
      const Nl80211AttributeWiphyFragThreshold&) = delete;
  Nl80211AttributeWiphyFragThreshold& operator=(
      const Nl80211AttributeWiphyFragThreshold&) = delete;
};

class Nl80211AttributeWiphyFreq : public net_base::NetlinkU32Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeWiphyFreq()
      : net_base::NetlinkU32Attribute(kName, kNameString) {}
  Nl80211AttributeWiphyFreq(const Nl80211AttributeWiphyFreq&) = delete;
  Nl80211AttributeWiphyFreq& operator=(const Nl80211AttributeWiphyFreq&) =
      delete;
};

class Nl80211AttributeChannelType : public net_base::NetlinkU32Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeChannelType()
      : net_base::NetlinkU32Attribute(kName, kNameString) {}
  Nl80211AttributeChannelType(const Nl80211AttributeChannelType&) = delete;
  Nl80211AttributeChannelType& operator=(const Nl80211AttributeChannelType&) =
      delete;
};

class Nl80211AttributeChannelWidth : public net_base::NetlinkU32Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeChannelWidth()
      : net_base::NetlinkU32Attribute(kName, kNameString) {}
  Nl80211AttributeChannelWidth(const Nl80211AttributeChannelWidth&) = delete;
  Nl80211AttributeChannelWidth& operator=(const Nl80211AttributeChannelWidth&) =
      delete;
};

class Nl80211AttributeCenterFreq1 : public net_base::NetlinkU32Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeCenterFreq1()
      : net_base::NetlinkU32Attribute(kName, kNameString) {}
  Nl80211AttributeCenterFreq1(const Nl80211AttributeCenterFreq1&) = delete;
  Nl80211AttributeCenterFreq1& operator=(const Nl80211AttributeCenterFreq1&) =
      delete;
};

class Nl80211AttributeCenterFreq2 : public net_base::NetlinkU32Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeCenterFreq2()
      : net_base::NetlinkU32Attribute(kName, kNameString) {}
  Nl80211AttributeCenterFreq2(const Nl80211AttributeCenterFreq2&) = delete;
  Nl80211AttributeCenterFreq2& operator=(const Nl80211AttributeCenterFreq2&) =
      delete;
};

class Nl80211AttributeWiphyRtsThreshold : public net_base::NetlinkU32Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeWiphyRtsThreshold()
      : net_base::NetlinkU32Attribute(kName, kNameString) {}
  Nl80211AttributeWiphyRtsThreshold(const Nl80211AttributeWiphyRtsThreshold&) =
      delete;
  Nl80211AttributeWiphyRtsThreshold& operator=(
      const Nl80211AttributeWiphyRtsThreshold&) = delete;
};

// U64.

class Nl80211AttributeCookie : public net_base::NetlinkU64Attribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeCookie()
      : net_base::NetlinkU64Attribute(kName, kNameString) {}
  Nl80211AttributeCookie(const Nl80211AttributeCookie&) = delete;
  Nl80211AttributeCookie& operator=(const Nl80211AttributeCookie&) = delete;
};

// Flag.

class Nl80211AttributeControlPortEthertype
    : public net_base::NetlinkFlagAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeControlPortEthertype()
      : net_base::NetlinkFlagAttribute(kName, kNameString) {}
  Nl80211AttributeControlPortEthertype(
      const Nl80211AttributeControlPortEthertype&) = delete;
  Nl80211AttributeControlPortEthertype& operator=(
      const Nl80211AttributeControlPortEthertype&) = delete;
};

class Nl80211AttributeDisconnectedByAp : public net_base::NetlinkFlagAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeDisconnectedByAp()
      : net_base::NetlinkFlagAttribute(kName, kNameString) {}
  Nl80211AttributeDisconnectedByAp(const Nl80211AttributeDisconnectedByAp&) =
      delete;
  Nl80211AttributeDisconnectedByAp& operator=(
      const Nl80211AttributeDisconnectedByAp&) = delete;
};

class Nl80211AttributeOffchannelTxOk : public net_base::NetlinkFlagAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeOffchannelTxOk()
      : net_base::NetlinkFlagAttribute(kName, kNameString) {}
  Nl80211AttributeOffchannelTxOk(const Nl80211AttributeOffchannelTxOk&) =
      delete;
  Nl80211AttributeOffchannelTxOk& operator=(
      const Nl80211AttributeOffchannelTxOk&) = delete;
};

class Nl80211AttributeRoamSupport : public net_base::NetlinkFlagAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeRoamSupport()
      : net_base::NetlinkFlagAttribute(kName, kNameString) {}
  Nl80211AttributeRoamSupport(const Nl80211AttributeRoamSupport&) = delete;
  Nl80211AttributeRoamSupport& operator=(const Nl80211AttributeRoamSupport&) =
      delete;
};

class Nl80211AttributeSupportApUapsd : public net_base::NetlinkFlagAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeSupportApUapsd()
      : net_base::NetlinkFlagAttribute(kName, kNameString) {}
  Nl80211AttributeSupportApUapsd(const Nl80211AttributeSupportApUapsd&) =
      delete;
  Nl80211AttributeSupportApUapsd& operator=(
      const Nl80211AttributeSupportApUapsd&) = delete;
};

class Nl80211AttributeSupportIbssRsn : public net_base::NetlinkFlagAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeSupportIbssRsn()
      : net_base::NetlinkFlagAttribute(kName, kNameString) {}
  Nl80211AttributeSupportIbssRsn(const Nl80211AttributeSupportIbssRsn&) =
      delete;
  Nl80211AttributeSupportIbssRsn& operator=(
      const Nl80211AttributeSupportIbssRsn&) = delete;
};

class Nl80211AttributeSupportMeshAuth : public net_base::NetlinkFlagAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeSupportMeshAuth()
      : net_base::NetlinkFlagAttribute(kName, kNameString) {}
  Nl80211AttributeSupportMeshAuth(const Nl80211AttributeSupportMeshAuth&) =
      delete;
  Nl80211AttributeSupportMeshAuth& operator=(
      const Nl80211AttributeSupportMeshAuth&) = delete;
};

class Nl80211AttributeTdlsExternalSetup
    : public net_base::NetlinkFlagAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeTdlsExternalSetup()
      : net_base::NetlinkFlagAttribute(kName, kNameString) {}
  Nl80211AttributeTdlsExternalSetup(const Nl80211AttributeTdlsExternalSetup&) =
      delete;
  Nl80211AttributeTdlsExternalSetup& operator=(
      const Nl80211AttributeTdlsExternalSetup&) = delete;
};

class Nl80211AttributeTdlsSupport : public net_base::NetlinkFlagAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeTdlsSupport()
      : net_base::NetlinkFlagAttribute(kName, kNameString) {}
  Nl80211AttributeTdlsSupport(const Nl80211AttributeTdlsSupport&) = delete;
  Nl80211AttributeTdlsSupport& operator=(const Nl80211AttributeTdlsSupport&) =
      delete;
};

class Nl80211AttributeTimedOut : public net_base::NetlinkFlagAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeTimedOut()
      : net_base::NetlinkFlagAttribute(kName, kNameString) {}
  Nl80211AttributeTimedOut(const Nl80211AttributeTimedOut&) = delete;
  Nl80211AttributeTimedOut& operator=(const Nl80211AttributeTimedOut&) = delete;
};

// String.

class Nl80211AttributeRegAlpha2 : public net_base::NetlinkStringAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeRegAlpha2()
      : net_base::NetlinkStringAttribute(kName, kNameString) {}
  Nl80211AttributeRegAlpha2(const Nl80211AttributeRegAlpha2&) = delete;
  Nl80211AttributeRegAlpha2& operator=(const Nl80211AttributeRegAlpha2&) =
      delete;
};

class Nl80211AttributeWiphyName : public net_base::NetlinkStringAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeWiphyName()
      : net_base::NetlinkStringAttribute(kName, kNameString) {}
  Nl80211AttributeWiphyName(const Nl80211AttributeWiphyName&) = delete;
  Nl80211AttributeWiphyName& operator=(const Nl80211AttributeWiphyName&) =
      delete;
};

// Nested.

class Nl80211AttributeBss : public net_base::NetlinkNestedAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  // These are sorted alphabetically.
  static const int kChallengeTextAttributeId;
  static const int kChannelsAttributeId;
  static const int kCountryInfoAttributeId;
  static const int kDSParameterSetAttributeId;
  static const int kErpAttributeId;
  static const int kExtendedRatesAttributeId;
  static const int kHtCapAttributeId;
  static const int kHtInfoAttributeId;
  static const int kMeshIdAttributeId;
  static const int kPowerCapabilityAttributeId;
  static const int kPowerConstraintAttributeId;
  static const int kRequestAttributeId;
  static const int kRsnAttributeId;
  static const int kSsidAttributeId;
  static const int kSupportedRatesAttributeId;
  static const int kTpcReportAttributeId;
  static const int kVendorSpecificAttributeId;
  static const int kVhtCapAttributeId;
  static const int kVhtInfoAttributeId;

  Nl80211AttributeBss();
  Nl80211AttributeBss(const Nl80211AttributeBss&) = delete;
  Nl80211AttributeBss& operator=(const Nl80211AttributeBss&) = delete;

 private:
  static bool ParseInformationElements(net_base::AttributeList* attribute_list,
                                       int id,
                                       const std::string& attribute_name,
                                       base::span<const uint8_t> data);
};

class Nl80211AttributeCqm : public net_base::NetlinkNestedAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeCqm();
  Nl80211AttributeCqm(const Nl80211AttributeCqm&) = delete;
  Nl80211AttributeCqm& operator=(const Nl80211AttributeCqm&) = delete;
};

class Nl80211AttributeRegRules : public net_base::NetlinkNestedAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeRegRules();
  Nl80211AttributeRegRules(const Nl80211AttributeRegRules&) = delete;
  Nl80211AttributeRegRules& operator=(const Nl80211AttributeRegRules&) = delete;
};

class Nl80211AttributeScanFrequencies
    : public net_base::NetlinkNestedAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeScanFrequencies();
  Nl80211AttributeScanFrequencies(const Nl80211AttributeScanFrequencies&) =
      delete;
  Nl80211AttributeScanFrequencies& operator=(
      const Nl80211AttributeScanFrequencies&) = delete;
};

class Nl80211AttributeScanSsids : public net_base::NetlinkNestedAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeScanSsids();
  Nl80211AttributeScanSsids(const Nl80211AttributeScanSsids&) = delete;
  Nl80211AttributeScanSsids& operator=(const Nl80211AttributeScanSsids&) =
      delete;
};

class Nl80211AttributeStaInfo : public net_base::NetlinkNestedAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeStaInfo();
  Nl80211AttributeStaInfo(const Nl80211AttributeStaInfo&) = delete;
  Nl80211AttributeStaInfo& operator=(const Nl80211AttributeStaInfo&) = delete;
};

class Nl80211AttributeMPathInfo : public net_base::NetlinkNestedAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeMPathInfo();
  Nl80211AttributeMPathInfo(const Nl80211AttributeMPathInfo&) = delete;
  Nl80211AttributeMPathInfo& operator=(const Nl80211AttributeMPathInfo&) =
      delete;
};

class Nl80211AttributeSupportedIftypes
    : public net_base::NetlinkNestedAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeSupportedIftypes();
  Nl80211AttributeSupportedIftypes(const Nl80211AttributeSupportedIftypes&) =
      delete;
  Nl80211AttributeSupportedIftypes& operator=(
      const Nl80211AttributeSupportedIftypes&) = delete;

 private:
  static bool ParseIfaceTypes(net_base::AttributeList* attribute_list,
                              int id,
                              const std::string& attribute_name,
                              base::span<const uint8_t> data);
};

class Nl80211AttributeWiphyBands : public net_base::NetlinkNestedAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeWiphyBands();
  Nl80211AttributeWiphyBands(const Nl80211AttributeWiphyBands&) = delete;
  Nl80211AttributeWiphyBands& operator=(const Nl80211AttributeWiphyBands&) =
      delete;
};

class Nl80211AttributeInterfaceCombinations
    : public net_base::NetlinkNestedAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeInterfaceCombinations();
  Nl80211AttributeInterfaceCombinations(
      const Nl80211AttributeInterfaceCombinations&) = delete;
  Nl80211AttributeInterfaceCombinations& operator=(
      const Nl80211AttributeInterfaceCombinations&) = delete;
};

class Nl80211AttributeWowlanTriggers : public net_base::NetlinkNestedAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  explicit Nl80211AttributeWowlanTriggers(
      const Nl80211Message::Context& context);
  Nl80211AttributeWowlanTriggers(const Nl80211AttributeWowlanTriggers&) =
      delete;
  Nl80211AttributeWowlanTriggers& operator=(
      const Nl80211AttributeWowlanTriggers&) = delete;
};

class Nl80211AttributeWowlanTriggersSupported
    : public net_base::NetlinkNestedAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeWowlanTriggersSupported();
  Nl80211AttributeWowlanTriggersSupported(
      const Nl80211AttributeWowlanTriggersSupported&) = delete;
  Nl80211AttributeWowlanTriggersSupported& operator=(
      const Nl80211AttributeWowlanTriggersSupported&) = delete;
};

// Raw.

class Nl80211AttributeCipherSuites : public net_base::NetlinkRawAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeCipherSuites()
      : net_base::NetlinkRawAttribute(kName, kNameString) {}
  Nl80211AttributeCipherSuites(const Nl80211AttributeCipherSuites&) = delete;
  Nl80211AttributeCipherSuites& operator=(const Nl80211AttributeCipherSuites&) =
      delete;
};

class Nl80211AttributeFrame : public net_base::NetlinkRawAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeFrame() : net_base::NetlinkRawAttribute(kName, kNameString) {}
  Nl80211AttributeFrame(const Nl80211AttributeFrame&) = delete;
  Nl80211AttributeFrame& operator=(const Nl80211AttributeFrame&) = delete;
};

class Nl80211AttributeHtCapabilityMask : public net_base::NetlinkRawAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeHtCapabilityMask()
      : net_base::NetlinkRawAttribute(kName, kNameString) {}
  Nl80211AttributeHtCapabilityMask(const Nl80211AttributeHtCapabilityMask&) =
      delete;
  Nl80211AttributeHtCapabilityMask& operator=(
      const Nl80211AttributeHtCapabilityMask&) = delete;
};

class Nl80211AttributeKeySeq : public net_base::NetlinkRawAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeKeySeq()
      : net_base::NetlinkRawAttribute(kName, kNameString) {}
  Nl80211AttributeKeySeq(const Nl80211AttributeKeySeq&) = delete;
  Nl80211AttributeKeySeq& operator=(const Nl80211AttributeKeySeq&) = delete;
};

class Nl80211AttributeMac : public net_base::NetlinkRawAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeMac() : net_base::NetlinkRawAttribute(kName, kNameString) {}
  Nl80211AttributeMac(const Nl80211AttributeMac&) = delete;
  Nl80211AttributeMac& operator=(const Nl80211AttributeMac&) = delete;

  bool ToString(std::string* value) const override;

  // Stringizes the MAC address found in 'arg'.  If there are problems (such
  // as a NULL |arg|), |value| is set to a bogus MAC address.
  static std::string StringFromMacAddress(const uint8_t* arg);
};

class Nl80211AttributeRespIe : public net_base::NetlinkRawAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeRespIe()
      : net_base::NetlinkRawAttribute(kName, kNameString) {}
  Nl80211AttributeRespIe(const Nl80211AttributeRespIe&) = delete;
  Nl80211AttributeRespIe& operator=(const Nl80211AttributeRespIe&) = delete;
};

class Nl80211AttributeSurveyInfo : public net_base::NetlinkNestedAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeSurveyInfo();
  Nl80211AttributeSurveyInfo(const Nl80211AttributeSurveyInfo&) = delete;
  Nl80211AttributeSurveyInfo& operator=(const Nl80211AttributeSurveyInfo&) =
      delete;
};

class Nl80211AttributeSupportedCommands
    : public net_base::NetlinkNestedAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeSupportedCommands();
  Nl80211AttributeSupportedCommands(const Nl80211AttributeSupportedCommands&) =
      delete;
  Nl80211AttributeSupportedCommands& operator=(
      const Nl80211AttributeSupportedCommands&) = delete;
};

class Nl80211AttributeWiphySelfManagedReg
    : public net_base::NetlinkFlagAttribute {
 public:
  static const int kName;
  static const char kNameString[];
  Nl80211AttributeWiphySelfManagedReg()
      : net_base::NetlinkFlagAttribute(kName, kNameString) {}
  Nl80211AttributeWiphySelfManagedReg(
      const Nl80211AttributeWiphySelfManagedReg&) = delete;
  Nl80211AttributeWiphySelfManagedReg& operator=(
      const Nl80211AttributeWiphySelfManagedReg&) = delete;
};

}  // namespace shill

#endif  // SHILL_WIFI_NL80211_ATTRIBUTE_H_
