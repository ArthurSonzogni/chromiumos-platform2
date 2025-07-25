// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/wifi_endpoint.h"

#include <linux/if_ether.h>

#include <algorithm>

#include <base/containers/contains.h>
#include <base/logging.h>
#include <base/notimplemented.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/net-base/mac_address.h>

#include "shill/control_interface.h"
#include "shill/logging.h"
#include "shill/metrics.h"
#include "shill/supplicant/supplicant_bss_proxy_interface.h"
#include "shill/supplicant/wpa_supplicant.h"
#include "shill/wifi/ieee80211.h"
#include "shill/wifi/wifi.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kWiFi;
}  // namespace Logging

namespace {

// Returns whether an 802.11 BSSID is likely to be owned by an Android device.
bool IsAndroidBSSID(net_base::MacAddress bssid) {
  constexpr uint8_t kAndroidBSSIDPrefix[] = {0x02, 0x1a, 0x11};
  return std::memcmp(bssid.data(), kAndroidBSSIDPrefix,
                     sizeof(kAndroidBSSIDPrefix)) == 0;
}

// Returns whether any of the organizationally unique identifiers in
// |oui_set| is commonly associated with IOS devices.
bool HasIosOui(const std::set<uint32_t>& oui_set) {
  constexpr uint32_t kIosOui = 0x0017f2;
  return oui_set.find(kIosOui) != oui_set.end();
}

void PackSecurity(const WiFiEndpoint::SecurityFlags& flags,
                  KeyValueStore* args) {
  Strings wpa, rsn;

  if (flags.rsn_8021x_wpa3) {
    rsn.push_back(std::string(WPASupplicant::kKeyManagementMethodPrefixEAP) +
                  std::string(WPASupplicant::kKeyManagementMethodSuiteB));
  }
  if (flags.rsn_sae) {
    rsn.push_back(WPASupplicant::kKeyManagementMethodSAE);
  }
  if (flags.rsn_8021x) {
    rsn.push_back(std::string("wpa2") +
                  WPASupplicant::kKeyManagementMethodSuffixEAP);
  }
  if (flags.rsn_psk) {
    rsn.push_back(std::string("wpa2") +
                  WPASupplicant::kKeyManagementMethodSuffixPSK);
  }
  if (flags.rsn_owe) {
    rsn.push_back(WPASupplicant::kKeyManagementMethodOWE);
  }
  if (flags.wpa_8021x) {
    wpa.push_back(std::string("wpa") +
                  WPASupplicant::kKeyManagementMethodSuffixEAP);
  }
  if (flags.wpa_psk) {
    wpa.push_back(std::string("wpa") +
                  WPASupplicant::kKeyManagementMethodSuffixPSK);
  }

  if (flags.privacy) {
    args->Set<bool>(WPASupplicant::kPropertyPrivacy, true);
  }

  if (!rsn.empty()) {
    KeyValueStore rsn_args;
    rsn_args.Set<Strings>(WPASupplicant::kSecurityMethodPropertyKeyManagement,
                          rsn);
    args->Set<KeyValueStore>(WPASupplicant::kPropertyRSN, rsn_args);
  }
  if (!wpa.empty()) {
    KeyValueStore wpa_args;
    wpa_args.Set<Strings>(WPASupplicant::kSecurityMethodPropertyKeyManagement,
                          wpa);
    args->Set<KeyValueStore>(WPASupplicant::kPropertyWPA, wpa_args);
  }
}

}  // namespace

WiFiEndpoint::WiFiEndpoint(ControlInterface* control_interface,
                           const WiFiRefPtr& device,
                           const RpcIdentifier& rpc_id,
                           const KeyValueStore& properties,
                           Metrics* metrics)
    : ssid_(properties.Get<std::vector<uint8_t>>(
          WPASupplicant::kBSSPropertySSID)),
      bssid_(net_base::MacAddress::CreateFromBytes(
                 properties.Get<std::vector<uint8_t>>(
                     WPASupplicant::kBSSPropertyBSSID))
                 .value()),
      ssid_hex_(base::HexEncode(ssid_.data(), ssid_.size())),
      frequency_(0),
      physical_mode_(Metrics::kWiFiNetworkPhyModeUndef),
      metrics_(metrics),
      control_interface_(control_interface),
      device_(device),
      rpc_id_(rpc_id) {
  signal_strength_ = properties.Get<int16_t>(WPASupplicant::kBSSPropertySignal);
  if (properties.Contains<uint32_t>(WPASupplicant::kBSSPropertyAge)) {
    last_seen_ =
        base::Time::Now() -
        base::Seconds(properties.Get<uint32_t>(WPASupplicant::kBSSPropertyAge));
  } else {
    last_seen_ = base::Time();
  }
  if (properties.Contains<uint16_t>(WPASupplicant::kBSSPropertyFrequency)) {
    frequency_ = properties.Get<uint16_t>(WPASupplicant::kBSSPropertyFrequency);
    if (frequency_ >= IEEE_80211::kWiFiMin6GHzFreq &&
        frequency_ <= IEEE_80211::kWiFiMax6GHzFreq) {
      supported_features_.band6ghz_support = true;
    }
  }

  Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
  if (!ParseIEs(properties, &phy_mode)) {
    phy_mode = DeterminePhyModeFromFrequency(properties, frequency_);
  }
  physical_mode_ = phy_mode;

  network_mode_ =
      ParseMode(properties.Get<std::string>(WPASupplicant::kBSSPropertyMode));
  // Result of ParseSecurity() depends on the contents of the information
  // elements so don't move this call prior to ParseIEs() call above.
  security_mode_ = ParseSecurity(properties, &security_flags_);
  has_rsn_property_ = security_flags_.rsn_8021x_wpa3 ||
                      security_flags_.rsn_8021x || security_flags_.rsn_owe ||
                      security_flags_.rsn_psk || security_flags_.rsn_sae;
  has_wpa_property_ = security_flags_.wpa_8021x || security_flags_.wpa_psk;

  ssid_string_ = std::string(ssid_.begin(), ssid_.end());
  WiFi::SanitizeSSID(&ssid_string_);

  CheckForTetheringSignature();
}

WiFiEndpoint::~WiFiEndpoint() = default;

void WiFiEndpoint::Start() {
  supplicant_bss_proxy_ =
      control_interface_->CreateSupplicantBSSProxy(this, rpc_id_);
}

