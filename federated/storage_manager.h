// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_STORAGE_MANAGER_H_
#define FEDERATED_STORAGE_MANAGER_H_

#include <string>

#include <base/optional.h>

#include "federated/example_database.h"

namespace dbus {
class Bus;
}  // namespace dbus

namespace federated {
// Singleton class providing storage to satisfy federated service interface
// which receives new examples and federated computation interface which
// consumes examples for training/analytics.
class StorageManager {
 public:
  static StorageManager* GetInstance();

  // StorageManager connects/disconnects the database on session state changes,
  // so it needs to register itself as an observer of session manager.
  // The passed-in dbus::Bus* is owned by daemon.
  virtual void InitializeSessionManagerProxy(dbus::Bus* bus) = 0;

  virtual bool OnExampleReceived(const std::string& client_name,
                                 const std::string& serialized_example) = 0;

  virtual base::Optional<ExampleDatabase::Iterator> GetExampleIterator(
      const std::string& client_name) const = 0;

 protected:
  StorageManager() = default;
  virtual ~StorageManager() = default;
};

}  // namespace federated

#endif  // FEDERATED_STORAGE_MANAGER_H_
