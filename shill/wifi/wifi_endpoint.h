// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_WIFI_ENDPOINT_H_
#define SHILL_WIFI_WIFI_ENDPOINT_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/memory/ref_counted.h>
#include <base/time/time.h>
#include <chromeos/net-base/mac_address.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/metrics.h"
#include "shill/refptr_types.h"
#include "shill/store/key_value_store.h"
#include "shill/wifi/wifi_security.h"

namespace shill {

class ControlInterface;
class SupplicantBSSProxyInterface;

class WiFiEndpoint : public base::RefCounted<WiFiEndpoint> {
 public:
  struct SecurityFlags {
    bool rsn_8021x_wpa3 = false;
    bool rsn_8021x = false;
    bool rsn_owe = false;
    bool rsn_psk = false;
    bool rsn_sae = false;
    bool trans_owe = false;
    bool wpa_8021x = false;
    bool wpa_psk = false;
    bool privacy = false;
  };
  struct VendorInformation {
    std::string wps_manufacturer;
    std::string wps_model_name;
    std::string wps_model_number;
    std::string wps_device_name;
    std::set<uint32_t> oui_set;
  };
  struct Ap80211krvSupport {
    bool neighbor_list_supported = false;
    bool ota_ft_supported = false;
    bool otds_ft_supported = false;
    bool adaptive_ft_supported = false;
    bool dms_supported = false;
    bool bss_max_idle_period_supported = false;
    bool bss_transition_supported = false;
  };
  struct HS20Information {
    bool supported = false;
    int version = 0;
  };
  struct QosSupport {
    bool scs_supported = false;
    bool mscs_supported = false;
    bool alternate_edca_supported = false;
  };
  // Subset of ANQP capabilities we're interested in.
  struct ANQPCapabilities {
    bool capability_list = false;
    bool venue_name = false;
    bool network_auth_type = false;
    bool address_type_availability = false;
    bool venue_url = false;
  };
  struct SupportedFeatures {
    Ap80211krvSupport krv_support;
    HS20Information hs20_information;
    bool mbo_support = false;
    QosSupport qos_support;
    bool anqp_support = false;
    ANQPCapabilities anqp_capabilities;
    bool band6ghz_support = false;
  };
  WiFiEndpoint(ControlInterface* control_interface,
               const WiFiRefPtr& device,
               const RpcIdentifier& rpc_id,
               const KeyValueStore& properties,
               Metrics* metrics);
  WiFiEndpoint(const WiFiEndpoint&) = delete;
  WiFiEndpoint& operator=(const WiFiEndpoint&) = delete;

  virtual ~WiFiEndpoint();

  // Set up RPC channel. Broken out from the ctor, so that WiFi can
  // look over the Endpoint details before commiting to setting up
  // RPC.
  virtual void Start();

  // Called by SupplicantBSSProxy, in response to events from
  // wpa_supplicant.
  void PropertiesChanged(const KeyValueStore& properties);

  // Called by WiFi when the path to an endpoint changes.
  void UpdateRPCPath(const RpcIdentifier& rpc_id);

  // Called by WiFi when it polls for signal strength from the kernel.
  void UpdateSignalStrength(int16_t strength);

  // Maps mode strings from flimflam's nomenclature, as defined
  // in chromeos/dbus/service_constants.h, to uints used by supplicant
  static uint32_t ModeStringToUint(const std::string& mode_string);

  // Returns a stringmap containing information gleaned about the
  // vendor of this AP.
  std::map<std::string, std::string> GetVendorInformation() const;

  Metrics::WiFiConnectionAttemptInfo::ApSupportedFeatures
  ToApSupportedFeatures() const;

  const std::vector<uint8_t>& ssid() const;
  const std::string& ssid_string() const;
  const std::string& ssid_hex() const;
  net_base::MacAddress bssid() const;
  const std::string& country_code() const;
  const WiFiRefPtr& device() const;
  int16_t signal_strength() const;
  base::Time last_seen() const;
  uint16_t frequency() const;
  uint16_t physical_mode() const;
  const std::string& network_mode() const;
  WiFiSecurity::Mode security_mode() const;
  bool has_rsn_property() const;
  bool has_wpa_property() const;
  bool has_psk_property() const;
  bool has_tethering_signature() const;
  bool has_rsn_owe() const;
  const Ap80211krvSupport& krv_support() const;
  const HS20Information& hs20_information() const;
  bool mbo_support() const;
  bool band6ghz_support() const;
  const QosSupport& qos_support() const;
  bool anqp_support() const;
  const ANQPCapabilities& anqp_capabilities() const;
  // Transitional mode OWE AP consists of two BSSes pointing to each other via
  // IEs in the beacon. The SSID and BSSID is included in these IEs for
  // identification and these two functions return them. For endpoints not
  // belonging to the transitional mode OWE AP returned values are empty.
  const std::vector<uint8_t>& owe_ssid() const;
  std::optional<net_base::MacAddress> owe_bssid() const;