void WiFiEndpoint::PropertiesChanged(const KeyValueStore& properties) {
  SLOG(2) << __func__;
  bool should_notify = false;
  if (properties.Contains<int16_t>(WPASupplicant::kBSSPropertySignal)) {
    signal_strength_ =
        properties.Get<int16_t>(WPASupplicant::kBSSPropertySignal);
    should_notify = true;
  }

  if (properties.Contains<uint32_t>(WPASupplicant::kBSSPropertyAge)) {
    last_seen_ =
        base::Time::Now() -
        base::Seconds(properties.Get<uint32_t>(WPASupplicant::kBSSPropertyAge));
    should_notify = true;
  }

  if (properties.Contains<std::string>(WPASupplicant::kBSSPropertyMode)) {
    auto new_mode =
        ParseMode(properties.Get<std::string>(WPASupplicant::kBSSPropertyMode));
    if (!new_mode.empty() && new_mode != network_mode_) {
      SLOG(2) << "WiFiEndpoint " << bssid_.ToString()
              << " mode change: " << network_mode_ << " -> " << new_mode;
      network_mode_ = new_mode;
      should_notify = true;
    }
  }

  if (properties.Contains<uint16_t>(WPASupplicant::kBSSPropertyFrequency)) {
    uint16_t new_frequency =
        properties.Get<uint16_t>(WPASupplicant::kBSSPropertyFrequency);
    if (new_frequency != frequency_) {
      if (new_frequency >= IEEE_80211::kWiFiMin6GHzFreq &&
          new_frequency <= IEEE_80211::kWiFiMax6GHzFreq) {
        supported_features_.band6ghz_support = true;
      }
      if (metrics_) {
        metrics_->NotifyApChannelSwitch(frequency_, new_frequency);
      }
      if (device_->GetCurrentEndpoint().get() == this) {
        SLOG(2) << "Current WiFiEndpoint " << bssid_.ToString()
                << " frequency change: " << frequency_ << " -> "
                << new_frequency;
      }
      frequency_ = new_frequency;
      should_notify = true;
    }
  }

  bool hs20_support_changed = false;
  if (properties.Contains<std::vector<uint8_t>>(
          WPASupplicant::kBSSPropertyIEs)) {
    Metrics::WiFiNetworkPhyMode new_phy_mode =
        Metrics::kWiFiNetworkPhyModeUndef;
    bool old_hs20_suppported = hs20_information().supported;
    if (!ParseIEs(properties, &new_phy_mode)) {
      new_phy_mode = DeterminePhyModeFromFrequency(properties, frequency_);
    }
    if (new_phy_mode != physical_mode_) {
      SLOG(2) << "WiFiEndpoint " << bssid_.ToString()
              << " phy mode change: " << physical_mode_ << " -> "
              << new_phy_mode;
      physical_mode_ = new_phy_mode;
      should_notify = true;
    }
    hs20_support_changed = old_hs20_suppported != hs20_information().supported;
  }

  WiFiSecurity::Mode new_security_mode =
      ParseSecurity(properties, &security_flags_);
  if (new_security_mode != security_mode()) {
    SLOG(2) << "WiFiEndpoint " << bssid_.ToString()
            << " security change: " << security_mode() << " -> "
            << new_security_mode;
    security_mode_ = new_security_mode;
    should_notify = true;
  }

  bool anqp_info_changed = false;
  if (properties.Contains<KeyValueStore>(WPASupplicant::kBSSPropertyANQP)) {
    anqp_info_changed = ParseANQPFields(properties);
  }

  if (should_notify) {
    device_->NotifyEndpointChanged(this);
  }
  if (hs20_support_changed) {
    device_->NotifyHS20InformationChanged(this);
  }
  if (anqp_info_changed) {
    device_->NotifyANQPInformationChanged(this);
  }
}

void WiFiEndpoint::UpdateSignalStrength(int16_t strength) {
  if (signal_strength_ == strength) {
    return;
  }

  SLOG(2) << __func__ << ": signal strength " << signal_strength_ << " -> "
          << strength;
  signal_strength_ = strength;
  device_->NotifyEndpointChanged(this);
}

void WiFiEndpoint::UpdateRPCPath(const RpcIdentifier& rpc_id) {
  if (rpc_id_ == rpc_id) {
    return;
  }

  SLOG(2) << __func__ << ": rpc id " << rpc_id_.value() << " -> "
          << rpc_id.value();
  rpc_id_ = rpc_id;
  Start();
}

std::map<std::string, std::string> WiFiEndpoint::GetVendorInformation() const {
  std::map<std::string, std::string> vendor_information;
  if (!vendor_information_.wps_manufacturer.empty()) {
    vendor_information[kVendorWPSManufacturerProperty] =
        vendor_information_.wps_manufacturer;
  }
  if (!vendor_information_.wps_model_name.empty()) {
    vendor_information[kVendorWPSModelNameProperty] =
        vendor_information_.wps_model_name;
  }
  if (!vendor_information_.wps_model_number.empty()) {
    vendor_information[kVendorWPSModelNumberProperty] =
        vendor_information_.wps_model_number;
  }
  if (!vendor_information_.wps_device_name.empty()) {
    vendor_information[kVendorWPSDeviceNameProperty] =
        vendor_information_.wps_device_name;
  }
  if (!vendor_information_.oui_set.empty()) {
    std::vector<std::string> oui_vector;
    for (auto oui : vendor_information_.oui_set) {
      oui_vector.push_back(base::StringPrintf("%02x-%02x-%02x", oui >> 16,
                                              (oui >> 8) & 0xff, oui & 0xff));
    }
    vendor_information[kVendorOUIListProperty] =
        base::JoinString(oui_vector, " ");
  }
  return vendor_information;
}

// static
uint32_t WiFiEndpoint::ModeStringToUint(const std::string& mode_string) {
  if (mode_string == kModeManaged) {
    return WPASupplicant::kNetworkModeInfrastructureInt;
  } else {
    NOTIMPLEMENTED() << "Shill does not support " << mode_string
                     << " mode at this time.";
  }
  return 0;
}

const std::vector<uint8_t>& WiFiEndpoint::ssid() const {
  return ssid_;
}

const std::string& WiFiEndpoint::ssid_string() const {
  return ssid_string_;
}

const std::string& WiFiEndpoint::ssid_hex() const {
  return ssid_hex_;
}

net_base::MacAddress WiFiEndpoint::bssid() const {
  return bssid_;
}

const std::vector<uint8_t>& WiFiEndpoint::owe_ssid() const {
  return owe_ssid_;
}

std::optional<net_base::MacAddress> WiFiEndpoint::owe_bssid() const {
  return owe_bssid_;
}

const std::string& WiFiEndpoint::country_code() const {
  return country_code_;
}

const WiFiRefPtr& WiFiEndpoint::device() const {
  return device_;
}

int16_t WiFiEndpoint::signal_strength() const {
  return signal_strength_;
}

base::Time WiFiEndpoint::last_seen() const {
  return last_seen_;
}

uint16_t WiFiEndpoint::frequency() const {
  return frequency_;
}

uint16_t WiFiEndpoint::physical_mode() const {
  return physical_mode_;
}

const std::string& WiFiEndpoint::network_mode() const {
  return network_mode_;
}

WiFiSecurity::Mode WiFiEndpoint::security_mode() const {
  return security_mode_;
}

bool WiFiEndpoint::has_rsn_property() const {
  return has_rsn_property_;
}

