// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fake/fake_service_manager.h"

#include <optional>
#include <string>
#include <utility>

#include <base/notimplemented.h>
#include <base/time/time.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/system/message_pipe.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

namespace diagnostics {

FakeServiceManager::FakeServiceManager() : receiver_(this) {}

FakeServiceManager::~FakeServiceManager() = default;

void FakeServiceManager::Register(
    const std::string& service_name,
    mojo::PendingRemote<chromeos::mojo_service_manager::mojom::ServiceProvider>
        service_provider) {
  NOTIMPLEMENTED();
}

void FakeServiceManager::Request(const std::string& service_name,
                                 std::optional<base::TimeDelta> timeout,
                                 mojo::ScopedMessagePipeHandle receiver) {
  NOTIMPLEMENTED();
}

void FakeServiceManager::Query(const std::string& service_name,
                               QueryCallback callback) {
  const auto& it = query_result_.find(service_name);
  if (it == query_result_.end()) {
    NOTIMPLEMENTED();
  } else {
    std::move(callback).Run(it->second->Clone());
  }
}

void FakeServiceManager::AddServiceObserver(
    mojo::PendingRemote<chromeos::mojo_service_manager::mojom::ServiceObserver>
        observer) {
  NOTIMPLEMENTED();
}

void FakeServiceManager::SetQuery(
    const std::string& service_name,
    chromeos::mojo_service_manager::mojom::ErrorOrServiceStatePtr
        error_or_service_state) {
  query_result_[service_name] = std::move(error_or_service_state);
}

}  // namespace diagnostics
