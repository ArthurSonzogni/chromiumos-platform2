// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/supplicant_interface_proxy.h"

#include <string>
#include <utility>

#include <base/bind.h>

#include "shill/logging.h"
#include "shill/supplicant/supplicant_event_delegate_interface.h"
#include "shill/supplicant/wpa_supplicant.h"

using std::string;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDBus;
static string ObjectID(const dbus::ObjectPath* p) {
  return p->value();
}
}  // namespace Logging

const char SupplicantInterfaceProxy::kInterfaceName[] =
    "fi.w1.wpa_supplicant1.Interface";
const char SupplicantInterfaceProxy::kPropertyFastReauth[] = "FastReauth";
const char SupplicantInterfaceProxy::kPropertyRoamThreshold[] = "RoamThreshold";
const char SupplicantInterfaceProxy::kPropertyScan[] = "Scan";
const char SupplicantInterfaceProxy::kPropertyScanInterval[] = "ScanInterval";
const char SupplicantInterfaceProxy::kPropertySchedScan[] = "SchedScan";
const char SupplicantInterfaceProxy::kPropertyMacAddressRandomizationMask[] =
    "MACAddressRandomizationMask";

SupplicantInterfaceProxy::PropertySet::PropertySet(
    dbus::ObjectProxy* object_proxy,
    const std::string& interface_name,
    const PropertyChangedCallback& callback)
    : dbus::PropertySet(object_proxy, interface_name, callback) {
  RegisterProperty(kPropertyFastReauth, &fast_reauth);
  RegisterProperty(kPropertyRoamThreshold, &roam_threshold);
  RegisterProperty(kPropertyScan, &scan);
  RegisterProperty(kPropertyScanInterval, &scan_interval);
  RegisterProperty(kPropertySchedScan, &sched_scan);
  RegisterProperty(kPropertyMacAddressRandomizationMask,
                   &mac_address_randomization_mask);
}

SupplicantInterfaceProxy::SupplicantInterfaceProxy(
    const scoped_refptr<dbus::Bus>& bus,
    const RpcIdentifier& object_path,
    SupplicantEventDelegateInterface* delegate)
    : interface_proxy_(new fi::w1::wpa_supplicant1::InterfaceProxy(
          bus, WPASupplicant::kDBusAddr, dbus::ObjectPath(object_path))),
      delegate_(delegate) {
  // Register properites.
  properties_.reset(
      new PropertySet(interface_proxy_->GetObjectProxy(), kInterfaceName,
                      base::Bind(&SupplicantInterfaceProxy::OnPropertyChanged,
                                 weak_factory_.GetWeakPtr())));

  // Register signal handlers.
  dbus::ObjectProxy::OnConnectedCallback on_connected_callback = base::Bind(
      &SupplicantInterfaceProxy::OnSignalConnected, weak_factory_.GetWeakPtr());
  interface_proxy_->RegisterScanDoneSignalHandler(
      base::Bind(&SupplicantInterfaceProxy::ScanDone,
                 weak_factory_.GetWeakPtr()),
      on_connected_callback);
  interface_proxy_->RegisterBSSAddedSignalHandler(
      base::Bind(&SupplicantInterfaceProxy::BSSAdded,
                 weak_factory_.GetWeakPtr()),
      on_connected_callback);
  interface_proxy_->RegisterBSSRemovedSignalHandler(
      base::Bind(&SupplicantInterfaceProxy::BSSRemoved,
                 weak_factory_.GetWeakPtr()),
      on_connected_callback);
  interface_proxy_->RegisterBlobAddedSignalHandler(
      base::Bind(&SupplicantInterfaceProxy::BlobAdded,
                 weak_factory_.GetWeakPtr()),
      on_connected_callback);
  interface_proxy_->RegisterBlobRemovedSignalHandler(
      base::Bind(&SupplicantInterfaceProxy::BlobRemoved,
                 weak_factory_.GetWeakPtr()),
      on_connected_callback);
  interface_proxy_->RegisterCertificationSignalHandler(
      base::Bind(&SupplicantInterfaceProxy::Certification,
                 weak_factory_.GetWeakPtr()),
      on_connected_callback);
  interface_proxy_->RegisterEAPSignalHandler(
      base::Bind(&SupplicantInterfaceProxy::EAP, weak_factory_.GetWeakPtr()),
      on_connected_callback);
  interface_proxy_->RegisterNetworkAddedSignalHandler(
      base::Bind(&SupplicantInterfaceProxy::NetworkAdded,
                 weak_factory_.GetWeakPtr()),
      on_connected_callback);
  interface_proxy_->RegisterNetworkRemovedSignalHandler(
      base::Bind(&SupplicantInterfaceProxy::NetworkRemoved,
                 weak_factory_.GetWeakPtr()),
      on_connected_callback);
  interface_proxy_->RegisterNetworkSelectedSignalHandler(
      base::Bind(&SupplicantInterfaceProxy::NetworkSelected,
                 weak_factory_.GetWeakPtr()),
      on_connected_callback);
  interface_proxy_->RegisterPropertiesChangedSignalHandler(
      base::Bind(&SupplicantInterfaceProxy::PropertiesChanged,
                 weak_factory_.GetWeakPtr()),
      on_connected_callback);
  interface_proxy_->RegisterTDLSDiscoverResponseSignalHandler(
      base::Bind(&SupplicantInterfaceProxy::TDLSDiscoverResponse,
                 weak_factory_.GetWeakPtr()),
      on_connected_callback);

  // Connect property signals and initialize cached values. Based on
  // recommendations from src/dbus/property.h.
  properties_->ConnectSignals();
  properties_->GetAll();
}

