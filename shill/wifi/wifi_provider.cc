// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/wifi_provider.h"

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/containers/contains.h>
#include <base/format_macros.h>
#include <base/functional/bind.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/net-base/mac_address.h>
#include <chromeos/net-base/netlink_manager.h>
#include <chromeos/net-base/netlink_message.h>

#include "shill/error.h"
#include "shill/event_dispatcher.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/metrics.h"
#include "shill/profile.h"
#include "shill/result_aggregator.h"
#include "shill/store/key_value_store.h"
#include "shill/store/pkcs11_cert_store.h"
#include "shill/store/pkcs11_slot_getter.h"
#include "shill/store/store_interface.h"
#include "shill/supplicant/wpa_supplicant.h"
#include "shill/technology.h"
#include "shill/wifi/hotspot_device.h"
#include "shill/wifi/ieee80211.h"
#include "shill/wifi/passpoint_credentials.h"
#include "shill/wifi/wifi.h"
#include "shill/wifi/wifi_endpoint.h"
#include "shill/wifi/wifi_phy.h"
#include "shill/wifi/wifi_rf.h"
#include "shill/wifi/wifi_security.h"
#include "shill/wifi/wifi_service.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kWiFi;
}  // namespace Logging

namespace {

// We used to store a few properties under this group entry, but they've been
// deprecated. Remove after M-88.
const char kWiFiProviderStorageId[] = "provider_of_wifi";

// Note that WiFiProvider generates some manager-level errors, because it
// implements the WiFi portion of the Manager.GetService flimflam API. The
// API is implemented here, rather than in manager, to keep WiFi-specific
// logic in the right place.
const char kManagerErrorSSIDRequired[] = "must specify SSID";
const char kManagerErrorSSIDTooLong[] = "SSID is too long";
const char kManagerErrorSSIDTooShort[] = "SSID is too short";
const char kManagerErrorInvalidSecurityClass[] = "invalid security class";
const char kManagerErrorInvalidServiceMode[] = "invalid service mode";

// Special value that can be passed into GetPhyInfo() to request a dump of all
// phys on the system.
static constexpr uint32_t kAllPhys = UINT32_MAX;

// Timeout for the completion of activities started by UpdateRegAndPhy()
// function.
static constexpr auto kPhyUpdateTimeout = base::Milliseconds(500);

// Interface name prefix used in local connection interfaces.
static constexpr char kHotspotIfacePrefix[] = "ap";

// Retrieve a WiFi service's identifying properties from passed-in |args|.
// Returns true if |args| are valid and populates |ssid|, |mode|,
// |security_class| and |hidden_ssid|, if successful.  Otherwise, this function
// returns false and populates |error| with the reason for failure.  It
// is a fatal error if the "Type" parameter passed in |args| is not kWiFi.
bool GetServiceParametersFromArgs(const KeyValueStore& args,
                                  std::vector<uint8_t>* ssid_bytes,
                                  std::string* mode,
                                  std::string* security_class,
                                  WiFiSecurity* security,
                                  bool* hidden_ssid,
                                  Error* error) {
  CHECK_EQ(args.Lookup<std::string>(kTypeProperty, ""), kTypeWifi);

  std::string mode_test = args.Lookup<std::string>(kModeProperty, kModeManaged);
  if (!WiFiService::IsValidMode(mode_test)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          kManagerErrorInvalidServiceMode);
    return false;
  }

  std::vector<uint8_t> ssid;
  if (args.Contains<std::string>(kWifiHexSsid)) {
    std::string ssid_hex_string = args.Get<std::string>(kWifiHexSsid);
    if (!base::HexStringToBytes(ssid_hex_string, &ssid)) {
      Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                            "Hex SSID parameter is not valid");
      return false;
    }
  } else if (args.Contains<std::string>(kSSIDProperty)) {
    std::string ssid_string = args.Get<std::string>(kSSIDProperty);
    ssid = std::vector<uint8_t>(ssid_string.begin(), ssid_string.end());
  } else {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          kManagerErrorSSIDRequired);
    return false;
  }

  if (ssid.size() < 1) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidNetworkName,
                          kManagerErrorSSIDTooShort);
    return false;
  }

  if (ssid.size() > IEEE_80211::kMaxSSIDLen) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidNetworkName,
                          kManagerErrorSSIDTooLong);
    return false;
  }

  WiFiSecurity security_test;
  if (args.Contains<std::string>(kSecurityProperty)) {
    security_test =
        WiFiSecurity(args.Lookup<std::string>(kSecurityProperty, ""));
    if (!security_test.IsValid()) {
      Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                            "Invalid Security property");
      return false;
    }
    // Assignment below, after checking against SecurityClass.
  }

  const std::string kDefaultSecurity = kSecurityNone;
  if (args.Contains<std::string>(kSecurityClassProperty)) {
    std::string security_class_test =
        args.Lookup<std::string>(kSecurityClassProperty, kDefaultSecurity);
    if (!WiFiService::IsValidSecurityClass(security_class_test)) {
      Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                            kManagerErrorInvalidSecurityClass);
      return false;
    }
    if (security_test.IsValid() &&
        security_test.SecurityClass() != security_class_test) {
      Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                            "Mismatch between Security and SecurityClass");
      return false;
    }
    *security_class = security_class_test;
  } else {
    *security_class = security_test.IsValid() ? security_test.SecurityClass()
                                              : kDefaultSecurity;
  }

  if (security_test.IsValid()) {
    *security = security_test;
  }
  *ssid_bytes = ssid;
  *mode = mode_test;

  // If the caller didn't specify otherwise, assume it is not a hidden service.
  *hidden_ssid = args.Lookup<bool>(kWifiHiddenSsid, false);

  return true;
}

// Retrieve a WiFi service's identifying properties from passed-in |storage|.
// Return true if storage contain valid parameter values and populates |ssid|,
// |mode|, |security_class| and |hidden_ssid|. Otherwise, this function returns
// false and populates |error| with the reason for failure.
bool GetServiceParametersFromStorage(const StoreInterface* storage,
                                     const std::string& entry_name,
                                     std::vector<uint8_t>* ssid_bytes,
                                     std::string* mode,
                                     std::string* security_class,
                                     WiFiSecurity* security,
                                     bool* hidden_ssid,
                                     Error* error) {
  // Verify service type.
  std::string type;
  if (!storage->GetString(entry_name, WiFiService::kStorageType, &type) ||
      type != kTypeWifi) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Unspecified or invalid network type");
    return false;
  }

  std::string ssid_hex;
  if (!storage->GetString(entry_name, WiFiService::kStorageSSID, &ssid_hex) ||
      !base::HexStringToBytes(ssid_hex, ssid_bytes)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Unspecified or invalid SSID");
    return false;
  }

  if (!storage->GetString(entry_name, WiFiService::kStorageMode, mode) ||
      mode->empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Network mode not specified");
    return false;
  }

  std::string store_security;
  if (storage->GetString(entry_name, WiFiService::kStorageSecurity,
                         &store_security)) {
    WiFiSecurity sec(store_security);
    if (sec.IsValid()) {
      *security = sec;
    } else {
      Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                            "Unspecified or invalid security");
      return false;
    }
  }

  if (!storage->GetString(entry_name, WiFiService::kStorageSecurityClass,
                          security_class) ||
      !WiFiService::IsValidSecurityClass(*security_class)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Unspecified or invalid security class");
    return false;
  }

  if (!storage->GetBool(entry_name, WiFiService::kStorageHiddenSSID,
                        hidden_ssid)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Hidden SSID not specified");
    return false;
  }
  return true;
}

}  // namespace

const char WiFiProvider::kEventTypeConfig[] = "config";
const char WiFiProvider::kEventTypeScan[] = "scan";
const char WiFiProvider::kEventTypeRegulatory[] = "regulatory";
const char WiFiProvider::kEventTypeMlme[] = "mlme";