 private:
  friend class ManagerTest;  // for MakeOpenEndpoint
  friend class WiFiANQPFuzz;
  friend class WiFiEndpointTest;
  friend class WiFiIEsFuzz;
  friend class WiFiObjectTest;    // for MakeOpenEndpoint
  friend class WiFiProviderTest;  // for MakeOpenEndpoint
  friend class WiFiServiceTest;   // for MakeOpenEndpoint
  // For DeterminePhyModeFromFrequency
  FRIEND_TEST(WiFiEndpointTest, DeterminePhyModeFromFrequency);
  FRIEND_TEST(WiFiEndpointTest, ParseIEs);
  FRIEND_TEST(WiFiEndpointTest, ParseVendorIEs);
  FRIEND_TEST(WiFiEndpointTest, ParseWPACapabilities);
  FRIEND_TEST(WiFiEndpointTest, ParseCountryCode);
  FRIEND_TEST(WiFiEndpointTest, ParseAdvertisementProtocolList);
  FRIEND_TEST(WiFiEndpointTest, ParseANQPFields);
  // These test cases need access to the KeyManagement enum.
  FRIEND_TEST(WiFiEndpointTest, ParseKeyManagementMethodsOWE);
  FRIEND_TEST(WiFiEndpointTest, ParseKeyManagementMethodsEAP);
  FRIEND_TEST(WiFiEndpointTest, ParseKeyManagementMethodsPSK);
  FRIEND_TEST(WiFiEndpointTest, ParseKeyManagementMethodsEAPAndPSK);
  FRIEND_TEST(WiFiEndpointTest, HasTetheringSignature);  // vendor_information_
  FRIEND_TEST(WiFiProviderTest, OnEndpointUpdated);
  FRIEND_TEST(WiFiServiceTest, GetTethering);
  FRIEND_TEST(WiFiServiceUpdateFromEndpointsTest, EndpointModified);
  // for physical_mode_
  FRIEND_TEST(WiFiServiceUpdateFromEndpointsTest, PhysicalMode);
  // for supported_features_
  FRIEND_TEST(WiFiEndpointTest, Ap80211krvSupported);
  FRIEND_TEST(WiFiEndpointTest, PropertiesChangedFrequency6GHz);
  FRIEND_TEST(WiFiEndpointTest, InitialFrequency6GHz);

  enum KeyManagement {
    kKeyManagement802_1x,
    kKeyManagement802_1x_Wpa3,
    kKeyManagementPSK,
    kKeyManagementSAE,
    kKeyManagementOWE,
  };

