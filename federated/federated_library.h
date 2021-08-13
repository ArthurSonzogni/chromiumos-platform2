// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_FEDERATED_LIBRARY_H_
#define FEDERATED_FEDERATED_LIBRARY_H_

#include <string>

#include <absl/status/status.h>
#include <base/optional.h>
#include <base/scoped_native_library.h>
#include <fcp/fcp.h>

#include "federated/federated_session.h"

namespace federated {

class DeviceStatusMonitor;

// A singleton proxy class for the federated DSO.
class FederatedLibrary {
 public:
  static FederatedLibrary* GetInstance();

  // Load federated library and look up the functions.
  FederatedLibrary();
  FederatedLibrary(const FederatedLibrary&) = delete;
  FederatedLibrary& operator=(const FederatedLibrary&) = delete;

  virtual ~FederatedLibrary();

  absl::Status GetStatus() const;

  FederatedSession CreateSession(
      const std::string& service_uri,
      const std::string& api_key,
      const ClientConfigMetadata client_config,
      DeviceStatusMonitor* const device_status_monitor);

 private:
  base::Optional<base::ScopedNativeLibrary> library_;
  absl::Status status_;

  FlRunPlanFn run_plan_;
  FlFreeRunPlanResultFn free_run_plan_result_;
};

}  // namespace federated

#endif  // FEDERATED_FEDERATED_LIBRARY_H_
