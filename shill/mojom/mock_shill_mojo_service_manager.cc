// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mojom/mock_shill_mojo_service_manager.h"

#include <utility>

#include <base/functional/callback.h>

namespace shill {

MockShillMojoServiceManager::MockShillMojoServiceManager(
    base::OnceClosure destroy_callback)
    : destroy_callback_(std::move(destroy_callback)) {}

MockShillMojoServiceManager::~MockShillMojoServiceManager() {
  std::move(destroy_callback_).Run();
}

MockShillMojoServiceManagerFactory::MockShillMojoServiceManagerFactory() =
    default;
MockShillMojoServiceManagerFactory::~MockShillMojoServiceManagerFactory() =
    default;

}  // namespace shill