  // Build a simple WiFiEndpoint, for testing purposes.
  static WiFiEndpointRefPtr MakeEndpoint(ControlInterface* control_interface,
                                         const WiFiRefPtr& wifi,
                                         const std::string& ssid,
                                         net_base::MacAddress bssid,
                                         const std::string& network_mode,
                                         uint16_t frequency,
                                         int16_t signal_dbm,
                                         const SecurityFlags& security_flags);
  // As above, but with the last two parameters false.
  static WiFiEndpointRefPtr MakeOpenEndpoint(
      ControlInterface* control_interface,
      const WiFiRefPtr& wifi,
      const std::string& ssid,
      net_base::MacAddress bssid,
      const std::string& network_mode,
      uint16_t frequency,
      int16_t signal_dbm);
  // Maps mode strings from supplicant into flimflam's nomenclature, as defined
  // in chromeos/dbus/service_constants.h.
  static std::string ParseMode(const std::string& mode_string);
  // Parses an Endpoint's properties to identify an approprirate flimflam
  // security property value, as defined in chromeos/dbus/service_constants.h.
  // The stored data in the |flags| parameter is merged with the provided
  // properties, and the security value returned is the result of the
  // merger.
  static WiFiSecurity::Mode ParseSecurity(const KeyValueStore& properties,
                                          SecurityFlags* flags);
  // Parses an Endpoint's properties' "RSN" or "WPA" sub-dictionary, to
  // identify supported key management methods (802.1x or PSK).
  static void ParseKeyManagementMethods(
      const KeyValueStore& security_method_properties,
      std::set<KeyManagement>* key_management_methods);
  // Determine the negotiated operating mode for the channel by looking at
  // the information elements, frequency and data rates.  The information
  // elements and data rates live in |properties|.
  static Metrics::WiFiNetworkPhyMode DeterminePhyModeFromFrequency(
      const KeyValueStore& properties, uint16_t frequency);
  // Parse information elements to determine the physical mode and other
  // information associated with the AP.  Returns true if a physical mode was
  // determined from the IE elements, false otherwise.
  bool ParseIEs(const KeyValueStore& properties,
                Metrics::WiFiNetworkPhyMode* phy_mode);
  // Parse MDE information element and set *|otds_ft_supported| to true if
  // Over-the-DS Fast BSS Transition is supported by this AP.
  static void ParseMobilityDomainElement(
      std::vector<uint8_t>::const_iterator ie,
      std::vector<uint8_t>::const_iterator end,
      Ap80211krvSupport* krv_support);
  // Parse an Extended Capabilities information element, set
  // *|bss_transition_supported| to true if BSS Transition management is
  // supported by this AP, and set *|dms_supported| to true if DMS is supported
  // by this AP.
  static void ParseExtendedCapabilities(
      std::vector<uint8_t>::const_iterator ie,
      std::vector<uint8_t>::const_iterator end,
      SupportedFeatures* supported_features);
  // Get the value of the extended capability identified by |octet| and |bit|.
  // Returns false if the information element is not long enough.
  static bool GetExtendedCapability(std::vector<uint8_t>::const_iterator ie,
                                    std::vector<uint8_t>::const_iterator end,
                                    IEEE_80211::ExtendedCapOctet octet,
                                    uint8_t bit);
  // Parse a WPA information element.
  static void ParseWPACapabilities(std::vector<uint8_t>::const_iterator ie,
                                   std::vector<uint8_t>::const_iterator end,
                                   bool* found_ft_cipher);
  // Parse a single vendor information element.
  void ParseVendorIE(std::vector<uint8_t>::const_iterator ie,
                     std::vector<uint8_t>::const_iterator end);
  // Parse Advertisement Protocols list.
  void ParseAdvertisementProtocolList(std::vector<uint8_t>::const_iterator ie,
                                      std::vector<uint8_t>::const_iterator end,
                                      bool* anqp_support);
  // Parse Reduced Neighbor Report element.
  void ParseRNR(std::vector<uint8_t>::const_iterator ie,
                std::vector<uint8_t>::const_iterator end);
  // Parse ANQP fields, return when ANQP fields where effectively parsed.
  bool ParseANQPFields(const KeyValueStore& properties);
  // Parse ANQP Capability List field.
  bool ParseANQPCapabilityList(std::vector<uint8_t>::const_iterator ie,
                               std::vector<uint8_t>::const_iterator end,
                               ANQPCapabilities* anqp_capabilities);

  // Assigns a value to |has_tethering_signature_|.
  void CheckForTetheringSignature();

  // Private setter used in unit tests.
  void set_security_mode(WiFiSecurity::Mode mode) { security_mode_ = mode; }

  const std::vector<uint8_t> ssid_;
  const net_base::MacAddress bssid_;
  std::vector<uint8_t> owe_ssid_;
  std::optional<net_base::MacAddress> owe_bssid_;
  std::string ssid_string_;
  const std::string ssid_hex_;
  std::string country_code_;
  int16_t signal_strength_;
  base::Time last_seen_;
  uint16_t frequency_;
  uint16_t physical_mode_;
  // network_mode_ is represented as flimflam names
  // (not necessarily the same as wpa_supplicant names)
  std::string network_mode_;
  WiFiSecurity::Mode security_mode_ = WiFiSecurity::kNone;
  VendorInformation vendor_information_;
  bool has_rsn_property_;
  bool has_wpa_property_;
  bool has_tethering_signature_;
  SecurityFlags security_flags_;
  Metrics* metrics_;

  SupportedFeatures supported_features_;

  ControlInterface* control_interface_;
  WiFiRefPtr device_;
  RpcIdentifier rpc_id_;
  std::unique_ptr<SupplicantBSSProxyInterface> supplicant_bss_proxy_;
};

}  // namespace shill

#endif  // SHILL_WIFI_WIFI_ENDPOINT_H_