bool WiFiEndpoint::has_wpa_property() const {
  return has_wpa_property_;
}

// "PSK", as in WPA-PSK or WPA2-PSK.
bool WiFiEndpoint::has_psk_property() const {
  return security_flags_.rsn_psk || security_flags_.wpa_psk;
}

bool WiFiEndpoint::has_tethering_signature() const {
  return has_tethering_signature_;
}

bool WiFiEndpoint::has_rsn_owe() const {
  return security_flags_.rsn_owe;
}

const WiFiEndpoint::Ap80211krvSupport& WiFiEndpoint::krv_support() const {
  return supported_features_.krv_support;
}

const WiFiEndpoint::HS20Information& WiFiEndpoint::hs20_information() const {
  return supported_features_.hs20_information;
}

bool WiFiEndpoint::mbo_support() const {
  return supported_features_.mbo_support;
}

bool WiFiEndpoint::band6ghz_support() const {
  return supported_features_.band6ghz_support;
}

const WiFiEndpoint::QosSupport& WiFiEndpoint::qos_support() const {
  return supported_features_.qos_support;
}

bool WiFiEndpoint::anqp_support() const {
  return supported_features_.anqp_support;
}

const WiFiEndpoint::ANQPCapabilities& WiFiEndpoint::anqp_capabilities() const {
  return supported_features_.anqp_capabilities;
}

// static
WiFiEndpointRefPtr WiFiEndpoint::MakeOpenEndpoint(
    ControlInterface* control_interface,
    const WiFiRefPtr& wifi,
    const std::string& ssid,
    net_base::MacAddress bssid,
    const std::string& network_mode,
    uint16_t frequency,
    int16_t signal_dbm) {
  return MakeEndpoint(control_interface, wifi, ssid, bssid, network_mode,
                      frequency, signal_dbm, SecurityFlags());
}

// static
WiFiEndpointRefPtr WiFiEndpoint::MakeEndpoint(
    ControlInterface* control_interface,
    const WiFiRefPtr& wifi,
    const std::string& ssid,
    net_base::MacAddress bssid,
    const std::string& network_mode,
    uint16_t frequency,
    int16_t signal_dbm,
    const SecurityFlags& security_flags) {
  KeyValueStore args;

  auto ssid_bytes = std::vector<uint8_t>(ssid.begin(), ssid.end());
  args.Set<std::vector<uint8_t>>(WPASupplicant::kBSSPropertySSID, ssid_bytes);

  std::vector<uint8_t> bssid_bytes = bssid.ToBytes();
  args.Set<std::vector<uint8_t>>(WPASupplicant::kBSSPropertyBSSID, bssid_bytes);

  args.Set<int16_t>(WPASupplicant::kBSSPropertySignal, signal_dbm);
  args.Set<uint16_t>(WPASupplicant::kBSSPropertyFrequency, frequency);
  args.Set<std::string>(WPASupplicant::kBSSPropertyMode, network_mode);

  if (security_flags.trans_owe) {
    // The format of the Transitional OWE IE is:
    // - VendorElemID (1B) + len (1B)
    // - WiFiAliance OUIVendor (3B) + TransOWE OUIType (1B)
    // - BSSID (6B) + SSID len (1B) + SSID (SSID len)
    // For testing purposes the convention for SSID of the hidden BSS is that it
    // equals to the SSID of the public with "_hidden" suffix appended.  So when
    // configuring public just pass the SSID (and the suffix will be appended)
    // and when configuring hidden make sure the SSID ends with the suffix since
    // it will be stripped.
    // The convention for the value of BBSID is that the BSSID of the other BSS
    // in the pair can be obtained by flippipng bits (xor 0xFF) of the last
    // byte.
    constexpr std::string_view suffix{"_hidden"};
    std::vector<uint8_t> ie;
    // First let's handle SSID part (so the size of the IE is known).
    constexpr auto ssid_offset = 12;
    if (security_flags.rsn_owe) {  // hidden BSS (trans + using encryption)
      if (!base::EndsWith(ssid, suffix)) {
        LOG(ERROR) << "Make sure the SSID of the hidden OWE BSS ends "
                      "with \"_hidden\"";
        return nullptr;
      }
      ie.resize(ssid_offset + 1 + ssid_bytes.size() - suffix.size());
      auto ssid_it = ie.begin() + ssid_offset;
      // We strip suffix so encoded SSID length = SSID - suffix
      *ssid_it = ssid_bytes.size() - suffix.size();
      std::copy_n(ssid_bytes.begin(), *ssid_it, ssid_it + 1);
    } else {  // public BSS (trans + no encryption)
      ie.resize(ssid_offset + 1 + ssid_bytes.size() + suffix.size());
      auto ssid_it = ie.begin() + ssid_offset;
      // We add suffix so encoded SSID length = SSID + suffix
      *ssid_it = ssid_bytes.size() + suffix.size();
      ssid_it += 1;
      std::copy_n(ssid_bytes.begin(), ssid_bytes.size(), ssid_it);
      ssid_it += ssid_bytes.size();
      std::copy_n(suffix.begin(), suffix.size(), ssid_it);
    }
    ie[0] = IEEE_80211::kElemIdVendor;
    ie[1] = ie.size() - 2;
    // Big-endian packing of OUI
    ie[2] = IEEE_80211::kOUIVendorWiFiAlliance >> 16;
    ie[3] = IEEE_80211::kOUIVendorWiFiAlliance >> 8 & 0xFF;
    ie[4] = IEEE_80211::kOUIVendorWiFiAlliance & 0xFF;
    ie[5] = IEEE_80211::kOUITypeWiFiAllianceTransOWE;
    std::copy_n(ie.begin() + 6, bssid_bytes.size(), bssid_bytes.begin());
    ie[11] ^= 0xFF;

    args.Set<std::vector<uint8_t>>(WPASupplicant::kBSSPropertyIEs, ie);
  }

  PackSecurity(security_flags, &args);

  return new WiFiEndpoint(
      control_interface, wifi,
      RpcIdentifier(bssid.ToString()),  // |bssid| fakes an RPC ID
      args,
      nullptr);  // MakeEndpoint is only used for unit
                 // tests, where Metrics are not needed.
}

// static
std::string WiFiEndpoint::ParseMode(const std::string& mode_string) {
  if (mode_string == WPASupplicant::kNetworkModeInfrastructure) {
    return kModeManaged;
  } else if (mode_string == WPASupplicant::kNetworkModeAdHoc ||
             mode_string == WPASupplicant::kNetworkModeAccessPoint ||
             mode_string == WPASupplicant::kNetworkModeP2P ||
             mode_string == WPASupplicant::kNetworkModeMesh) {
    SLOG(2) << "Shill does not support mode: " << mode_string;
    return "";
  } else {
    LOG(ERROR) << "Unknown WiFi endpoint mode: " << mode_string;
    return "";
  }
}

