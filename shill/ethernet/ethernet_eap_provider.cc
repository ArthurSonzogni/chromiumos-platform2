// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/ethernet/ethernet_eap_provider.h"

#include <string>

#include "shill/ethernet/ethernet_eap_service.h"
#include "shill/manager.h"

using std::string;

namespace shill {

EthernetEapProvider::EthernetEapProvider(ControlInterface *control_interface,
                                         EventDispatcher *dispatcher,
                                         Metrics *metrics,
                                         Manager *manager)
    : control_interface_(control_interface),
      dispatcher_(dispatcher),
      metrics_(metrics),
      manager_(manager) {}

EthernetEapProvider::~EthernetEapProvider() {}

void EthernetEapProvider::CreateServicesFromProfile(
    const ProfileRefPtr &profile) {
  // Since the EthernetEapProvider's service is created during Start(),
  // there is no need to do anything in this method.
}

ServiceRefPtr EthernetEapProvider::FindSimilarService(const KeyValueStore &args,
                                                      Error *error) const {
  CHECK_EQ(kTypeEthernetEap, args.LookupString(kTypeProperty, ""))
      << "Service type must be Ethernet EAP!";
  return service();
}

ServiceRefPtr EthernetEapProvider::GetService(const KeyValueStore &args,
                                              Error *error) {
  return FindSimilarService(args, error);
}

ServiceRefPtr EthernetEapProvider::CreateTemporaryService(
    const KeyValueStore &args,
    Error *error) {
  return new EthernetEapService(control_interface_,
                                dispatcher_,
                                metrics_,
                                manager_);
}

void EthernetEapProvider::Start() {
  if (!service_) {
    service_ = new EthernetEapService(control_interface_,
                                      dispatcher_,
                                      metrics_,
                                      manager_);
  }
  manager_->RegisterService(service_);
}

void EthernetEapProvider::Stop() {
  if (service_) {
    manager_->DeregisterService(service_);
  }
  // Do not destroy the service, since devices may or may not have been
  // removed as the provider is stopped, and we'd like them to continue
  // to refer to the same service on restart.
}

void EthernetEapProvider::SetCredentialChangeCallback(
    Ethernet *device, CredentialChangeCallback callback) {
  callback_map_[device] = callback;
}

void EthernetEapProvider::ClearCredentialChangeCallback(Ethernet *device) {
  CallbackMap::iterator it = callback_map_.find(device);
  if (it != callback_map_.end()) {
    callback_map_.erase(it);
  }
}

void EthernetEapProvider::OnCredentialsChanged() const {
  CallbackMap::const_iterator it;
  for (it = callback_map_.begin(); it != callback_map_.end(); ++it) {
    CHECK(!it->second.is_null());
    it->second.Run();
  }
}

}  // namespace shill