WiFiProvider::WiFiProvider(Manager* manager)
    : manager_(manager),
      netlink_manager_(net_base::NetlinkManager::GetInstance()),
      weak_ptr_factory_while_started_(this),
      p2p_manager_(new P2PManager(manager_)),
      hotspot_device_factory_(
          base::BindRepeating([](Manager* manager,
                                 const std::string& primary_link_name,
                                 const std::string& link_name,
                                 net_base::MacAddress mac_address,
                                 uint32_t phy_index,
                                 WiFiPhy::Priority priority,
                                 LocalDevice::EventCallback callback) {
            return HotspotDeviceRefPtr(new HotspotDevice(
                manager, primary_link_name, link_name, mac_address, phy_index,
                priority, std::move(callback)));
          })),
      running_(false),
      disable_vht_(false) {}

WiFiProvider::~WiFiProvider() = default;

void WiFiProvider::Start() {
  running_ = true;
  p2p_manager_->InitPropertyStore(manager_->mutable_store());
  broadcast_handler_ =
      base::BindRepeating(&WiFiProvider::HandleNetlinkBroadcast,
                          weak_ptr_factory_while_started_.GetWeakPtr());
  netlink_manager_->AddBroadcastHandler(broadcast_handler_);
  // Subscribe to multicast events.
  netlink_manager_->SubscribeToEvents(Nl80211Message::kMessageTypeString,
                                      kEventTypeConfig);
  netlink_manager_->SubscribeToEvents(Nl80211Message::kMessageTypeString,
                                      kEventTypeScan);
  netlink_manager_->SubscribeToEvents(Nl80211Message::kMessageTypeString,
                                      kEventTypeRegulatory);
  netlink_manager_->SubscribeToEvents(Nl80211Message::kMessageTypeString,
                                      kEventTypeMlme);
  GetPhyInfo(kAllPhys);
  p2p_manager_->Start();
}

void WiFiProvider::Stop() {
  SLOG(2) << __func__;
  while (!services_.empty()) {
    WiFiServiceRefPtr service = services_.back();
    ForgetService(service);
    SLOG(2) << "WiFiProvider deregistering service " << service->log_name();
    manager_->DeregisterService(service);
  }
  service_by_endpoint_.clear();
  weak_ptr_factory_while_started_.InvalidateWeakPtrs();
  netlink_manager_->RemoveBroadcastHandler(broadcast_handler_);
  wifi_phys_.clear();
  manager_->RefreshTetheringCapabilities();
  p2p_manager_->Stop();
  running_ = false;
}

void WiFiProvider::CreateServicesFromProfile(const ProfileRefPtr& profile) {
  const StoreInterface* storage = profile->GetConstStorage();
  KeyValueStore args;
  args.Set<std::string>(kTypeProperty, kTypeWifi);
  bool created_hidden_service = false;
  for (const auto& group : storage->GetGroupsWithProperties(args)) {
    std::vector<uint8_t> ssid_bytes;
    std::string network_mode;
    std::string security_class;
    WiFiSecurity security;
    bool is_hidden = false;
    if (!GetServiceParametersFromStorage(storage, group, &ssid_bytes,
                                         &network_mode, &security_class,
                                         &security, &is_hidden, nullptr)) {
      continue;
    }

    if (FindService(ssid_bytes, network_mode, security_class, security)) {
      // If service already exists, we have nothing to do, since the
      // service has already loaded its configuration from storage.
      // This is guaranteed to happen in the single case where
      // CreateServicesFromProfile() is called on a WiFiProvider from
      // Manager::PushProfile():
      continue;
    }

    // If we have stored Security then it is "sacrosanct", we can't
    // change it freely so we freeze it.
    if (security.IsValid()) {
      security.Freeze();
    }

    AddService(ssid_bytes, network_mode, security_class, security, is_hidden);

    // By registering the service in AddService, the rest of the configuration
    // will be loaded from the profile into the service via ConfigureService().

    if (is_hidden) {
      created_hidden_service = true;
    }
  }

  // If WiFi is unconnected and we created a hidden service as a result
  // of opening the profile, we should initiate a WiFi scan, which will
  // allow us to find any hidden services that we may have created.
  if (created_hidden_service &&
      !manager_->IsTechnologyConnected(Technology::kWiFi)) {
    Error unused_error;
    manager_->RequestScan(kTypeWifi, &unused_error);
  }

  ReportRememberedNetworkCount();

  // Only report service source metrics when a user profile is pushed.
  // This ensures that we have an equal number of samples for the
  // default profile and user profiles.
  if (!profile->IsDefault()) {
    ReportServiceSourceMetrics();
  }
}

ServiceRefPtr WiFiProvider::FindSimilarService(const KeyValueStore& args,
                                               Error* error) const {
  std::vector<uint8_t> ssid;
  std::string mode;
  std::string security_class;
  WiFiSecurity security;
  bool hidden_ssid;

  if (!GetServiceParametersFromArgs(args, &ssid, &mode, &security_class,
                                    &security, &hidden_ssid, error)) {
    return nullptr;
  }

  WiFiServiceRefPtr service(FindService(ssid, mode, security_class, security));
  if (!service) {
    error->Populate(Error::kNotFound, Error::kServiceNotFoundMsg, FROM_HERE);
  }

  return service;
}

ServiceRefPtr WiFiProvider::CreateTemporaryService(const KeyValueStore& args,
                                                   Error* error) {
  std::vector<uint8_t> ssid;
  std::string mode;
  std::string security_class;
  WiFiSecurity security;
  bool hidden_ssid;

  if (!GetServiceParametersFromArgs(args, &ssid, &mode, &security_class,
                                    &security, &hidden_ssid, error)) {
    return nullptr;
  }

  return new WiFiService(manager_, this, ssid, mode, security_class, security,
                         hidden_ssid);
}

ServiceRefPtr WiFiProvider::CreateTemporaryServiceFromProfile(
    const ProfileRefPtr& profile, const std::string& entry_name, Error* error) {
  std::vector<uint8_t> ssid;
  std::string mode;
  std::string security_class;
  WiFiSecurity security;
  bool hidden_ssid;
  if (!GetServiceParametersFromStorage(profile->GetConstStorage(), entry_name,
                                       &ssid, &mode, &security_class, &security,
                                       &hidden_ssid, error)) {
    return nullptr;
  }
  return new WiFiService(manager_, this, ssid, mode, security_class, security,
                         hidden_ssid);
}

ServiceRefPtr WiFiProvider::GetService(const KeyValueStore& args,
                                       Error* error) {
  return GetWiFiService(args, error);
}

WiFiServiceRefPtr WiFiProvider::GetWiFiService(const KeyValueStore& args,
                                               Error* error) {
  std::vector<uint8_t> ssid_bytes;
  std::string mode;
  std::string security_class;
  WiFiSecurity security;
  bool hidden_ssid;

  if (!GetServiceParametersFromArgs(args, &ssid_bytes, &mode, &security_class,
                                    &security, &hidden_ssid, error)) {
    return nullptr;
  }

  WiFiServiceRefPtr service(
      FindService(ssid_bytes, mode, security_class, security));
  if (!service) {
    if (security.IsValid()) {
      // We are called with key/value args obtained via DBus.  For this case
      // if we get Security property then it is "fixed".
      security.Freeze();
    }
    service =
        AddService(ssid_bytes, mode, security_class, security, hidden_ssid);
  }

  return service;
}

void WiFiProvider::AbandonService(const ServiceRefPtr& service) {
  // It is safe to cast the Service to WiFiService since the manager routes the
  // call to the provider according to the technology included in the service.
  CHECK_EQ(service->technology(), Technology::kWiFi);
  WiFiServiceRefPtr wifi_service(static_cast<WiFiService*>(service.get()));

  PasspointCredentialsRefPtr credentials(wifi_service->parent_credentials());
  if (!credentials) {
    return;
  }

  // Remove the certificate and the key used by this set of credentials if
  // it's not used by anybody else.
  EraseUnusedCertificateAndKey(credentials);

  // Delete the credentials set from profile storage.
  EraseCredentials(credentials);
}