// static
WiFiSecurity::Mode WiFiEndpoint::ParseSecurity(const KeyValueStore& properties,
                                               SecurityFlags* flags) {
  if (properties.Contains<KeyValueStore>(WPASupplicant::kPropertyRSN)) {
    KeyValueStore rsn_properties =
        properties.Get<KeyValueStore>(WPASupplicant::kPropertyRSN);
    std::set<KeyManagement> key_management;
    ParseKeyManagementMethods(rsn_properties, &key_management);
    flags->rsn_8021x_wpa3 =
        base::Contains(key_management, kKeyManagement802_1x_Wpa3);
    flags->rsn_8021x = base::Contains(key_management, kKeyManagement802_1x);
    flags->rsn_psk = base::Contains(key_management, kKeyManagementPSK);
    flags->rsn_sae = base::Contains(key_management, kKeyManagementSAE);
    flags->rsn_owe = base::Contains(key_management, kKeyManagementOWE);
  }

  if (properties.Contains<KeyValueStore>(WPASupplicant::kPropertyWPA)) {
    KeyValueStore rsn_properties =
        properties.Get<KeyValueStore>(WPASupplicant::kPropertyWPA);
    std::set<KeyManagement> key_management;
    ParseKeyManagementMethods(rsn_properties, &key_management);
    flags->wpa_8021x = base::Contains(key_management, kKeyManagement802_1x);
    flags->wpa_psk = base::Contains(key_management, kKeyManagementPSK);
  }

  if (properties.Contains<bool>(WPASupplicant::kPropertyPrivacy)) {
    flags->privacy = properties.Get<bool>(WPASupplicant::kPropertyPrivacy);
  }

  if (flags->rsn_8021x_wpa3) {
    return flags->rsn_8021x ? WiFiSecurity::kWpa2Wpa3Enterprise
                            : WiFiSecurity::kWpa3Enterprise;
  } else if (flags->rsn_8021x) {
    return flags->wpa_8021x ? WiFiSecurity::kWpaWpa2Enterprise
                            : WiFiSecurity::kWpa2Enterprise;
  } else if (flags->wpa_8021x) {
    return WiFiSecurity::kWpaEnterprise;
  } else if (flags->rsn_sae) {
    return flags->rsn_psk ? WiFiSecurity::kWpa2Wpa3 : WiFiSecurity::kWpa3;
  } else if (flags->rsn_psk) {
    return flags->wpa_psk ? WiFiSecurity::kWpaWpa2 : WiFiSecurity::kWpa2;
  } else if (flags->wpa_psk) {
    return WiFiSecurity::kWpa;
  } else if (flags->trans_owe) {
    // Both public and hidden endpoint of a transitional-OWE AP has "trans-owe"
    // security (even though only the hidden one is using encryption).  This way
    // they both match security of the WiFiService and you can tell difference
    // between them by comparing ssid() against SSID of WiFiService and/or
    // checking has_rsn_owe() flag.
    return WiFiSecurity::kTransOwe;
  } else if (flags->rsn_owe) {
    return WiFiSecurity::kOwe;
  } else if (flags->privacy) {
    return WiFiSecurity::kWep;
  } else {
    return WiFiSecurity::kNone;
  }
}

// static
void WiFiEndpoint::ParseKeyManagementMethods(
    const KeyValueStore& security_method_properties,
    std::set<KeyManagement>* key_management_methods) {
  if (!security_method_properties.Contains<Strings>(
          WPASupplicant::kSecurityMethodPropertyKeyManagement)) {
    return;
  }

  const std::vector<std::string> key_management_vec =
      security_method_properties.Get<Strings>(
          WPASupplicant::kSecurityMethodPropertyKeyManagement);

  for (const auto& method : key_management_vec) {
    if (base::EndsWith(method, WPASupplicant::kKeyManagementMethodSAE,
                       base::CompareCase::SENSITIVE) ||
        base::EndsWith(method, WPASupplicant::kKeyManagementMethodSAEEXTKEY,
                       base::CompareCase::SENSITIVE)) {
      // sae and ft-sae
      key_management_methods->insert(kKeyManagementSAE);
    } else if (method == WPASupplicant::kKeyManagementMethodOWE) {
      key_management_methods->insert(kKeyManagementOWE);
    } else if (base::StartsWith(method,
                                WPASupplicant::kKeyManagementMethodPrefixEAP) &&
               (base::Contains(method,
                               WPASupplicant::kKeyManagementMethodSuiteB) ||
                base::EndsWith(
                    method, WPASupplicant::kKeyManagementMethodSuffixEAPSHA256,
                    base::CompareCase::SENSITIVE))) {
      key_management_methods->insert(kKeyManagement802_1x_Wpa3);
    } else if (base::StartsWith(method,
                                WPASupplicant::kKeyManagementMethodPrefixEAP) ||
               base::EndsWith(method,
                              WPASupplicant::kKeyManagementMethodSuffixEAP,
                              base::CompareCase::SENSITIVE)) {
      key_management_methods->insert(kKeyManagement802_1x);
    } else if (base::EndsWith(method,
                              WPASupplicant::kKeyManagementMethodSuffixPSK,
                              base::CompareCase::SENSITIVE) ||
               base::EndsWith(
                   method, WPASupplicant::kKeyManagementMethodSuffixPSKSHA256,
                   base::CompareCase::SENSITIVE)) {
      key_management_methods->insert(kKeyManagementPSK);
    }
  }
}

// static
Metrics::WiFiNetworkPhyMode WiFiEndpoint::DeterminePhyModeFromFrequency(
    const KeyValueStore& properties, uint16_t frequency) {
  uint32_t max_rate = 0;
  if (properties.Contains<std::vector<uint32_t>>(
          WPASupplicant::kBSSPropertyRates)) {
    auto rates =
        properties.Get<std::vector<uint32_t>>(WPASupplicant::kBSSPropertyRates);
    if (!rates.empty()) {
      max_rate = rates[0];  // Rates are sorted in descending order
    }
  }

  Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
  if (frequency < 3000) {
    // 2.4GHz legacy, check for tx rate for 11b-only
    // (note 22M is valid)
    if (max_rate < 24000000) {
      phy_mode = Metrics::kWiFiNetworkPhyMode11b;
    } else {
      phy_mode = Metrics::kWiFiNetworkPhyMode11g;
    }
  } else {
    phy_mode = Metrics::kWiFiNetworkPhyMode11a;
  }

  return phy_mode;
}

