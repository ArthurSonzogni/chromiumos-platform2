// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/p2p_manager.h"

#include <ios>
#include <string>

#include <base/logging.h>
#include <chromeos/dbus/shill/dbus-constants.h>

#include "shill/error.h"
#include "shill/manager.h"
#include "shill/store/property_accessor.h"

namespace shill {

P2PManager::P2PManager(Manager* manager) : manager_(manager), allowed_(false) {}

P2PManager::~P2PManager() = default;

void P2PManager::InitPropertyStore(PropertyStore* store) {
  HelpRegisterDerivedBool(store, kP2PAllowedProperty, &P2PManager::GetAllowed,
                          &P2PManager::SetAllowed);
}

void P2PManager::Start() {}

void P2PManager::Stop() {}

void P2PManager::HelpRegisterDerivedBool(PropertyStore* store,
                                         std::string_view name,
                                         bool (P2PManager::*get)(Error* error),
                                         bool (P2PManager::*set)(const bool&,
                                                                 Error*)) {
  store->RegisterDerivedBool(
      name, BoolAccessor(new CustomAccessor<P2PManager, bool>(this, get, set)));
}

bool P2PManager::SetAllowed(const bool& value, Error* error) {
  if (allowed_ == value)
    return false;

  LOG(INFO) << __func__ << " Allowed set to " << std::boolalpha << value;
  allowed_ = value;
  Stop();
  return true;
}

}  // namespace shill
