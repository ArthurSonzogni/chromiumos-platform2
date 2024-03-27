// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOJOM_MOCK_SHILL_MOJO_SERVICE_MANAGER_H_
#define SHILL_MOJOM_MOCK_SHILL_MOJO_SERVICE_MANAGER_H_

#include "shill/mojom/shill_mojo_service_manager.h"

#include <memory>

#include <base/functional/callback_forward.h>
#include <gmock/gmock.h>

namespace shill {

class MockShillMojoServiceManager : public ShillMojoServiceManager {
 public:
  explicit MockShillMojoServiceManager(base::OnceClosure destroy_callback);
  ~MockShillMojoServiceManager() override;

 private:
  base::OnceClosure destroy_callback_;
};

class MockShillMojoServiceManagerFactory
    : public ShillMojoServiceManagerFactory {
 public:
  MockShillMojoServiceManagerFactory();
  ~MockShillMojoServiceManagerFactory() override;

  MOCK_METHOD(std::unique_ptr<ShillMojoServiceManager>,
              Create,
              (Manager*),
              (override));
};

}  // namespace shill
#endif  // SHILL_MOJOM_MOCK_SHILL_MOJO_SERVICE_MANAGER_H_