SupplicantInterfaceProxy::~SupplicantInterfaceProxy() {
  interface_proxy_->ReleaseObjectProxy(base::Bind(&base::DoNothing));
}

bool SupplicantInterfaceProxy::AddNetwork(const KeyValueStore& args,
                                          RpcIdentifier* network) {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__;
  brillo::VariantDictionary dict =
      KeyValueStore::ConvertToVariantDictionary(args);
  dbus::ObjectPath path;
  brillo::ErrorPtr error;
  if (!interface_proxy_->AddNetwork(dict, &path, &error)) {
    LOG(ERROR) << "Failed to add network: " << error->GetCode() << " "
               << error->GetMessage();
    return false;
  }
  *network = path.value();
  return true;
}

bool SupplicantInterfaceProxy::EAPLogoff() {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__;
  brillo::ErrorPtr error;
  if (!interface_proxy_->EAPLogoff(&error)) {
    LOG(ERROR) << "Failed to EPA logoff " << error->GetCode() << " "
               << error->GetMessage();
    return false;
  }
  return true;
}

bool SupplicantInterfaceProxy::EAPLogon() {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__;
  brillo::ErrorPtr error;
  if (!interface_proxy_->EAPLogon(&error)) {
    LOG(ERROR) << "Failed to EAP logon: " << error->GetCode() << " "
               << error->GetMessage();
    return false;
  }
  return true;
}

bool SupplicantInterfaceProxy::Disconnect() {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__;
  brillo::ErrorPtr error;
  if (!interface_proxy_->Disconnect(&error)) {
    LOG(ERROR) << "Failed to disconnect: " << error->GetCode() << " "
               << error->GetMessage();
    return false;
  }
  return true;
}

bool SupplicantInterfaceProxy::FlushBSS(const uint32_t& age) {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__;
  brillo::ErrorPtr error;
  if (!interface_proxy_->FlushBSS(age, &error)) {
    LOG(ERROR) << "Failed to flush BSS: " << error->GetCode() << " "
               << error->GetMessage();
    return false;
  }
  return true;
}

bool SupplicantInterfaceProxy::NetworkReply(const RpcIdentifier& network,
                                            const string& field,
                                            const string& value) {
  SLOG(&interface_proxy_->GetObjectPath(), 2)
      << __func__ << " network: " << network << " field: " << field
      << " value: " << value;
  brillo::ErrorPtr error;
  if (!interface_proxy_->NetworkReply(dbus::ObjectPath(network), field, value,
                                      &error)) {
    LOG(ERROR) << "Failed to network reply: " << error->GetCode() << " "
               << error->GetMessage();
    return false;
  }
  return true;
}

