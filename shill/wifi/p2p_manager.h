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

  // The following methods are used to handle P2P-related actions.

  // Create a new P2P group-owner mode interface and initialize a P2P group on
  // that interface.
  void CreateP2PGroup(base::OnceCallback<void(KeyValueStore result)> callback,
                      const KeyValueStore& args);

  // Creates a P2P client mode interface to and connects it to an existing P2P
  // group.
  void ConnectToP2PGroup(
      base::OnceCallback<void(KeyValueStore result)> callback,
      const KeyValueStore& args);

  // Destroy the existing P2P group and tear down the P2P group-owner interface.
  void DestroyP2PGroup(base::OnceCallback<void(KeyValueStore result)> callback,
                       int shill_id);

  // Disconnect from a P2P group. Will destroy the P2P client interface.
  void DisconnectFromP2PGroup(
      base::OnceCallback<void(KeyValueStore result)> callback, int shill_id);

  // D-Bus property getters
  // This property is temporary and will be removed when the feature is mature.
  bool allowed() const { return allowed_; }

 private:
  friend class P2PManagerTest;
  FRIEND_TEST(P2PManagerTest, SetP2PAllowed);

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