WiFiServiceRefPtr WiFiProvider::FindServiceForEndpoint(
    const WiFiEndpointConstRefPtr& endpoint) {
  EndpointServiceMap::iterator service_it =
      service_by_endpoint_.find(endpoint.get());
  if (service_it == service_by_endpoint_.end())
    return nullptr;
  return service_it->second;
}

bool WiFiProvider::OnEndpointAdded(const WiFiEndpointConstRefPtr& endpoint) {
  if (!running_) {
    return false;
  }

  auto ssid = endpoint->ssid();
  auto security = endpoint->security_mode();
  const auto mode = endpoint->network_mode();

  WiFiServiceRefPtr service = FindService(endpoint);
  if (!service) {
    if (security == WiFiSecurity::kTransOwe && endpoint->has_rsn_owe() &&
        !endpoint->owe_ssid().empty()) {
      LOG(WARNING) << "Found a hidden OWE BSS w/o public counterpart";
      ssid = endpoint->owe_ssid();
    }
    service = AddService(ssid, mode, WiFiSecurity::SecurityClass(security),
                         security, /*is_hidden=*/false);
  }

  std::string asgn_endpoint_log = base::StringPrintf(
      "Assigning endpoint %s to service %s",
      endpoint->bssid().ToString().c_str(), service->log_name().c_str());

  if (!service->HasEndpoints() && service->IsRemembered()) {
    LOG(INFO) << asgn_endpoint_log;
  } else {
    SLOG(1) << asgn_endpoint_log;
  }

  service->AddEndpoint(endpoint);
  service_by_endpoint_[endpoint.get()] = service;

  manager_->UpdateService(service);
  // Return whether the service has already matched with a set of credentials
  // or not.
  return service->parent_credentials() != nullptr;
}

WiFiServiceRefPtr WiFiProvider::OnEndpointRemoved(
    const WiFiEndpointConstRefPtr& endpoint) {
  if (!running_) {
    return nullptr;
  }

  WiFiServiceRefPtr service = FindServiceForEndpoint(endpoint);

  CHECK(service) << "Can't find Service for Endpoint (with BSSID "
                 << endpoint->bssid().ToString() << ").";

  std::string rmv_endpoint_log = base::StringPrintf(
      "Removed endpoint %s from service %s",
      endpoint->bssid().ToString().c_str(), service->log_name().c_str());

  service->RemoveEndpoint(endpoint);
  service_by_endpoint_.erase(endpoint.get());

  if (!service->HasEndpoints() && service->IsRemembered()) {
    LOG(INFO) << rmv_endpoint_log;
  } else {
    SLOG(1) << rmv_endpoint_log;
  }

  if (service->HasEndpoints() || service->IsRemembered()) {
    // Keep services around if they are in a profile or have remaining
    // endpoints.
    manager_->UpdateService(service);
    return nullptr;
  }

  ForgetService(service);
  manager_->DeregisterService(service);

  return service;
}

void WiFiProvider::OnEndpointUpdated(const WiFiEndpointConstRefPtr& endpoint) {
  if (!running_) {
    return;
  }

  WiFiService* service = FindServiceForEndpoint(endpoint).get();
  CHECK(service);

  // If the service still matches the endpoint in its new configuration,
  // we need only to update the service.
  if (service->IsMatch(endpoint)) {
    service->NotifyEndpointUpdated(endpoint);
    return;
  }

  // The endpoint no longer matches the associated service.  Remove the
  // endpoint, so current references to the endpoint are reset, then add
  // it again so it can be associated with a new service.
  OnEndpointRemoved(endpoint);
  OnEndpointAdded(endpoint);
}

bool WiFiProvider::OnServiceUnloaded(
    const WiFiServiceRefPtr& service,
    const PasspointCredentialsRefPtr& credentials) {
  if (credentials) {
    // The service had credentials. We want to remove them and invalidate all
    // the services that were populated with it.
    ForgetCredentials(credentials);
  }

  // If the service still has endpoints, it should remain in the service list.
  if (service->HasEndpoints()) {
    return false;
  }

  // This is the one place where we forget the service but do not also
  // deregister the service with the manager.  However, by returning
  // true below, the manager will do so itself.
  ForgetService(service);
  return true;
}

void WiFiProvider::UpdateStorage(Profile* profile) {
  CHECK(profile);
  StoreInterface* storage = profile->GetStorage();
  // We stored this only to the default profile, but no reason not to delete it
  // from any profile it exists in.
  // Remove after M-88.
  storage->DeleteGroup(kWiFiProviderStorageId);
}

void WiFiProvider::SortServices() {
  std::sort(services_.begin(), services_.end(),
            [](const WiFiServiceRefPtr& a, const WiFiServiceRefPtr& b) -> bool {
              return Service::Compare(a, b, true, {}).first;
            });
}

WiFiServiceRefPtr WiFiProvider::AddService(const std::vector<uint8_t>& ssid,
                                           const std::string& mode,
                                           const std::string& security_class,
                                           const WiFiSecurity& security,
                                           bool is_hidden) {
  WiFiServiceRefPtr service = new WiFiService(
      manager_, this, ssid, mode, security_class, security, is_hidden);

  services_.push_back(service);
  manager_->RegisterService(service);
  return service;
}

WiFiServiceRefPtr WiFiProvider::FindService(
    const std::vector<uint8_t>& ssid,
    const std::string& mode,
    const std::string& security_class,
    const WiFiSecurity& security) const {
  for (const auto& service : services_) {
    if (service->IsMatch(ssid, mode, security_class, security)) {
      return service;
    }
  }
  return nullptr;
}

WiFiServiceRefPtr WiFiProvider::FindService(
    const WiFiEndpointConstRefPtr& endpoint) const {
  for (const auto& service : services_) {
    if (service->IsMatch(endpoint)) {
      return service;
    }
  }
  return nullptr;
}

ByteArrays WiFiProvider::GetHiddenSSIDList() {
  SortServices();

  // Create a unique container of hidden SSIDs.
  ByteArrays hidden_ssids;
  for (const auto& service : services_) {
    if (service->hidden_ssid() && service->IsRemembered()) {
      if (base::Contains(hidden_ssids, service->ssid())) {
        LOG(WARNING) << "Duplicate HiddenSSID: " << service->log_name();
        continue;
      }
      hidden_ssids.push_back(service->ssid());
    }
  }
  SLOG(2) << "Found " << hidden_ssids.size() << " hidden services";
  return hidden_ssids;
}

void WiFiProvider::ForgetService(const WiFiServiceRefPtr& service) {
  std::vector<WiFiServiceRefPtr>::iterator it;
  it = std::find(services_.begin(), services_.end(), service);
  if (it == services_.end()) {
    return;
  }
  (*it)->ResetWiFi();
  services_.erase(it);
}

void WiFiProvider::ReportRememberedNetworkCount() {
  metrics()->SendToUMA(
      Metrics::kMetricRememberedWiFiNetworkCount,
      std::count_if(services_.begin(), services_.end(),
                    [](ServiceRefPtr s) { return s->IsRemembered(); }));
  metrics()->SendToUMA(Metrics::kMetricPasspointNetworkCount,
                       std::count_if(services_.begin(), services_.end(),
                                     [](WiFiServiceRefPtr s) {
                                       return s->parent_credentials() !=
                                              nullptr;
                                     }));
}

void WiFiProvider::ReportServiceSourceMetrics() {
  for (const auto& security_class : {kSecurityClassNone, kSecurityClassWep,
                                     kSecurityClassPsk, kSecurityClass8021x}) {
    metrics()->SendToUMA(
        Metrics::kMetricRememberedSystemWiFiNetworkCountBySecurityModeFormat,
        security_class,
        std::count_if(services_.begin(), services_.end(),
                      [security_class](WiFiServiceRefPtr s) {
                        return s->IsRemembered() &&
                               s->IsSecurityMatch(security_class) &&
                               s->profile()->IsDefault();
                      }));
    metrics()->SendToUMA(
        Metrics::kMetricRememberedUserWiFiNetworkCountBySecurityModeFormat,
        security_class,
        std::count_if(services_.begin(), services_.end(),
                      [security_class](WiFiServiceRefPtr s) {
                        return s->IsRemembered() &&
                               s->IsSecurityMatch(security_class) &&
                               !s->profile()->IsDefault();
                      }));
  }

  metrics()->SendToUMA(Metrics::kMetricHiddenSSIDNetworkCount,
                       std::count_if(services_.begin(), services_.end(),
                                     [](WiFiServiceRefPtr s) {
                                       return s->IsRemembered() &&
                                              s->hidden_ssid();
                                     }));

  for (const auto& service : services_) {
    if (service->IsRemembered() && service->hidden_ssid()) {
      metrics()->SendBoolToUMA(Metrics::kMetricHiddenSSIDEverConnected,
                               service->has_ever_connected());
    }
  }
}

