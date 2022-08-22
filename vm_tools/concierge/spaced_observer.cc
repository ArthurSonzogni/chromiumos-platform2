// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/spaced_observer.h"

#include <utility>

namespace vm_tools {
namespace concierge {

SpacedObserver::SpacedObserver(
    const base::RepeatingCallback<void(const spaced::StatefulDiskSpaceUpdate&)>
        handle_update_cb,
    scoped_refptr<dbus::Bus> bus)
    : handle_update_cb_(std::move(handle_update_cb)) {
  disk_usage_proxy_ = std::make_unique<spaced::DiskUsageProxy>(bus);
  disk_usage_proxy_->AddObserver(this);
  disk_usage_proxy_->StartMonitoring();
}

// spaced::SpacedObserverInterface override.
void SpacedObserver::OnStatefulDiskSpaceUpdate(
    const spaced::StatefulDiskSpaceUpdate& update) {
  handle_update_cb_.Run(update);
}

}  // namespace concierge
}  // namespace vm_tools