bool SupplicantInterfaceProxy::Roam(const string& addr) {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__;
  brillo::ErrorPtr error;
  if (!interface_proxy_->Roam(addr, &error)) {
    LOG(ERROR) << "Failed to Roam: " << error->GetCode() << " "
               << error->GetMessage();
    return false;
  }
  return true;
}

bool SupplicantInterfaceProxy::Reassociate() {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__;
  brillo::ErrorPtr error;
  if (!interface_proxy_->Reassociate(&error)) {
    LOG(ERROR) << "Failed to reassociate: " << error->GetCode() << " "
               << error->GetMessage();
    return false;
  }
  return true;
}

bool SupplicantInterfaceProxy::Reattach() {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__;
  brillo::ErrorPtr error;
  if (!interface_proxy_->Reattach(&error)) {
    LOG(ERROR) << "Failed to reattach: " << error->GetCode() << " "
               << error->GetMessage();
    return false;
  }
  return true;
}

bool SupplicantInterfaceProxy::RemoveAllNetworks() {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__;
  brillo::ErrorPtr error;
  if (!interface_proxy_->RemoveAllNetworks(&error)) {
    LOG(ERROR) << "Failed to remove all networks: " << error->GetCode() << " "
               << error->GetMessage();
    return false;
  }
  return true;
}

bool SupplicantInterfaceProxy::RemoveNetwork(const RpcIdentifier& network) {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__ << ": " << network;
  brillo::ErrorPtr error;
  if (!interface_proxy_->RemoveNetwork(dbus::ObjectPath(network), &error)) {
    LOG(ERROR) << "Failed to remove network: " << error->GetCode() << " "
               << error->GetMessage();
    // RemoveNetwork can fail with three different errors.
    //
    // If RemoveNetwork fails with a NetworkUnknown error, supplicant has
    // already removed the network object, so return true as if
    // RemoveNetwork removes the network object successfully.
    //
    // As shill always passes a valid network object path, RemoveNetwork
    // should not fail with an InvalidArgs error. Return false in such case
    // as something weird may have happened. Similarly, return false in case
    // of an UnknownError.
    if (error->GetCode() != WPASupplicant::kErrorNetworkUnknown) {
      return false;
    }
  }
  return true;
}

bool SupplicantInterfaceProxy::Scan(const KeyValueStore& args) {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__;
  brillo::VariantDictionary dict =
      KeyValueStore::ConvertToVariantDictionary(args);
  brillo::ErrorPtr error;
  if (!interface_proxy_->Scan(dict, &error)) {
    LOG(ERROR) << "Failed to scan: " << error->GetCode() << " "
               << error->GetMessage();
    return false;
  }
  return true;
}

bool SupplicantInterfaceProxy::SelectNetwork(const RpcIdentifier& network) {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__ << ": " << network;
  brillo::ErrorPtr error;
  if (!interface_proxy_->SelectNetwork(dbus::ObjectPath(network), &error)) {
    LOG(ERROR) << "Failed to select network: " << error->GetCode() << " "
               << error->GetMessage();
    return false;
  }
  return true;
}

bool SupplicantInterfaceProxy::SetHT40Enable(const RpcIdentifier& network,
                                             bool enable) {
  SLOG(&interface_proxy_->GetObjectPath(), 2)
      << __func__ << " network: " << network << " enable: " << enable;
  brillo::ErrorPtr error;
  if (!interface_proxy_->SetHT40Enable(dbus::ObjectPath(network), enable,
                                       &error)) {
    LOG(ERROR) << "Failed to set HT40 enable: " << error->GetCode() << " "
               << error->GetMessage();
    return false;
  }
  return true;
}