void WiFiProvider::ReportAutoConnectableServices() {
  int num_services = NumAutoConnectableServices();
  // Only report stats when there are wifi services available.
  if (num_services) {
    metrics()->SendToUMA(Metrics::kMetricWifiAutoConnectableServices,
                         num_services);
  }
}

int WiFiProvider::NumAutoConnectableServices() {
  const char* reason = nullptr;
  int num_services = 0;
  // Determine the number of services available for auto-connect.
  for (const auto& service : services_) {
    // Service is available for auto connect if it is configured for auto
    // connect, and is auto-connectable.
    if (service->auto_connect() && service->IsAutoConnectable(&reason)) {
      num_services++;
    }
  }
  return num_services;
}

void WiFiProvider::ResetServicesAutoConnectCooldownTime() {
  for (const auto& service : services_) {
    service->ResetAutoConnectCooldownTime();
  }
}

std::vector<std::vector<uint8_t>>
WiFiProvider::GetSsidsConfiguredForAutoConnect() {
  std::vector<std::vector<uint8_t>> results;
  for (const auto& service : services_) {
    if (service->auto_connect()) {
      // Service configured for auto-connect.
      results.push_back(service->ssid());
    }
  }
  return results;
}

void WiFiProvider::LoadCredentialsFromProfile(const ProfileRefPtr& profile) {
  const StoreInterface* storage = profile->GetConstStorage();
  Pkcs11SlotGetter* slot_getter = profile->GetSlotGetter();
  KeyValueStore args;
  args.Set<std::string>(PasspointCredentials::kStorageType,
                        PasspointCredentials::kTypePasspoint);
  const auto passpoint_credentials = storage->GetGroupsWithProperties(args);
  if (!profile->IsDefault()) {
    metrics()->SendSparseToUMA(Metrics::kMetricPasspointSavedCredentials,
                               passpoint_credentials.size());
  }
  for (const auto& group : passpoint_credentials) {
    PasspointCredentialsRefPtr creds = new PasspointCredentials(group);
    creds->SetEapSlotGetter(slot_getter);
    creds->Load(storage);
    creds->SetProfile(profile);
    AddCredentials(creds);
  }
}

void WiFiProvider::UnloadCredentialsFromProfile(const ProfileRefPtr& profile) {
  PasspointCredentialsMap creds(credentials_by_id_);
  for (const auto& [id, c] : creds) {
    if (c != nullptr && c->profile() == profile) {
      // We don't need to call RemoveCredentials with service removal because at
      // Profile removal time, we expect all the services to be removed already.
      RemoveCredentials(c);
    }
  }
}

void WiFiProvider::AddCredentials(
    const PasspointCredentialsRefPtr& credentials) {
  credentials_by_id_[credentials->id()] = credentials;

  LOG(INFO) << __func__ << ": " << *credentials;

  // Notify the observers a set of credentials was added.
  // It is done before pushing it to the wifi device as at this point, the set
  // of credentials is logically added to the list but supplicant might no be
  // ready to accept the configuration yet.
  for (PasspointCredentialsObserver& observer : credentials_observers_) {
    observer.OnPasspointCredentialsAdded(credentials);
  }

  DeviceRefPtr device =
      manager_->GetEnabledDeviceWithTechnology(Technology::kWiFi);
  if (!device) {
    return;
  }
  // We can safely do this because GetEnabledDeviceWithTechnology ensures
  // the type of the device is WiFi.
  WiFiRefPtr wifi(static_cast<WiFi*>(device.get()));
  if (!wifi->AddCred(credentials)) {
    SLOG(1) << "Failed to push credentials " << credentials->id()
            << " to device.";
  }
}

bool WiFiProvider::HasCredentials(const PasspointCredentialsRefPtr& credentials,
                                  const ProfileRefPtr& profile) {
  const StoreInterface* storage = profile->GetConstStorage();
  KeyValueStore args;
  args.Set<std::string>(PasspointCredentials::kStorageType,
                        PasspointCredentials::kTypePasspoint);
  const auto passpoint_credentials = storage->GetGroupsWithProperties(args);
  // Compare with saved Passpoint credentials.
  for (const auto& group : passpoint_credentials) {
    PasspointCredentialsRefPtr tmp_creds = new PasspointCredentials(group);
    tmp_creds->Load(storage);
    if (credentials.get()->IsKeyEqual(*tmp_creds.get())) {
      return true;
    }
  }
  // Compare with active Passpoint credentials.
  for (const auto& tmp_creds : credentials_by_id_) {
    if (credentials.get()->IsKeyEqual(*tmp_creds.second.get())) {
      return true;
    }
  }
  return false;
}

void WiFiProvider::EraseUnusedCertificateAndKey(
    const PasspointCredentialsRefPtr& credentials) {
  CHECK(credentials);
  if (credentials->eap().cert_id().empty()) {
    return;
  }

  // Check if there are other Passpoint credentials using the same certificate
  // or key. If so, avoid deleting the certificate and key.
  for (const auto& cred : credentials_by_id_) {
    if (credentials->id() != cred.first &&
        credentials->eap().cert_id() == cred.second->eap().cert_id()) {
      return;
    }
  }

  const auto& cert_id = credentials->eap().cert_id();
  const std::vector<std::string> data =
      base::SplitString(cert_id, ":", base::WhitespaceHandling::TRIM_WHITESPACE,
                        base::SplitResult::SPLIT_WANT_NONEMPTY);
  if (data.size() != 2) {
    LOG(ERROR) << "Invalid certificate ID " << cert_id;
    return;
  }
  uint32_t tmp_slot_id;
  if (!base::StringToUint(data[0], &tmp_slot_id)) {
    LOG(ERROR) << "Invalid slot ID " << data[0];
    return;
  }
  CK_SLOT_ID slot_id = tmp_slot_id;
  std::string cka_id;
  if (!base::HexStringToString(data[1], &cka_id)) {
    LOG(ERROR) << "Failed to decode hex ID string: " << data[1];
    return;
  }
  Pkcs11CertStore pkcs11_store;
  if (!pkcs11_store.Delete(slot_id, cka_id)) {
    LOG(ERROR) << "Failed to delete certificate and key with ID: " << cert_id;
  }
}

void WiFiProvider::EraseCredentials(
    const PasspointCredentialsRefPtr& credentials) {
  CHECK(credentials);
  StoreInterface* storage = credentials->profile()->GetStorage();
  storage->DeleteGroup(credentials->id());
  storage->Flush();
}

bool WiFiProvider::ForgetCredentials(
    const PasspointCredentialsRefPtr& credentials) {
  if (!credentials ||
      credentials_by_id_.find(credentials->id()) == credentials_by_id_.end()) {
    // Credentials have been removed, nothing to do.
    return true;
  }

  // Remove the credentials from our credentials set and from the WiFi device.
  bool success = RemoveCredentials(credentials);
  // Find all the services linked to the set.
  std::vector<WiFiServiceRefPtr> to_delete;
  for (auto& service : services_) {
    if (service->parent_credentials() == credentials) {
      // Prevent useless future calls to ForgetCredentials().
      service->set_parent_credentials(nullptr);
      // There's no risk of double removal here because the original service's
      // credentials were reset in WiFiService::Unload().
      to_delete.push_back(service);
    }
  }
  // Delete the services separately to avoid iterating over the list while
  // deleting.
  for (auto& service : to_delete) {
    Error error;
    service->Remove(&error);
  }
  return success;
}