bool WiFiEndpoint::ParseIEs(const KeyValueStore& properties,
                            Metrics::WiFiNetworkPhyMode* phy_mode) {
  if (!properties.Contains<std::vector<uint8_t>>(
          WPASupplicant::kBSSPropertyIEs)) {
    SLOG(2) << __func__ << ": No IE property in BSS.";
    return false;
  }
  auto ies =
      properties.Get<std::vector<uint8_t>>(WPASupplicant::kBSSPropertyIEs);

  // Format of an information element not of type 255:
  //    1       1          1 - 252
  // +------+--------+----------------+
  // | Type | Length | Data           |
  // +------+--------+----------------+
  //
  // Format of an information element of type 255:
  //    1       1          1         variable
  // +------+--------+----------+----------------+
  // | Type | Length | Ext Type | Data           |
  // +------+--------+----------+----------------+
  *phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
  bool found_ht = false;
  bool found_vht = false;
  bool found_he = false;
  bool found_eht = false;
  bool found_erp = false;
  bool found_country = false;
  bool found_power_constraint = false;
  bool found_rm_enabled_cap = false;
  bool found_mde = false;
  bool found_ft_cipher = false;
  int ie_len = 0;
  std::vector<uint8_t>::iterator it;
  for (it = ies.begin();
       std::distance(it, ies.end()) > 1;  // Ensure Length field is within PDU.
       it += ie_len) {
    ie_len = 2 + *(it + 1);
    if (std::distance(it, ies.end()) < ie_len) {
      LOG(ERROR) << __func__ << ": IE extends past containing PDU.";
      break;
    }
    switch (*it) {
      case IEEE_80211::kElemIdBSSMaxIdlePeriod:
        supported_features_.krv_support.bss_max_idle_period_supported = true;
        break;
      case IEEE_80211::kElemIdCountry:
        // Retrieve 2-character country code from the beginning of the element.
        if (ie_len >= 4) {
          std::string country(it + 2, it + 4);
          // ISO 3166 alpha-2 codes must be ASCII. There are probably other
          // restrictions we should honor too, but this is at least a minimum
          // coherence check.
          if (base::IsStringASCII(country)) {
            found_country = true;
            country_code_ = country;
          }
        }
        break;
      case IEEE_80211::kElemIdErp:
        found_erp = true;
        break;
      case IEEE_80211::kElemIdExtendedCap:
        ParseExtendedCapabilities(it + 2, it + ie_len, &supported_features_);
        break;
      case IEEE_80211::kElemIdHTCap:
      case IEEE_80211::kElemIdHTInfo:
        found_ht = true;
        break;
      case IEEE_80211::kElemIdMDE:
        found_mde = true;
        ParseMobilityDomainElement(it + 2, it + ie_len,
                                   &supported_features_.krv_support);
        break;
      case IEEE_80211::kElemIdPowerConstraint:
        found_power_constraint = true;
        break;
      case IEEE_80211::kElemIdRmEnabledCap:
        found_rm_enabled_cap = true;
        break;
      case IEEE_80211::kElemIdRSN:
        ParseWPACapabilities(it + 2, it + ie_len, &found_ft_cipher);
        break;
      case IEEE_80211::kElemIdVendor:
        ParseVendorIE(it + 2, it + ie_len);
        break;
      case IEEE_80211::kElemIdVHTCap:
      case IEEE_80211::kElemIdVHTOperation:
        found_vht = true;
        break;
      case IEEE_80211::kElemIdExt:
        if (std::distance(it, ies.end()) > 2) {
          switch (*(it + 2)) {
            case IEEE_80211::kElemIdExtEHTCap:
            case IEEE_80211::kElemIdExtEHTOperation:
              found_eht = true;
              break;
            case IEEE_80211::kElemIdExtHECap:
            case IEEE_80211::kElemIdExtHEOperation:
              found_he = true;
              break;
            default:
              SLOG(5) << __func__ << ": Element ID Extension " << *(it + 2)
                      << " not supported.";
              break;
          }
        }

        break;
      case IEEE_80211::kElemIdAdvertisementProtocols:
        // Format of an Advertisement Protocol element
        //    1       1
        // +------+--------+-----------------------------------+
        // | Type | Length | Advertisement Protocol Tuple list |
        // +------+--------+-----------------------------------+
        ParseAdvertisementProtocolList(it + 2, it + ie_len,
                                       &supported_features_.anqp_support);
        break;
      case IEEE_80211::kElemIdRNR:
        // Format of an Reduced Neighbor Report Element.
        //       1          1                 variable
        // +---------------------+--------------------------------+
        // | Element ID | Length | Neighbor AP Information Fields
        // +---------------------+--------------------------------+
        ParseRNR(it + 2, it + ie_len);
        break;
      default:
        SLOG(5) << __func__ << ": parsing of " << *it
                << " type IE not supported.";
    }
  }
  supported_features_.krv_support.neighbor_list_supported =
      found_country && found_power_constraint && found_rm_enabled_cap;
  supported_features_.krv_support.ota_ft_supported =
      found_mde && found_ft_cipher;
  supported_features_.krv_support.otds_ft_supported =
      supported_features_.krv_support.otds_ft_supported &&
      supported_features_.krv_support.ota_ft_supported;
  if (found_eht) {
    *phy_mode = Metrics::kWiFiNetworkPhyMode11be;
  } else if (found_he) {
    *phy_mode = Metrics::kWiFiNetworkPhyMode11ax;
  } else if (found_vht) {
    *phy_mode = Metrics::kWiFiNetworkPhyMode11ac;
  } else if (found_ht) {
    *phy_mode = Metrics::kWiFiNetworkPhyMode11n;
  } else if (found_erp) {
    *phy_mode = Metrics::kWiFiNetworkPhyMode11g;
  } else {
    return false;
  }
  return true;
}

// static
void WiFiEndpoint::ParseMobilityDomainElement(
    std::vector<uint8_t>::const_iterator ie,
    std::vector<uint8_t>::const_iterator end,
    Ap80211krvSupport* krv_support) {
  // Format of a Mobility Domain Element:
  //    2                1
  // +------+--------------------------+
  // | MDID | FT Capability and Policy |
  // +------+--------------------------+
  if (std::distance(ie, end) < IEEE_80211::kMDEFTCapabilitiesLen) {
    return;
  }

  // Advance past the MDID field and check the first bit of the capability
  // field, the Over-the-DS FT bit.
  ie += IEEE_80211::kMDEIDLen;
  krv_support->otds_ft_supported = (*ie & IEEE_80211::kMDEOTDSCapability) > 0;
}

// static
void WiFiEndpoint::ParseExtendedCapabilities(
    std::vector<uint8_t>::const_iterator ie,
    std::vector<uint8_t>::const_iterator end,
    SupportedFeatures* supported_features) {
  // Format of an Extended Capabilities Element:
  //        n
  // +--------------+
  // | Capabilities |
  // +--------------+
  // The Capabilities field is a bit field indicating the capabilities being
  // advertised by the STA transmitting the element. See section 8.4.2.29 of
  // the IEEE 802.11-2012 for a list of capabilities and their corresponding
  // bit positions.
  supported_features->krv_support.bss_transition_supported =
      GetExtendedCapability(ie, end, IEEE_80211::kExtendedCapOctet2,
                            IEEE_80211::kExtendedCapBit3);
  supported_features->krv_support.dms_supported = GetExtendedCapability(
      ie, end, IEEE_80211::kExtendedCapOctet3, IEEE_80211::kExtendedCapBit2);
  supported_features->qos_support.scs_supported = GetExtendedCapability(
      ie, end, IEEE_80211::kExtendedCapOctet6, IEEE_80211::kExtendedCapBit6);
  supported_features->qos_support.alternate_edca_supported =
      GetExtendedCapability(ie, end, IEEE_80211::kExtendedCapOctet7,
                            IEEE_80211::kExtendedCapBit0);
  supported_features->qos_support.mscs_supported = GetExtendedCapability(
      ie, end, IEEE_80211::kExtendedCapOctet10, IEEE_80211::kExtendedCapBit5);
}