bool SupplicantInterfaceProxy::EnableMacAddressRandomization(
    const std::vector<unsigned char>& mask) {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__;
  brillo::ErrorPtr error;
  // The MACRandomizationMask property is a map(type_string, ipmask_array)
  // where type_string is scan type ("scan" || "sched_scan" || "pno") and
  // ipmask specifies the corresponding mask as an array of bytes.
  std::map<std::string, std::vector<uint8_t>> mac_randomization_args;
  mac_randomization_args.insert(
      std::pair<std::string, std::vector<uint8_t>>("scan", mask));
  mac_randomization_args.insert(
      std::pair<std::string, std::vector<uint8_t>>("sched_scan", mask));

  // First try setting the MACRandomizationMask property
  // (wpa_supplicant-2.8 interface).
  // If that fails, try the EnableMACAddressRandomization method
  // (wpa_supplicant-2.6 interface).
  // TODO(crbug.com/985122): Remove supplicant-2.6 method call after
  // uprev to supplicant-2.8 is complete.
  if (!(properties_->mac_address_randomization_mask.SetAndBlock(
            mac_randomization_args) ||
        interface_proxy_->EnableMACAddressRandomization(mask, &error))) {
    LOG(ERROR) << "Failed to enable MAC address randomization: "
               << error->GetCode() << " " << error->GetMessage();
    return false;
  }
  return true;
}

bool SupplicantInterfaceProxy::DisableMacAddressRandomization() {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__;
  brillo::ErrorPtr error;
  // Send an empty map to disable Randomization for all scan types.
  std::map<std::string, std::vector<uint8_t>> mac_randomization_empty;

  // First try setting the MACRandomizationMask property
  // (wpa_supplicant-2.8 interface).
  // If that fails, try the DisableMACAddressRandomization method
  // (wpa_supplicant-2.6 interface).
  // TODO(crbug.com/985122): Remove supplicant-2.6 method call after
  // uprev to supplicant-2.8 is complete.
  if (!(properties_->mac_address_randomization_mask.SetAndBlock(
            mac_randomization_empty) ||
        interface_proxy_->DisableMACAddressRandomization(&error))) {
    LOG(ERROR) << "Failed to enable MAC address randomization: "
               << error->GetCode() << " " << error->GetMessage();
    return false;
  }
  return true;
}

bool SupplicantInterfaceProxy::TDLSDiscover(const string& peer) {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__ << ": " << peer;
  brillo::ErrorPtr error;
  if (!interface_proxy_->TDLSDiscover(peer, &error)) {
    LOG(ERROR) << "Failed to perform TDLS discover: " << error->GetCode() << " "
               << error->GetMessage();
    return false;
  }
  return true;
}

bool SupplicantInterfaceProxy::TDLSSetup(const string& peer) {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__ << ": " << peer;
  brillo::ErrorPtr error;
  if (!interface_proxy_->TDLSSetup(peer, &error)) {
    LOG(ERROR) << "Failed to perform TDLS setup: " << error->GetCode() << " "
               << error->GetMessage();
    return false;
  }
  return true;
}

bool SupplicantInterfaceProxy::TDLSStatus(const string& peer, string* status) {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__ << ": " << peer;
  brillo::ErrorPtr error;
  if (!interface_proxy_->TDLSStatus(peer, status, &error)) {
    LOG(ERROR) << "Failed to retrieve TDLS status: " << error->GetCode() << " "
               << error->GetMessage();
    return false;
  }
  return true;
}

bool SupplicantInterfaceProxy::TDLSTeardown(const string& peer) {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__ << ": " << peer;
  brillo::ErrorPtr error;
  if (!interface_proxy_->TDLSTeardown(peer, &error)) {
    LOG(ERROR) << "Failed to perform TDLS teardown: " << error->GetCode() << " "
               << error->GetMessage();
    return false;
  }
  return true;
}

bool SupplicantInterfaceProxy::SetFastReauth(bool enabled) {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__ << ": " << enabled;
  if (!properties_->fast_reauth.SetAndBlock(enabled)) {
    LOG(ERROR) << __func__ << " failed: " << enabled;
    return false;
  }
  return true;
}

bool SupplicantInterfaceProxy::SetRoamThreshold(uint16_t threshold) {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__ << ": " << threshold;
  if (!properties_->roam_threshold.SetAndBlock(threshold)) {
    LOG(ERROR) << __func__ << " failed: " << threshold;
    return false;
  }
  return true;
}

