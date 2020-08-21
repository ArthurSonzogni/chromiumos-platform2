// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_STORAGE_MANAGER_H_
#define FEDERATED_STORAGE_MANAGER_H_

#include <string>

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

  // Provides example streaming. We assume there're no parallel streamings.
  // Usage:
  // 1. call PrepareStreamingForClient(), if it returns true, then;
  // 2. call GetNextExample() to get examples, until end_of_iterator = true or
  //    it returns false, then;
  // 3. call CloseStreaming() to close the current streaming and clean the used
  //    examples if clean_examples = true. clean_examples is set true only when
  //    the training job succeeds, otherwise the examples should be kept for
  //    future training.
  virtual bool PrepareStreamingForClient(const std::string& client_name) = 0;
  // Returns true when call to database.GetNextStreamedRecord succeeds.
  virtual bool GetNextExample(std::string* example, bool* end_of_iterator) = 0;
  // Returns true when database closes streaming successfully and deletes used
  // examples if clean_examples = true.
  virtual bool CloseStreaming(bool clean_examples) = 0;

 protected:
  StorageManager() = default;
  virtual ~StorageManager() = default;
};

}  // namespace federated

#endif  // FEDERATED_STORAGE_MANAGER_H_