// static
bool WiFiEndpoint::GetExtendedCapability(
    std::vector<uint8_t>::const_iterator ie,
    std::vector<uint8_t>::const_iterator end,
    IEEE_80211::ExtendedCapOctet octet,
    uint8_t bit) {
  // According to IEEE802.11-2020 (section 9.4.2.26) if fewer bits are received
  // in an Extended Capabilities field, the rest of the Extended Capabilities
  // field bits are assumed to be zero.
  if (std::distance(ie, end) < octet + 1) {
    return false;
  }
  return (*(ie + octet) & bit) != 0;
}

// static
void WiFiEndpoint::ParseWPACapabilities(
    std::vector<uint8_t>::const_iterator ie,
    std::vector<uint8_t>::const_iterator end,
    bool* found_ft_cipher) {
  // Format of an RSN Information Element:
  //    2             4
  // +------+--------------------+
  // | Type | Group Cipher Suite |
  // +------+--------------------+
  //             2             4 * pairwise count
  // +-----------------------+---------------------+
  // | Pairwise Cipher Count | Pairwise Ciphers... |
  // +-----------------------+---------------------+
  //             2             4 * authkey count
  // +-----------------------+---------------------+
  // | AuthKey Suite Count   | AuthKey Suites...   |
  // +-----------------------+---------------------+
  //          2
  // +------------------+
  // | RSN Capabilities |
  // +------------------+
  //          2            16 * pmkid count
  // +------------------+-------------------+
  // |   PMKID Count    |      PMKIDs...    |
  // +------------------+-------------------+
  //          4
  // +-------------------------------+
  // | Group Management Cipher Suite |
  // +-------------------------------+
  if (std::distance(ie, end) < IEEE_80211::kRSNIECipherCountOffset) {
    return;
  }
  ie += IEEE_80211::kRSNIECipherCountOffset;

  // Advance past the pairwise and authkey ciphers.  Each is a little-endian
  // cipher count followed by n * cipher_selector.
  for (int i = 0; i < IEEE_80211::kRSNIENumCiphers; ++i) {
    // Retrieve a little-endian cipher count.
    if (std::distance(ie, end) < IEEE_80211::kRSNIECipherCountLen) {
      return;
    }
    uint16_t cipher_count = *ie | (*(ie + 1) << 8);

    int skip_length = IEEE_80211::kRSNIECipherCountLen +
                      cipher_count * IEEE_80211::kRSNIESelectorLen;
    if (std::distance(ie, end) < skip_length) {
      return;
    }

    if (i == IEEE_80211::kRSNIEAuthKeyCiphers && cipher_count > 0 &&
        found_ft_cipher) {
      // Find the AuthKey Suite List and check for matches to Fast Transition
      // ciphers.
      std::vector<uint32_t> akm_suite_list(cipher_count, 0);
      std::memcpy(&akm_suite_list[0], &*(ie + IEEE_80211::kRSNIECipherCountLen),
                  cipher_count * IEEE_80211::kRSNIESelectorLen);
      for (uint16_t i = 0; i < cipher_count; i++) {
        uint32_t suite = akm_suite_list[i];
        if (suite == IEEE_80211::kRSNAuthType8021XFT ||
            suite == IEEE_80211::kRSNAuthTypePSKFT ||
            suite == IEEE_80211::kRSNAuthTypeSAEFT) {
          *found_ft_cipher = true;
          break;
        }
      }
    }

    // Skip over the cipher selectors.
    ie += skip_length;
  }
}

void WiFiEndpoint::ParseVendorIE(std::vector<uint8_t>::const_iterator ie,
                                 std::vector<uint8_t>::const_iterator end) {
  // Format of an vendor-specific information element (with type
  // and length field for the IE removed by the caller):
  //        3           1       1 - 248
  // +------------+----------+----------------+
  // | OUI        | OUI Type | Data           |
  // +------------+----------+----------------+
  if (std::distance(ie, end) < 4) {
    LOG(WARNING) << __func__ << ": no room in IE for OUI and type field.";
    return;
  }
  uint32_t oui = (*ie << 16) | (*(ie + 1) << 8) | *(ie + 2);
  uint8_t oui_type = *(ie + 3);
  ie += 4;

  if (oui != IEEE_80211::kOUIVendorEpigram &&
      oui != IEEE_80211::kOUIVendorMicrosoft) {
    vendor_information_.oui_set.insert(oui);
  }

  if (oui == IEEE_80211::kOUIVendorMicrosoft &&
      oui_type == IEEE_80211::kOUIMicrosoftWPS) {
    // Format of a WPS data element:
    //    2       2
    // +------+--------+----------------+
    // | Type | Length | Data           |
    // +------+--------+----------------+
    while (std::distance(ie, end) >= 4) {
      int element_type = (*ie << 8) | *(ie + 1);
      int element_length = (*(ie + 2) << 8) | *(ie + 3);
      ie += 4;
      if (std::distance(ie, end) < element_length) {
        LOG(WARNING) << __func__
                     << ": WPS element extends past containing PDU.";
        break;
      }
      std::string s(ie, ie + element_length);
      if (base::IsStringASCII(s)) {
        switch (element_type) {
          case IEEE_80211::kWPSElementManufacturer:
            vendor_information_.wps_manufacturer = s;
            break;
          case IEEE_80211::kWPSElementModelName:
            vendor_information_.wps_model_name = s;
            break;
          case IEEE_80211::kWPSElementModelNumber:
            vendor_information_.wps_model_number = s;
            break;
          case IEEE_80211::kWPSElementDeviceName:
            vendor_information_.wps_device_name = s;
            break;
        }
      }
      ie += element_length;
    }
  } else if (oui == IEEE_80211::kOUIVendorWiFiAlliance &&
             oui_type == IEEE_80211::kOUITypeWiFiAllianceHS20Indicator) {
    // Format of a Hotspot 2.0 Indication data element:
    //            1                  2             2
    // +-----------------------+-----------+----------------+
    // | Hotspot Configuration | PPS MO ID | ANQP Domain ID |
    // +-----------------------+-----------+----------------+
    //                          (optional)     (optional)
    //
    // Format of Hotspot Configuration Field (bits):
    //         4              1               1
    // +----------------+----------+------------------------+
    // | Version Number | Reserved | ANQP Domain ID present |
    // +----------------+----------+------------------------+
    //          1                 1
    // +-------------------+---------------+
    // | PPS MO ID Present | DGAF Disabled |
    // +-------------------+---------------+
    if (std::distance(ie, end) < 1) {
      LOG(WARNING) << __func__ << ": no room in Hotspot 2.0 indication element"
                   << " for Hotspot Configuration field.";
      return;
    }
    supported_features_.hs20_information.supported = true;
    // Parse out the version number from the Hotspot Configuration field.
    supported_features_.hs20_information.version = (*ie & 0xf0) >> 4;
  } else if (oui == IEEE_80211::kOUIVendorWiFiAlliance &&
             oui_type == IEEE_80211::kOUITypeWiFiAllianceMBO) {
    supported_features_.mbo_support = true;
  } else if (oui == IEEE_80211::kOUIVendorWiFiAlliance &&
             oui_type == IEEE_80211::kOUITypeWiFiAllianceTransOWE) {
    if (std::distance(ie, end) < ETH_ALEN + 1) {
      LOG(WARNING) << __func__ << ": not enough data in OWE element";
      return;
    }
    security_flags_.trans_owe = true;
    net_base::MacAddress::DataType owe_bssid_bytes;
    std::copy_n(ie, owe_bssid_bytes.size(), owe_bssid_bytes.begin());
    owe_bssid_ = net_base::MacAddress(owe_bssid_bytes);
    ie += ETH_ALEN;
    uint8_t ssid_len = *ie++;
    if (std::distance(ie, end) < ssid_len) {
      LOG(WARNING) << __func__ << ": data for SSID too short";
      ssid_len = std::distance(ie, end);
    }
    if (ssid_len != 0) {
      owe_ssid_.resize(ssid_len);
      std::copy_n(ie, ssid_len, owe_ssid_.begin());
    }
  } else if (oui == IEEE_80211::kOUIVendorCiscoAironet &&
             oui_type == IEEE_80211::kOUITypeCiscoExtendedCapabilitiesIE) {
    if (std::distance(ie, end) < 1) {
      LOG(WARNING) << __func__ << ": Cisco Extended Capabilities IE too short";
      return;
    }
    supported_features_.krv_support.adaptive_ft_supported =
        *ie & IEEE_80211::kCiscoExtendedCapabilitiesAdaptiveFT;
  }
}