bool WiFiProvider::DeleteCredentials(
    const PasspointCredentialsRefPtr& credentials) {
  CHECK(credentials);
  // Remove certificate and keys used by the set of credentials if not used by
  // anyone else.
  EraseUnusedCertificateAndKey(credentials);
  // Remove the set of credentials from the storage.
  EraseCredentials(credentials);
  // Remove the set of credentials and populated service from the provider.
  return ForgetCredentials(credentials);
}

bool WiFiProvider::DeleteCredentials(const KeyValueStore& properties) {
  const auto fqdn = properties.Lookup<std::string>(
      kPasspointCredentialsFQDNProperty, std::string());
  const auto package_name = properties.Lookup<std::string>(
      kPasspointCredentialsAndroidPackageNameProperty, std::string());

  bool success = true;
  std::vector<const PasspointCredentialsRefPtr> removed_credentials;
  for (const auto& credentials : credentials_by_id_) {
    if (!fqdn.empty() && credentials.second->GetFQDN() != fqdn) {
      continue;
    }
    if (!package_name.empty() &&
        credentials.second->android_package_name() != package_name) {
      continue;
    }
    removed_credentials.push_back(credentials.second);
  }
  for (const auto& credentials : removed_credentials) {
    success &= DeleteCredentials(credentials);
  }
  return success;
}

bool WiFiProvider::RemoveCredentials(
    const PasspointCredentialsRefPtr& credentials) {
  credentials_by_id_.erase(credentials->id());

  LOG(INFO) << __func__ << ": " << *credentials;

  // Notify the observers a set of credentials was removed.
  for (PasspointCredentialsObserver& observer : credentials_observers_) {
    observer.OnPasspointCredentialsRemoved(credentials);
  }

  DeviceRefPtr device =
      manager_->GetEnabledDeviceWithTechnology(Technology::kWiFi);
  if (!device) {
    return false;
  }
  // We can safely do this because GetEnabledDeviceWithTechnology ensures
  // the type of the device is WiFi.
  WiFiRefPtr wifi(static_cast<WiFi*>(device.get()));
  if (!wifi->RemoveCred(credentials)) {
    SLOG(1) << "Failed to remove credentials " << credentials->id()
            << " from the device.";
    return false;
  }
  return true;
}

std::vector<PasspointCredentialsRefPtr> WiFiProvider::GetCredentials() {
  std::vector<PasspointCredentialsRefPtr> list;
  for (const auto& [_, c] : credentials_by_id_) {
    list.push_back(c);
  }
  return list;
}

PasspointCredentialsRefPtr WiFiProvider::FindCredentials(
    const std::string& id) {
  const auto it = credentials_by_id_.find(id);
  if (it == credentials_by_id_.end()) {
    return nullptr;
  }
  return it->second;
}

void WiFiProvider::OnPasspointCredentialsMatches(
    const std::vector<PasspointMatch>& matches) {
  SLOG(1) << __func__;

  // Keep the best match for each service.
  std::map<WiFiService*, PasspointMatch> matches_by_service;
  for (const auto& m : matches) {
    LOG(INFO) << __func__ << " match between " << *m.credentials << " and "
              << m.endpoint->bssid().ToString();

    WiFiServiceRefPtr service = FindServiceForEndpoint(m.endpoint);
    if (!service) {
      SLOG(1) << "No service for endpoint " << m.endpoint->bssid().ToString();
      metrics()->SendEnumToUMA(Metrics::kMetricPasspointMatch,
                               Metrics::kPasspointMatchServiceNotFound);
      continue;
    }

    if (service->parent_credentials() &&
        service->match_priority() <= m.priority) {
      // The current match brought better or as good credentials than the
      // new one, we won't override it.
      metrics()->SendEnumToUMA(Metrics::kMetricPasspointMatch,
                               Metrics::kPasspointMatchPriorPasspointMatch);
      continue;
    }

    const auto it = matches_by_service.find(service.get());
    if (it == matches_by_service.end()) {
      // No match exists yet, just insert the new one.
      matches_by_service[service.get()] = m;
      continue;
    }

    if (it->second.priority > m.priority) {
      // The new match is better than the previous one
      matches_by_service[service.get()] = m;
    }
  }

  // Populate each service with the credentials contained in the match.
  for (auto& [service_ref, match] : matches_by_service) {
    WiFiServiceRefPtr service(service_ref);
    if (service->connectable() && !service->parent_credentials()) {
      // The service already has non-Passpoint credentials, we don't want to
      // override it.
      metrics()->SendEnumToUMA(Metrics::kMetricPasspointMatch,
                               Metrics::kPasspointMatchPriorCredentials);
      continue;
    }

    if (service->parent_credentials() &&
        service->match_priority() < match.priority) {
      // The service is populated with Passpoint credentials and the
      // previous match priority is better than the one we got now.
      // We don't want to override it.
      metrics()->SendEnumToUMA(Metrics::kMetricPasspointMatch,
                               Metrics::kPasspointMatchPriorPasspointMatch);
      continue;
    }

    auto match_type = Metrics::kPasspointNoMatch;
    if (service->parent_credentials() == nullptr) {
      switch (match.priority) {
        case MatchPriority::kHome:
          match_type = Metrics::kPasspointMatchNewHomeMatch;
          break;
        case MatchPriority::kRoaming:
          match_type = Metrics::kPasspointMatchNewRoamingMatch;
          break;
        default:
          match_type = Metrics::kPasspointMatchNewUnknownMatch;
          break;
      }
    } else {
      switch (match.priority) {
        case MatchPriority::kHome:
          match_type = Metrics::kPasspointMatchUpgradeToHomeMatch;
          break;
        case MatchPriority::kRoaming:
          match_type = Metrics::kPasspointMatchUpgradeToRoamingMatch;
          break;
        default:
          break;
      }
    }
    metrics()->SendEnumToUMA(Metrics::kMetricPasspointMatch, match_type);
    // Ensure the service is updated with the credentials and saved in the same
    // profile as the credentials set.
    LOG(INFO) << __func__ << " updating service " << service->log_name()
              << " with " << *match.credentials;
    service->OnPasspointMatch(match.credentials, match.priority);
    manager_->UpdateService(service);
    if (service->profile() != match.credentials->profile()) {
      manager_->MoveServiceToProfile(service, match.credentials->profile());
    }
  }
}

void WiFiProvider::AddPasspointCredentialsObserver(
    PasspointCredentialsObserver* observer) {
  credentials_observers_.AddObserver(observer);
}

void WiFiProvider::RemovePasspointCredentialsObserver(
    PasspointCredentialsObserver* observer) {
  credentials_observers_.RemoveObserver(observer);
}

void WiFiProvider::PhyDumpComplete(uint32_t phy_index) {
  if (phy_index == kAllPhys) {
    for (auto& it : wifi_phys_) {
      it.second->PhyDumpComplete();
    }
    return;
  }

  if (!base::Contains(wifi_phys_, phy_index)) {
    LOG(ERROR) << "Invalid PHY index: " << phy_index;
    return;
  }
  wifi_phys_[phy_index]->PhyDumpComplete();
}

void WiFiProvider::GetPhyInfo(uint32_t phy_index) {
  GetWiphyMessage get_wiphy;
  get_wiphy.AddFlag(NLM_F_DUMP);
  if (phy_index != kAllPhys) {
    get_wiphy.attributes()->SetU32AttributeValue(NL80211_ATTR_WIPHY, phy_index);
  }
  get_wiphy.attributes()->SetFlagAttributeValue(NL80211_ATTR_SPLIT_WIPHY_DUMP,
                                                true);
  get_wiphy.Send(
      netlink_manager_,
      base::BindRepeating(&WiFiProvider::OnNewWiphy,
                          weak_ptr_factory_while_started_.GetWeakPtr()),
      base::BindRepeating(&net_base::NetlinkManager::OnAckDoNothing),
      base::BindRepeating(&WiFiProvider::OnGetPhyInfoAuxMessage,
                          weak_ptr_factory_while_started_.GetWeakPtr(),
                          phy_index));
}