bool SupplicantInterfaceProxy::SetScanInterval(int32_t scan_interval) {
  SLOG(&interface_proxy_->GetObjectPath(), 2)
      << __func__ << ": " << scan_interval;
  if (!properties_->scan_interval.SetAndBlock(scan_interval)) {
    LOG(ERROR) << __func__ << " failed: " << scan_interval;
    return false;
  }
  return true;
}

bool SupplicantInterfaceProxy::SetScan(bool enable) {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__ << ": " << enable;
  if (!properties_->scan.SetAndBlock(enable)) {
    LOG(ERROR) << __func__ << " failed: " << enable;
    return false;
  }
  return true;
}

void SupplicantInterfaceProxy::BlobAdded(const string& /*blobname*/) {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__;
  // XXX
}

void SupplicantInterfaceProxy::BlobRemoved(const string& /*blobname*/) {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__;
  // XXX
}

void SupplicantInterfaceProxy::BSSAdded(
    const dbus::ObjectPath& BSS, const brillo::VariantDictionary& properties) {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__;
  KeyValueStore store = KeyValueStore::ConvertFromVariantDictionary(properties);
  delegate_->BSSAdded(BSS.value(), store);
}

void SupplicantInterfaceProxy::Certification(
    const brillo::VariantDictionary& properties) {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__;
  KeyValueStore store = KeyValueStore::ConvertFromVariantDictionary(properties);
  delegate_->Certification(store);
}

void SupplicantInterfaceProxy::EAP(const string& status,
                                   const string& parameter) {
  SLOG(&interface_proxy_->GetObjectPath(), 2)
      << __func__ << ": status " << status << ", parameter " << parameter;
  delegate_->EAPEvent(status, parameter);
}

void SupplicantInterfaceProxy::BSSRemoved(const dbus::ObjectPath& BSS) {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__;
  delegate_->BSSRemoved(BSS.value());
}

void SupplicantInterfaceProxy::NetworkAdded(
    const dbus::ObjectPath& /*network*/,
    const brillo::VariantDictionary& /*properties*/) {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__;
  // XXX
}

void SupplicantInterfaceProxy::NetworkRemoved(
    const dbus::ObjectPath& /*network*/) {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__;
  // TODO(quiche): Pass this up to the delegate, so that it can clean its
  // rpcid_by_service_ map. crbug.com/207648
}

void SupplicantInterfaceProxy::NetworkSelected(
    const dbus::ObjectPath& /*network*/) {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__;
  // XXX
}

void SupplicantInterfaceProxy::PropertiesChanged(
    const brillo::VariantDictionary& properties) {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__;
  KeyValueStore store = KeyValueStore::ConvertFromVariantDictionary(properties);
  delegate_->PropertiesChanged(store);
}

void SupplicantInterfaceProxy::ScanDone(bool success) {
  SLOG(&interface_proxy_->GetObjectPath(), 2) << __func__ << ": " << success;
  delegate_->ScanDone(success);
}

void SupplicantInterfaceProxy::TDLSDiscoverResponse(
    const std::string& peer_address) {
  SLOG(&interface_proxy_->GetObjectPath(), 2)
      << __func__ << ": " << peer_address;
  delegate_->TDLSDiscoverResponse(peer_address);
}

void SupplicantInterfaceProxy::OnPropertyChanged(
    const std::string& property_name) {
  SLOG(&interface_proxy_->GetObjectPath(), 2)
      << __func__ << ": " << property_name;
}

void SupplicantInterfaceProxy::OnSignalConnected(const string& interface_name,
                                                 const string& signal_name,
                                                 bool success) {
  SLOG(&interface_proxy_->GetObjectPath(), 2)
      << __func__ << "interface: " << interface_name
      << " signal: " << signal_name << "success: " << success;
  if (!success) {
    LOG(ERROR) << "Failed to connect signal " << signal_name << " to interface "
               << interface_name;
  }
}

}  // namespace shill
