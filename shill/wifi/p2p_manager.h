// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_P2P_MANAGER_H_
#define SHILL_WIFI_P2P_MANAGER_H_

#include <string>
#include <string_view>

#include "shill/error.h"
#include "shill/store/key_value_store.h"
#include "shill/store/property_store.h"

namespace shill {

class Manager;
class StoreInterface;

// P2PManager handles P2P related logic. It is created by the Manager class.
class P2PManager {
 public:
  explicit P2PManager(Manager* manager);
  P2PManager(const P2PManager&) = delete;
  P2PManager& operator=(const P2PManager&) = delete;

  virtual ~P2PManager();

  // Initialize D-Bus properties related to P2P.
  void InitPropertyStore(PropertyStore* store);
  // Start P2PManager.
  void Start();
  // Stop P2PManager.
  void Stop();

  // D-Bus property getters
  // This property is temporary and will be removed when the feature is mature.
  bool allowed() const { return allowed_; }

 private:
  friend class P2PManagerTest;
  void HelpRegisterDerivedBool(PropertyStore* store,
                               std::string_view name,
                               bool (P2PManager::*get)(Error* error),
                               bool (P2PManager::*set)(const bool&, Error*));
  // D-Bus accessors.
  bool SetAllowed(const bool& value, Error* error);
  bool GetAllowed(Error* /*error*/) { return allowed_; }

  // Reference to the main Shill Manager instance. P2PManager is created and
  // owned by WiFiProvider, which can be accessed indirectly through manager_.
  Manager* manager_;
  // P2P feature flag.
  bool allowed_;
};

}  // namespace shill

#endif  // SHILL_WIFI_P2P_MANAGER_H_
