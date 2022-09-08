// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/mojo_service_impl.h"

#include <memory>

namespace diagnostics {

MojoServiceImpl::MojoServiceImpl() = default;

MojoServiceImpl::~MojoServiceImpl() = default;

// static
std::unique_ptr<MojoServiceImpl> MojoServiceImpl::Create() {
  auto impl = std::unique_ptr<MojoServiceImpl>(new MojoServiceImpl());
  impl->service_manager_.Bind(
      impl->fake_service_manager_.receiver().BindNewPipeAndPassRemote());

  impl->chromium_data_collector_relay_.InitNewPipeAndWaitForIncomingRemote();
  return impl;
}

chromeos::mojo_service_manager::mojom::ServiceManager*
MojoServiceImpl::GetServiceManager() {
  DCHECK(service_manager_.is_bound());
  return service_manager_.get();
}

chromeos::cros_healthd::internal::mojom::ChromiumDataCollector*
MojoServiceImpl::GetChromiumDataCollector() {
  return chromium_data_collector_relay_.Get();
}

}  // namespace diagnostics
