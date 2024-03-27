// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOJOM_SHILL_MOJO_SERVICE_MANAGER_H_
#define SHILL_MOJOM_SHILL_MOJO_SERVICE_MANAGER_H_

#include <memory>

#include "shill/manager.h"

namespace shill {

// This class setups the Mojo enrironment, and exposes the Shill Mojo services
// to the chromeos::MojoServiceManager. It's designed in RAII style. The
// Mojo-related resources are released when the instance is destroyed.
class ShillMojoServiceManager {
 public:
  // Creates a ShillMojoServiceManager instance. The |manager| should be started
  // before creating the ShillMojoServiceManager instance, and should be stopped
  // after the ShillMojoServiceManager instance destroyed.
  static std::unique_ptr<ShillMojoServiceManager> Create(Manager* manager);

  virtual ~ShillMojoServiceManager() = default;
};

// The factory class of ShillMojoServiceManager, used for injecting a mock
// instance at testing.
class ShillMojoServiceManagerFactory {
 public:
  ShillMojoServiceManagerFactory() = default;
  virtual ~ShillMojoServiceManagerFactory() = default;

  virtual std::unique_ptr<ShillMojoServiceManager> Create(Manager* manager) {
    return ShillMojoServiceManager::Create(manager);
  }
};

}  // namespace shill
#endif  // SHILL_MOJOM_SHILL_MOJO_SERVICE_MANAGER_H_
