// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_STORAGE_MANAGER_H_
#define FEDERATED_STORAGE_MANAGER_H_

#include <memory>
#include <string>

#include <base/no_destructor.h>
#include <base/optional.h>
#include <base/sequence_checker.h>

#include "federated/example_database.h"
#include "federated/session_manager_observer_interface.h"
#include "federated/session_manager_proxy.h"

namespace dbus {
class Bus;
}  // namespace dbus

namespace federated {

class SessionManagerProxy;

// Singleton class providing storage to satisfy federated service interface
// which receives new examples and federated computation interface which
// consumes examples for training/analytics.
class StorageManager : public SessionManagerObserverInterface {
 public:
  // Constructor is protected to disallow direct instantiation.
  ~StorageManager() override;

  static StorageManager* GetInstance();

  // Virtual for mocking.
  virtual void InitializeSessionManagerProxy(dbus::Bus* bus);
  virtual bool OnExampleReceived(const std::string& client_name,
                                 const std::string& serialized_example);
  virtual base::Optional<ExampleDatabase::Iterator> GetExampleIterator(
      const std::string& client_name) const;

 protected:
  // NoDestructor needs access to constructor.
  friend class base::NoDestructor<StorageManager>;

  StorageManager();
  StorageManager(const StorageManager&) = delete;
  StorageManager& operator=(const StorageManager&) = delete;

 private:
  friend class StorageManagerTest;

  void set_example_database_for_testing(ExampleDatabase* example_database) {
    example_database_.reset(example_database);
  }

  // SessionManagerObserverInterface:
  void OnSessionStarted() override;
  void OnSessionStopped() override;

  void ConnectToDatabaseIfNecessary();

  // Session manager that notifies session state changes.
  std::unique_ptr<SessionManagerProxy> session_manager_proxy_;

  // The database connection.
  std::unique_ptr<ExampleDatabase> example_database_;

  // Current login user hash. The database is connected to
  // /run/daemon-store/federated/<sanitized_username_>/examples.db.
  std::string sanitized_username_;

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace federated

#endif  // FEDERATED_STORAGE_MANAGER_H_