void WiFiProvider::OnNewWiphy(const Nl80211Message& nl80211_message) {
  // Verify NL80211_CMD_NEW_WIPHY.
  if (nl80211_message.command() != NewWiphyMessage::kCommand) {
    LOG(ERROR) << "Received unexpected command:" << nl80211_message.command();
    return;
  }
  uint32_t phy_index;
  if (!nl80211_message.const_attributes()->GetU32AttributeValue(
          NL80211_ATTR_WIPHY, &phy_index)) {
    LOG(ERROR) << "NL80211_CMD_NEW_WIPHY had no NL80211_ATTR_WIPHY";
    return;
  }

  // Get the WiFiPhy object at phy_index, or create a new WiFiPhy if there isn't
  // one.
  if (!base::Contains(wifi_phys_, phy_index)) {
    SLOG(2) << "Adding a new phy object at index: " << phy_index;
    wifi_phys_[phy_index] = std::make_unique<WiFiPhy>(phy_index);
  }
  // Forward the message to the WiFiPhy object.
  wifi_phys_[phy_index]->OnNewWiphy(nl80211_message);
  // If the phy's concurrency combinations are ready, see if we've got any
  // pending requests that can now be satisfied based on this new phy
  // information.
  if (!wifi_phys_[phy_index]->ConcurrencyCombinations().empty()) {
    ProcessDeviceRequests();
  }
}

void WiFiProvider::HandleNetlinkBroadcast(
    const net_base::NetlinkMessage& message) {
  if (message.message_type() != Nl80211Message::GetMessageType()) {
    SLOG(7) << __func__ << ": Not a NL80211 Message";
    return;
  }
  const Nl80211Message& nl80211_message =
      dynamic_cast<const Nl80211Message&>(message);
  uint32_t phy_index;
  if (!nl80211_message.const_attributes()->GetU32AttributeValue(
          NL80211_ATTR_WIPHY, &phy_index)) {
    return;
  }

  if ((nl80211_message.command() == NewWiphyMessage::kCommand)) {
    // Force a split phy dump to retrieve all the phy information as the
    // unsolicited new phy message will be truncated and incomplete.
    // Ref code:
    // https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/third_party/kernel/v5.15/net/wireless/nl80211.c;l=2547
    GetPhyInfo(phy_index);
    return;
  }

  if ((nl80211_message.command() == DelWiphyMessage::kCommand)) {
    wifi_phys_.erase(phy_index);
    manager_->RefreshTetheringCapabilities();
    return;
  }
  // The NL80211 message includes a phy index for which we have no associated
  // WiFiPhy object. Request the phy at this index to get us back in sync.
  // This is needed because the WiFi driver may not broadcast an
  // NL80211_CMD_NEW_WIPHY when a new phy comes online.
  if (!base::Contains(wifi_phys_, phy_index)) {
    SLOG(2) << "Recieved command " << nl80211_message.command_string()
            << " for unknown phy at index " << phy_index
            << " requesting phy info";
    GetPhyInfo(phy_index);
    return;
  }
}

std::string WiFiProvider::GetPrimaryLinkName() const {
  // TODO(b/269163735) Use WiFi device registered in WiFiPhy to get the primary
  // interface.
  const auto wifi_devices = manager_->FilterByTechnology(Technology::kWiFi);
  if (wifi_devices.empty()) {
    LOG(ERROR) << "No WiFi device available.";
    return "";
  }
  return wifi_devices.front().get()->link_name();
}

const WiFiPhy* WiFiProvider::GetPhyAtIndex(uint32_t phy_index) {
  if (!base::Contains(wifi_phys_, phy_index)) {
    return nullptr;
  }
  return wifi_phys_[phy_index].get();
}

std::vector<const WiFiPhy*> WiFiProvider::GetPhys() const {
  std::vector<const WiFiPhy*> phy_vec;
  for (auto& [idx, phy] : wifi_phys_) {
    phy_vec.push_back(phy.get());
  }
  return phy_vec;
}

void WiFiProvider::RegisterDeviceToPhy(WiFiConstRefPtr device,
                                       uint32_t phy_index) {
  CHECK(device);
  CHECK(base::Contains(wifi_phys_, phy_index))
      << "Tried to register WiFi device " << device->link_name()
      << " to phy_index: " << phy_index << " but the phy does not exist";
  SLOG(2) << "Registering WiFi device " << device->link_name()
          << " to phy_index: " << phy_index;
  wifi_phys_[phy_index]->AddWiFiDevice(device);
}

void WiFiProvider::DeregisterDeviceFromPhy(std::string_view link_name,
                                           uint32_t phy_index) {
  SLOG(2) << "Deregistering WiFi device " << link_name
          << " from phy_index: " << phy_index;
  if (base::Contains(wifi_phys_, phy_index) &&
      wifi_phys_[phy_index] != nullptr) {
    wifi_phys_[phy_index]->DeleteWiFiDevice(link_name);
  }
}

void WiFiProvider::WiFiDeviceStateChanged(WiFiConstRefPtr device) {
  if (base::Contains(wifi_phys_, device->phy_index())) {
    wifi_phys_[device->phy_index()]->WiFiDeviceStateChanged(device);
  }
  ProcessDeviceRequests();
}

void WiFiProvider::EnableDevice(WiFiRefPtr device,
                                bool persist,
                                ResultCallback callback) {
  if (device == nullptr) {
    return;
  }
  device->SetEnabledChecked(true, persist, std::move(callback));
}

void WiFiProvider::EnableDevices(std::vector<WiFiRefPtr> devices,
                                 bool persist,
                                 ResultCallback callback) {
  auto result_aggregator(base::MakeRefCounted<ResultAggregator>(
      std::move(callback), FROM_HERE, "Enable WiFi failed: "));
  // Track whether we actually queued up any requests. If we didn't, we'll need
  // to invoke the aggregator callback directly, per ResultAggregator
  // documentation.
  bool request_queued = false;
  for (auto& device : devices) {
    CHECK(device);
    if (device->enabled()) {
      // Don't bother queuing up a request for devices which are already
      // enabled.
      continue;
    }
    ResultCallback aggregator_callback(
        base::BindOnce(&ResultAggregator::ReportResult, result_aggregator));
    if (device->supplicant_state() !=
        WPASupplicant::kInterfaceStateInterfaceDisabled) {
      // The device is already considered "enabled" by Supplicant, but not by
      // Shill, so directly trigger enablement in Shill to correct this
      // misalignment. Bypass concurrency considerations since the device is
      // already considered to own the relevant device resoureces.
      EnableDevice(device, persist, std::move(aggregator_callback));
      continue;
    }
    // If we don't have a ready WiFiPhy for this device yet, just send the
    // request without considering concurrency. It'll only be processed once we
    // actually have the WiFiPhy and it is ready.
    if (wifi_phys_.contains(device->phy_index()) &&
        !wifi_phys_[device->phy_index()]->ConcurrencyCombinations().empty()) {
      WiFiPhy* phy = wifi_phys_[device->phy_index()].get();
      // TODO(b/345553305): Consider concurrency with all requested devices
      // together, rather than one at a time.
      auto ifaces_to_delete =
          phy->RequestNewIface(NL80211_IFTYPE_STATION, device->priority());
      if (!ifaces_to_delete || ifaces_to_delete->size() > 0) {
        LOG(INFO) << "Failed to enable device " << device->link_name()
                  << " due to concurrency conflict";
        std::move(aggregator_callback).Run(Error(Error::kOperationFailed));
        continue;
      }
    }
    // Create a PendingDeviceRequest for each device we want to enable.
    base::OnceCallback<void()> cb =
        base::BindOnce(&WiFiProvider::EnableDevice,
                       weak_ptr_factory_while_started_.GetWeakPtr(), device,
                       persist, std::move(aggregator_callback));
    PushPendingDeviceRequest(NL80211_IFTYPE_STATION, device->priority(),
                             std::move(cb));
    request_queued = true;
  }
  if (!request_queued) {
    ResultCallback aggregator_callback(
        base::BindOnce(&ResultAggregator::ReportResult, result_aggregator));
    std::move(aggregator_callback).Run(Error(Error::kSuccess));
    return;
  }
  ProcessDeviceRequests();
}

