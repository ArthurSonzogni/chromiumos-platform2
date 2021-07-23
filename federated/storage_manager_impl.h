// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_STORAGE_MANAGER_IMPL_H_
#define FEDERATED_STORAGE_MANAGER_IMPL_H_

#include "federated/storage_manager.h"

#include <memory>
#include <string>

#include <base/sequence_checker.h>

#include "federated/example_database.h"
#include "federated/session_manager_observer_interface.h"
#include "federated/session_manager_proxy.h"

namespace federated {

class SessionManagerProxy;

class StorageManagerImpl : public StorageManager,
                           public SessionManagerObserverInterface {
 public:
  StorageManagerImpl() = default;
  StorageManagerImpl(const StorageManagerImpl&) = delete;
  StorageManagerImpl& operator=(const StorageManagerImpl&) = delete;

  ~StorageManagerImpl() override = default;

  // StorageManager:
  void InitializeSessionManagerProxy(dbus::Bus* bus) override;
  bool OnExampleReceived(const std::string& client_name,
                         const std::string& serialized_example) override;
  base::Optional<ExampleDatabase::Iterator> GetExampleIterator(
      const std::string& client_name) const override;

  // SessionManagerObserverInterface:
  void OnSessionStarted() override;
  void OnSessionStopped() override;

 private:
  friend class StorageManagerImplTest;

  void set_example_database_for_testing(ExampleDatabase* example_database) {
    example_database_.reset(example_database);
  }

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

#endif  // FEDERATED_STORAGE_MANAGER_IMPL_H_
