// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_UTIL_SERVER_CONFIGURATION_CONTROLLER_H_
#define MISSIVE_UTIL_SERVER_CONFIGURATION_CONTROLLER_H_

#include <base/memory/ref_counted.h>

#include "missive/health/health_module.h"
#include "missive/proto/record.pb.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/util/dynamic_flag.h"

namespace reporting {

class ServerConfigurationController
    : public DynamicFlag,
      public base::RefCountedThreadSafe<ServerConfigurationController> {
 public:
  // Configuration file record blocked UMA name.
  static constexpr char kConfigFileRecordBlocked[] =
      "Platform.Missive.ConfigFileRecordBlocked";
  // Not copyable or movable.
  ServerConfigurationController(const ServerConfigurationController& other) =
      delete;
  ServerConfigurationController& operator=(
      const ServerConfigurationController& other) = delete;

  // Factory method creates |ServerConfigurationController| object.
  static scoped_refptr<ServerConfigurationController> Create(bool is_enabled);

  class BlockedDestinations {
   public:
    BlockedDestinations();
    ~BlockedDestinations() = default;
    BlockedDestinations(const BlockedDestinations&) = delete;
    BlockedDestinations& operator=(const BlockedDestinations&) = delete;

    // Atomically sets the array to 'false'.
    void ClearDestinations();

    // Retrieve the data.
    bool get(Destination destination) const;

    void blocked(Destination destination, bool state);

   private:
    // At construction all Destinations are set to 'false'.
    std::array<std::atomic<bool>, Destination_ARRAYSIZE> blocked_destinations_;
  };

  // This method updates the internal list of blocked destinations and produces
  // a new health record if the module is enabled. Declared virtual to use in
  // testing.
  virtual void UpdateConfiguration(ListOfBlockedDestinations list,
                                   HealthModule::Recorder recorder);

  // This method checks if the provided destination is currently blocked and
  // records an UMA metric if a record is blocked.
  bool IsDestinationBlocked(Destination destination);

 protected:
  friend base::RefCountedThreadSafe<ServerConfigurationController>;

  // Constructor can only be called by Create factory method.
  explicit ServerConfigurationController(bool is_enabled);

  // Refcounted object must have destructor declared protected or private.
  ~ServerConfigurationController() override;

 private:
  BlockedDestinations blocked_destinations_;
};

}  // namespace reporting

#endif  // MISSIVE_UTIL_SERVER_CONFIGURATION_CONTROLLER_H_
