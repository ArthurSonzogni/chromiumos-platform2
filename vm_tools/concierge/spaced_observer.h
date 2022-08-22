// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_SPACED_OBSERVER_H_
#define VM_TOOLS_CONCIERGE_SPACED_OBSERVER_H_

#include <memory>

#include <base/callback.h>
#include <base/memory/ref_counted.h>
#include <dbus/bus.h>
#include <spaced/disk_usage_proxy.h>

namespace vm_tools {
namespace concierge {

// Observer for listening to spaced.
class SpacedObserver : public spaced::SpacedObserverInterface {
 public:
  SpacedObserver(const base::RepeatingCallback<void(
                     const spaced::StatefulDiskSpaceUpdate&)> handle_update_cb,
                 scoped_refptr<dbus::Bus> bus);

  // spaced::SpacedObserverInterface override.
  void OnStatefulDiskSpaceUpdate(
      const spaced::StatefulDiskSpaceUpdate& update) override;

 private:
  // Proxy for interacting with spaced.
  std::unique_ptr<spaced::DiskUsageProxy> disk_usage_proxy_;
  const base::RepeatingCallback<void(const spaced::StatefulDiskSpaceUpdate&)>
      handle_update_cb_;
};

}  // namespace concierge
}  // namespace vm_tools

#endif  // VM_TOOLS_CONCIERGE_SPACED_OBSERVER_H_