void WiFiEndpoint::ParseAdvertisementProtocolList(
    std::vector<uint8_t>::const_iterator ie,
    std::vector<uint8_t>::const_iterator end,
    bool* anqp_support) {
  if (std::distance(ie, end) < 2) {
    LOG(WARNING) << __func__
                 << ": no room in IE for Advertisement protocol tuple.";
    return;
  }

  // Format of an Advertisement Protocol tuple.
  //    1                     variable
  // +---------------------+---------------------------+
  // | Query Response Info | Advertisement Protocol ID |
  // +---------------------+---------------------------+

  while (std::distance(ie, end) >= 2) {
    // Skip the Query Response Info.
    ie++;
    // Advertisement Protocol ID.
    switch (*ie) {
      case IEEE_80211::kAdvProtANQP:
        *anqp_support = true;
        ie++;
        break;
      case IEEE_80211::kAdvProtVendorSpecific: {
        // Format of a Vendor Specific element.
        //    1            1
        // +------------+--------+-----+-----------------+
        // | Element ID | Length | OUI | Vendor specific |
        // +------------+--------+-----+-----------------+
        if (std::distance(ie, end) < 2) {
          LOG(WARNING) << __func__
                       << "No room for vendor specific element length.";
          break;
        }
        uint8_t element_len = *(ie + 1);
        if (std::distance(ie, end) < element_len) {
          LOG(WARNING) << __func__
                       << ": Advertisement protocol element extends past "
                          "containing PDU.";
          break;
        }
        // Skip the vendor specific element.
        ie += element_len;
        break;
      }
      case IEEE_80211::kAdvProtMisIS:
      case IEEE_80211::kAdvProtMisCESCD:
      case IEEE_80211::kAdvProtEAS:
      case IEEE_80211::kAdvProtRLQP:
      default:
        // Except for Vendor specific element, elements ID are always 1 byte
        // long (IEEE802.11-2020 - 9.4.2.92). Skip unsupported or reserved
        // identifiers.
        ie++;
    }
  }
}

void WiFiEndpoint::ParseRNR(std::vector<uint8_t>::const_iterator ie,
                            std::vector<uint8_t>::const_iterator end) {
  if (std::distance(ie, end) < 5) {
    LOG(WARNING) << __func__
                 << ": no room in IE for Neighbor AP Information Field.";
    return;
  }

  // Format of a Neighbor AP Information Field.
  //              2                     1                 1
  // +-------------------------+-----------------+----------------+
  // | TBTT Information Header | Operating Class | Channel Number
  // +-------------------------+-----------------+----------------+
  //         variable
  // +----------------------+
  // | TBTT Information Set
  // +----------------------+
  while (std::distance(ie, end) >= 5) {
    // Format of TBTT Information Header in bits.
    //              2                           1                 1
    // +-----------------------------+-----------------------+----------+
    // | TBTT Information Field Type | Filtered Neighbor AP | Reserved
    // +-----------------------------+-----------------------+----------+
    //             4                         8
    // +------------------------+-------------------------+
    // | TBTT Information Count | TBTT Information Length
    // +------------------------+-------------------------+
    uint8_t tbtt_count = (*ie & 0x0f) + 1;
    uint8_t tbtt_info_len = *(ie + 1);
    ie += 2;
    // Operating Class.
    if ((*ie) >= IEEE_80211::kWiFiMin6GHzOpClass &&
        (*ie) <= IEEE_80211::kWiFiMax6GHzOpClass) {
      // Found a neighboring AP on 6GHz but need to confirm it is also
      // co-located.
      for (int i = 0; i < tbtt_count; ++i) {
        // Format of TBTT Information field.
        //              1                   0 or 6               0 or 4
        // +-------------------------+------------------+----------------------+
        // | Neighbor AP TBTT Offset | BSSID (optional) | Short SSID (optional)
        // +-------------------------+------------------+----------------------+
        //       0 or 1         0 or 1
        // +----------------+-----------+
        // | BSS Parameters | 20MHz PSD
        // +----------------+-----------+
        auto bss_param_ie = ie + 2;
        // Neighbor AP TBTT Offset is always present. Whether optional IEs are
        // present depends on the TBTT Information Length.
        switch (tbtt_info_len) {
          // Neighbor AP TBTT Offset + BSS Parameters
          case 2:
            bss_param_ie += (tbtt_info_len * i) + 1;
            break;
          // Neighbor AP TBTT Offset + Short SSID + BSS Parameters
          case 6:
            bss_param_ie += (tbtt_info_len * i) + 5;
            break;
          // Neighbor AP TBTT Offset + BSSID + BSS Parameters
          case 8:
          // Neighbor AP TBTT Offset + BSSID + BSS Parameters + 20MHz PSD
          case 9:
            bss_param_ie += (tbtt_info_len * i) + 7;
            break;
          // Neighbor AP TBTT Offset + BSSID + Short SSID + BSS Parameters +
          // 20MHz PSD
          case 12 ... 255:
            bss_param_ie += (tbtt_info_len * i) + 11;
            break;
          default:
            continue;
        }
        // Format of BSS Parameters Information field in bits.
        //         1               1              1                 1
        // +-----------------+-----------+----------------+--------------------+
        // | OCT Recommended | Same SSID | Multiple BSSID | Transmitted BSSID
        // +-----------------+-----------+----------------+--------------------+
        //                       1                              1
        // +---------------------------------------------+------------+
        // | Member of ESS With 2.4GHz/5GHz Colocated AP | UPR active
        // +---------------------------------------------+------------+
        //         1             1
        // +---------------+-----------+
        // | Co-located AP | Reserved
        // +---------------+-----------+
        if (std::distance(bss_param_ie, end) < 1) {
          LOG(WARNING) << __func__
                       << ": no room in IE for BSS Parameters field.";
          return;
        }
        if (*bss_param_ie & 0x40) {
          supported_features_.band6ghz_support = true;
          return;
        }
      }
    }
    ie += (tbtt_info_len * tbtt_count) + 2;
  }
  return;
}

