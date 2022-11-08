// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOJOM_MOJO_PASSPOINT_SERVICE_H_
#define SHILL_MOJOM_MOJO_PASSPOINT_SERVICE_H_

#include <string>

#include "mojom/passpoint.mojom.h"

namespace shill {

class Manager;

class MojoPasspointService
    : public chromeos::connectivity::mojom::PasspointService {
 public:
  explicit MojoPasspointService(Manager* manager);
  MojoPasspointService(const MojoPasspointService&) = delete;
  MojoPasspointService& operator=(const MojoPasspointService&) = delete;

  ~MojoPasspointService() override;

  void GetPasspointSubscription(
      const std::string& id,
      GetPasspointSubscriptionCallback callback) override;

 private:
  Manager* manager_;
};

}  // namespace shill

#endif  // SHILL_MOJOM_MOJO_PASSPOINT_SERVICE_H_