Metrics* WiFiProvider::metrics() const {
  return manager_->metrics();
}

WiFiProvider::PasspointMatch::PasspointMatch() = default;

WiFiProvider::PasspointMatch::PasspointMatch(
    const PasspointCredentialsRefPtr& cred_in,
    const WiFiEndpointRefPtr& endp_in,
    MatchPriority prio_in)
    : credentials(cred_in), endpoint(endp_in), priority(prio_in) {}

std::string WiFiProvider::GetUniqueLocalDeviceName(
    const std::string& iface_prefix) {
  uint8_t link_name_idx = 0;
  std::string link_name;
  do {
    link_name = iface_prefix + std::to_string(link_name_idx++);
  } while (base::Contains(local_devices_, link_name));

  return link_name;
}

void WiFiProvider::RegisterLocalDevice(LocalDeviceRefPtr device) {
  CHECK(device);
  CHECK(device->link_name())
      << "Tried to register a device without a link_name";
  uint32_t phy_index = device->phy_index();
  std::string link_name = *device->link_name();

  if (base::Contains(local_devices_, link_name)) {
    return;
  }

  CHECK(base::Contains(wifi_phys_, phy_index))
      << "Tried to register WiFi local device " << link_name
      << " to phy_index: " << phy_index << " but the phy does not exist";

  SLOG(2) << "Registering WiFi local device " << link_name
          << " to phy_index: " << phy_index;
  wifi_phys_[phy_index]->AddWiFiLocalDevice(device);

  local_devices_[link_name] = device;
  ProcessDeviceRequests();
}

void WiFiProvider::DeregisterLocalDevice(LocalDeviceConstRefPtr device) {
  CHECK(device);
  CHECK(device->link_name())
      << "Tried to deregister a device without a link_name";
  uint32_t phy_index = device->phy_index();
  std::string link_name = *device->link_name();

  SLOG(2) << "Deregistering WiFi local device " << link_name
          << " from phy_index: " << phy_index;
  if (base::Contains(wifi_phys_, phy_index)) {
    wifi_phys_[phy_index]->DeleteWiFiLocalDevice(device);
  }
  local_devices_.erase(link_name);
  ProcessDeviceRequests();
}

bool WiFiProvider::CreateHotspotDeviceForTest(
    const net_base::MacAddress mac_address,
    const std::string& device_name_for_test,
    uint32_t device_phy_index_for_test,
    LocalDevice::EventCallback callback) {
  const std::string link_name = device_name_for_test;
  HotspotDeviceRefPtr dev = hotspot_device_factory_.Run(
      manager_, device_name_for_test, link_name, mac_address,
      device_phy_index_for_test,
      WiFiPhy::Priority(WiFiPhy::Priority::kMinimumPriority), callback);
  if (dev->SetEnabled(true)) {
    RegisterLocalDevice(dev);
    manager_->tethering_manager()->OnDeviceCreated(dev);
    return true;
  } else {
    manager_->tethering_manager()->OnDeviceCreationFailed();
    return false;
  }
}

void WiFiProvider::CreateHotspotDevice(net_base::MacAddress mac_address,
                                       WiFiPhy::Priority priority,
                                       LocalDevice::EventCallback callback) {
  const std::string primary_link_name = GetPrimaryLinkName();
  if (primary_link_name.empty()) {
    LOG(ERROR) << "Failed to get primary link name.";
    manager_->tethering_manager()->OnDeviceCreationFailed();
    return;
  }
  if (wifi_phys_.empty()) {
    LOG(ERROR) << "No WiFiPhy available.";
    return;
  }
  const std::string link_name = GetUniqueLocalDeviceName(kHotspotIfacePrefix);
  // TODO(b/257340615) Select capable WiFiPhy according to band and security
  // requirement.
  const uint32_t phy_index = wifi_phys_.begin()->second->GetPhyIndex();

  HotspotDeviceRefPtr dev =
      hotspot_device_factory_.Run(manager_, primary_link_name, link_name,
                                  mac_address, phy_index, priority, callback);

  if (dev->SetEnabled(true)) {
    RegisterLocalDevice(dev);
    manager_->tethering_manager()->OnDeviceCreated(dev);
  } else {
    manager_->tethering_manager()->OnDeviceCreationFailed();
  }
}

void WiFiProvider::CreateP2PDevice(
    LocalDevice::IfaceType iface_type,
    LocalDevice::EventCallback callback,
    int32_t shill_id,
    WiFiPhy::Priority priority,
    base::OnceCallback<void(P2PDeviceRefPtr)> success_cb,
    base::OnceCallback<void()> fail_cb) {
  if (iface_type != LocalDevice::IfaceType::kP2PGO &&
      iface_type != LocalDevice::IfaceType::kP2PClient) {
    LOG(ERROR) << "Failed to create P2PDevice, invalid interface type: "
               << iface_type;
    std::move(fail_cb).Run();
    return;
  }
  if (wifi_phys_.empty()) {
    LOG(ERROR) << "No WiFiPhy available.";
    std::move(fail_cb).Run();
    return;
  }

  // TODO(b/257340615) Select capable WiFiPhy according to capabilities.
  uint32_t phy_index = wifi_phys_.begin()->second->GetPhyIndex();

  const std::string primary_link_name = GetPrimaryLinkName();
  if (primary_link_name.empty()) {
    LOG(ERROR) << "Failed to get primary link name.";
    std::move(fail_cb).Run();
    return;
  }

  P2PDeviceRefPtr dev = new P2PDevice(manager_, iface_type, primary_link_name,
                                      phy_index, shill_id, priority, callback);
  std::move(success_cb).Run(dev);
}

bool WiFiProvider::RequestLocalDeviceCreation(
    LocalDevice::IfaceType iface_type,
    WiFiPhy::Priority priority,
    base::OnceClosure create_device_cb) {
  if (wifi_phys_.empty()) {
    LOG(ERROR) << "No WiFiPhy available.";
    return false;
  }
  // TODO(b/257340615) Select capable WiFiPhy according to band and security
  // requirement.
  WiFiPhy* phy = wifi_phys_.begin()->second.get();
  // If the phy's concurrency support isn't ready, just return false
  // immediately.
  if (phy->ConcurrencyCombinations().empty()) {
    return false;
  }
  std::optional<nl80211_iftype> type =
      LocalDevice::IfaceTypeToNl80211Type(iface_type);
  if (!type) {
    LOG(ERROR) << "Invalid iface type requested " << iface_type;
    return false;
  }
  auto ifaces_to_delete = phy->RequestNewIface(*type, priority);
  if (!ifaces_to_delete || ifaces_to_delete->size() > 0) {
    return false;
  }
  manager_->dispatcher()->PostTask(FROM_HERE, std::move(create_device_cb));
  return true;
}

void WiFiProvider::DeleteLocalDevice(LocalDeviceRefPtr device) {
  device->SetEnabled(false);
  // It's impossible for a device without a link_name value to be registered,
  // so we can skip deregistration in that case.
  if (!device->link_name()) {
    return;
  }
  // If the device has a link_name, then we can only deregister it if it is
  // already registered.
  if (!base::Contains(local_devices_, *device->link_name())) {
    return;
  }
  DeregisterLocalDevice(device);
}

void WiFiProvider::SetRegDomain(std::string country) {
  CHECK(!country.empty()) << "Missing alpha2";

  ReqSetRegMessage set_reg;
  set_reg.attributes()->SetStringAttributeValue(NL80211_ATTR_REG_ALPHA2,
                                                country);
  LOG(INFO) << "Setting region change to: " << country;
  set_reg.Send(
      netlink_manager_,
      base::RepeatingCallback<void(const Nl80211Message&)>(),  //  null handler
      base::BindRepeating(&net_base::NetlinkManager::OnAckDoNothing),
      base::BindRepeating(&net_base::NetlinkManager::OnNetlinkMessageError));
}