bool WiFiEndpoint::ParseANQPFields(const KeyValueStore& properties) {
  if (!properties.Contains<KeyValueStore>(WPASupplicant::kBSSPropertyANQP)) {
    SLOG(2) << __func__ << ": No ANQP properties in BSS.";
    return false;
  }
  auto anqp = properties.Get<KeyValueStore>(WPASupplicant::kBSSPropertyANQP);
  bool has_changed = false;

  if (anqp.Contains<std::vector<uint8_t>>(
          WPASupplicant::kANQPChangePropertyCapabilityList)) {
    std::vector<uint8_t> ies = anqp.Get<std::vector<uint8_t>>(
        WPASupplicant::kANQPChangePropertyCapabilityList);
    has_changed = ParseANQPCapabilityList(
        ies.begin(), ies.end(), &supported_features_.anqp_capabilities);
  }

  return has_changed;
}

bool WiFiEndpoint::ParseANQPCapabilityList(
    std::vector<uint8_t>::const_iterator ie,
    std::vector<uint8_t>::const_iterator end,
    ANQPCapabilities* anqp_capabilities) {
  // Format of the capability list as described in IEEE 802.11-2020 9.4.5.3.
  //    2            2 or 0            variable
  // +----------- +------------+-----+---------------+-----+
  // | ANQP       | ANQP Cap   | ... | ANQP Vendor   | ... |
  // | Capability | (optional) |     | Specific list |     |
  // +------------+------------+-----+---------------+-----+
  //
  // The capability list always contains the ANQP Capability list
  // elements, and may contain more.
  if (std::distance(ie, end) < 2) {
    LOG(WARNING) << __func__ << ": No room for ANQP capabilities.";
    return false;
  }

  bool found_venue_name = false;
  bool found_network_auth_type = false;
  bool found_address_type_availability = false;
  bool found_venue_url = false;

  // The Capability List element must always be included.
  uint16_t cap = *ie | (*(ie + 1) << 8);
  ie += 2;
  if (cap != IEEE_80211::kANQPCapabilityList) {
    LOG(WARNING) << __func__ << ": No ANQP Capability List element.";
    return false;
  }
  // Other capabilities are optional.
  while (std::distance(ie, end) >= 2) {
    cap = *ie | (*(ie + 1) << 8);
    ie += 2;
    switch (cap) {
      case IEEE_80211::kANQPVenueName:
        found_venue_name = true;
        break;
      case IEEE_80211::kANQPNetworkAuthenticationType:
        found_network_auth_type = true;
        break;
      case IEEE_80211::kANQPAddressTypeAvailability:
        found_address_type_availability = true;
        break;
      case IEEE_80211::kANQPVenueURL:
        found_venue_url = true;
        break;
      case IEEE_80211::kANQPVendorSpecificList: {
        // Format of a vendor specific element as described in IEEE802.11-2020
        // 9.4.5.8.
        //    2         2
        // +---------+--------+-----+-----------------+
        // | Info ID | Length | OUI | Vendor specific |
        // +---------+--------+-----+-----------------+
        //
        // Info ID was already parsed in |cap|, parse the length to skip the
        // element.
        if (std::distance(ie, end) < 2) {
          LOG(WARNING) << __func__
                       << ": No room for vendor specific element length.";
          return false;
        }
        int vendor_ie_len = *ie | (*(ie + 1) << 8);
        if (std::distance(ie, end) < vendor_ie_len) {
          LOG(WARNING) << __func__ << ": No room for vendor specific element.";
          return false;
        }
        // Skip the vendor vpecific element.
        ie += vendor_ie_len;
        break;
      }
      default:
        // All others capabilities are ignored for now.
        break;
    }
  }

  // Parsing was done successfully, update the endpoint ANQP capabilities. Note
  // that the parsing cannot be successful if the capability list identifier is
  // not present.
  anqp_capabilities->capability_list = true;
  anqp_capabilities->venue_name = found_venue_name;
  anqp_capabilities->address_type_availability =
      found_address_type_availability;
  anqp_capabilities->network_auth_type = found_network_auth_type;
  anqp_capabilities->venue_url = found_venue_url;

  return true;
}

void WiFiEndpoint::CheckForTetheringSignature() {
  has_tethering_signature_ =
      IsAndroidBSSID(bssid_) || (bssid_.IsLocallyAdministered() &&
                                 HasIosOui(vendor_information_.oui_set));
}

Metrics::WiFiConnectionAttemptInfo::ApSupportedFeatures
WiFiEndpoint::ToApSupportedFeatures() const {
  Metrics::WiFiConnectionAttemptInfo::ApSupportedFeatures ap_features;
  ap_features.krv_info.neighbor_list_supported =
      krv_support().neighbor_list_supported;
  ap_features.krv_info.ota_ft_supported = krv_support().ota_ft_supported;
  ap_features.krv_info.otds_ft_supported = krv_support().otds_ft_supported;
  ap_features.krv_info.dms_supported = krv_support().dms_supported;
  ap_features.krv_info.bss_max_idle_period_supported =
      krv_support().bss_max_idle_period_supported;
  ap_features.krv_info.bss_transition_supported =
      krv_support().bss_transition_supported;
  ap_features.hs20_info.supported = hs20_information().supported;
  ap_features.hs20_info.version = hs20_information().version;
  ap_features.mbo_supported = mbo_support();
  return ap_features;
}

}  // namespace shill
