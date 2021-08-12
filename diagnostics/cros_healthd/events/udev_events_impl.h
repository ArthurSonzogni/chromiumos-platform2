// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EVENTS_UDEV_EVENTS_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_EVENTS_UDEV_EVENTS_IMPL_H_

#include <memory>

#include <base/files/file_descriptor_watcher_posix.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/remote_set.h>

#include "diagnostics/cros_healthd/events/udev_events.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "mojo/cros_healthd_events.mojom.h"

namespace diagnostics {

class UdevEventsImpl final : public UdevEvents {
 public:
  explicit UdevEventsImpl(Context* context);
  UdevEventsImpl(const UdevEventsImpl&) = delete;
  UdevEventsImpl& operator=(const UdevEventsImpl&) = delete;
  ~UdevEventsImpl() override = default;

 private:
  void OnUdevEvent();

  // Unowned pointer. Should outlive this instance.
  Context* const context_ = nullptr;

  std::unique_ptr<base::FileDescriptorWatcher::Controller>
      udev_monitor_watcher_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EVENTS_UDEV_EVENTS_IMPL_H_