void WiFiProvider::ResetRegDomain() {
  ReqSetRegMessage set_reg;
  set_reg.attributes()->SetStringAttributeValue(NL80211_ATTR_REG_ALPHA2,
                                                kWorldRegDomain);
  LOG(INFO) << "Resetting regulatory to world domain.";
  set_reg.Send(
      netlink_manager_,
      base::RepeatingCallback<void(const Nl80211Message&)>(),  //  null handler
      base::BindRepeating(&net_base::NetlinkManager::OnAckDoNothing),
      base::BindRepeating(&net_base::NetlinkManager::OnNetlinkMessageError));
}

void WiFiProvider::UpdateRegAndPhyInfo(base::OnceClosure phy_ready_callback) {
  auto cellular_country = manager_->GetCellularOperatorCountryCode();
  LOG(INFO) << __func__ << ": cellular country is "
            << cellular_country.value_or("null");
  if (cellular_country.has_value()) {
    phy_info_ready_cb_ = std::move(phy_ready_callback);
    SetRegDomain(cellular_country.value());
    phy_update_timeout_cb_.Reset(
        base::BindOnce(&WiFiProvider::PhyUpdateTimeout,
                       weak_ptr_factory_while_started_.GetWeakPtr()));

    manager_->dispatcher()->PostDelayedTask(
        FROM_HERE, phy_update_timeout_cb_.callback(), kPhyUpdateTimeout);
  } else {
    UpdatePhyInfo(std::move(phy_ready_callback));
  }
}

void WiFiProvider::UpdatePhyInfo(base::OnceClosure phy_ready_callback) {
  LOG(INFO) << __func__;
  phy_info_ready_cb_ = std::move(phy_ready_callback);
  phy_update_timeout_cb_.Reset(
      base::BindOnce(&WiFiProvider::PhyUpdateTimeout,
                     weak_ptr_factory_while_started_.GetWeakPtr()));
  manager_->dispatcher()->PostDelayedTask(
      FROM_HERE, phy_update_timeout_cb_.callback(), kPhyUpdateTimeout);
  GetPhyInfo(kAllPhys);
}

void WiFiProvider::PhyUpdateTimeout() {
  LOG(WARNING) << "Timed out waiting for RegChange/PhyDump - proceeding with "
                  "current info.";
  if (phy_info_ready_cb_) {
    std::move(phy_info_ready_cb_).Run();
  }
}

void WiFiProvider::RegionChanged(const std::string& country) {
  LOG(INFO) << __func__ << ": Country notification: " << country;
  GetPhyInfo(kAllPhys);
}

void WiFiProvider::OnGetPhyInfoAuxMessage(
    uint32_t phy_index,
    net_base::NetlinkManager::AuxiliaryMessageType type,
    const net_base::NetlinkMessage* raw_message) {
  if (type != net_base::NetlinkManager::kDone) {
    net_base::NetlinkManager::OnNetlinkMessageError(type, raw_message);
    return;
  }
  // Signal the end of dump.
  PhyDumpComplete(phy_index);
  manager_->RefreshTetheringCapabilities();

  if (!phy_update_timeout_cb_.IsCancelled()) {
    phy_update_timeout_cb_.Cancel();
  }
  if (phy_info_ready_cb_) {
    manager_->dispatcher()->PostTask(FROM_HERE, std::move(phy_info_ready_cb_));
  }
}

void WiFiProvider::PushPendingDeviceRequest(
    nl80211_iftype type,
    WiFiPhy::Priority priority,
    base::OnceClosure create_device_cb) {
  request_queue_.insert(std::shared_ptr<WiFiProvider::PendingDeviceRequest>(
      new WiFiProvider::PendingDeviceRequest(type, priority,
                                             std::move(create_device_cb))));
}

void WiFiProvider::ProcessDeviceRequests() {
  if (wifi_phys_.empty()) {
    return;
  }
  // TODO(b/257340615) Select capable WiFiPhy according to band and security
  // requirement.
  WiFiPhy* phy = wifi_phys_.begin()->second.get();
  auto request = request_queue_.begin();
  while (request != request_queue_.end()) {
    auto ifaces_to_delete =
        phy->RequestNewIface((*request)->type, (*request)->priority);
    if (ifaces_to_delete && ifaces_to_delete->size() == 0) {
      if ((*request)->create_device_cb) {
        std::move((*request)->create_device_cb).Run();
      }
      request_queue_.erase(request);
      return;
    }
    request++;
  }
}

LocalDeviceConstRefPtr WiFiProvider::GetLowestPriorityLocalDeviceOfType(
    std::set<LocalDeviceConstRefPtr> devices, LocalDevice::IfaceType type) {
  LocalDeviceConstRefPtr candidate_dev;
  for (auto dev : devices) {
    if (dev->iface_type() == type &&
        (!candidate_dev || candidate_dev->priority() > dev->priority())) {
      candidate_dev = dev;
    }
  }
  return candidate_dev;
}

WiFiConstRefPtr WiFiProvider::GetLowestPriorityEnabledWiFiDevice(
    std::set<WiFiConstRefPtr> devices) {
  WiFiConstRefPtr candidate_dev;
  for (auto dev : devices) {
    if (dev->supplicant_state() !=
            WPASupplicant::kInterfaceStateInterfaceDisabled &&
        (!candidate_dev || candidate_dev->priority() > dev->priority())) {
      candidate_dev = dev;
    }
  }
  return candidate_dev;
}

bool WiFiProvider::BringDownDevicesByType(std::multiset<nl80211_iftype> types) {
  if (wifi_phys_.empty()) {
    return false;
  }
  WiFiPhy* phy = wifi_phys_.begin()->second.get();
  // Snapshot local and WiFi devices so that we can ensure a given device is
  // only selected once.
  auto local_devices = phy->LocalDevices();
  auto wifi_devices = phy->GetWiFiDevices();
  for (auto type : types) {
    switch (type) {
      case NL80211_IFTYPE_P2P_GO:
      case NL80211_IFTYPE_P2P_CLIENT: {
        LocalDeviceConstRefPtr dev = GetLowestPriorityLocalDeviceOfType(
            local_devices, type == NL80211_IFTYPE_P2P_GO
                               ? LocalDevice::IfaceType::kP2PGO
                               : LocalDevice::IfaceType::kP2PClient);
        if (!dev) {
          return false;
        }
        local_devices.erase(dev);
        p2p_manager_->DeviceTeardownOnResourceBusy(
            static_cast<const P2PDevice*>(dev.get())->shill_id());
        break;
      }
      case NL80211_IFTYPE_AP: {
        LocalDeviceConstRefPtr dev = GetLowestPriorityLocalDeviceOfType(
            local_devices, LocalDevice::IfaceType::kAP);
        if (!dev) {
          return false;
        }
        local_devices.erase(dev);
        manager_->tethering_manager()->StopOnResourceBusy();
        break;
      }
      case NL80211_IFTYPE_STATION: {
        WiFiConstRefPtr dev = GetLowestPriorityEnabledWiFiDevice(wifi_devices);
        if (!dev) {
          return false;
        }
        wifi_devices.erase(dev);
        // Get a mutable reference to dev.
        DeviceRefPtr mutable_dev;
        for (auto wifi_device :
             manager_->FilterByTechnology(Technology::kWiFi)) {
          if (wifi_device->interface_index() == dev->interface_index()) {
            mutable_dev = wifi_device;
          }
        }
        if (!mutable_dev) {
          return false;
        }
        mutable_dev->SetEnabled(false);
        base::OnceClosure cb =
            base::BindOnce(&WiFiProvider::EnableDevice,
                           weak_ptr_factory_while_started_.GetWeakPtr(),
                           WiFiRefPtr(static_cast<WiFi*>(mutable_dev.get())),
                           false, base::DoNothing());
        // Queue up a request to re-enable the device so it will be enabled as
        // soon as the resources are available.
        PushPendingDeviceRequest(NL80211_IFTYPE_STATION, dev->priority(),
                                 std::move(cb));
        break;
      }
      default:
        NOTREACHED();
    }
  }
  return true;
}

}  // namespace shill
