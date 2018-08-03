// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_SAR_WATCHER_STUB_H_
#define POWER_MANAGER_POWERD_SYSTEM_SAR_WATCHER_STUB_H_

#include <base/macros.h>
#include <base/observer_list.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/system/sar_watcher_interface.h"

namespace power_manager {
namespace system {

// Stub implementation of SarWatcherInterface for use by tests.
class SarWatcherStub : public SarWatcherInterface {
 public:
  SarWatcherStub() = default;
  ~SarWatcherStub() override = default;

  // SarWatcherInterface overrides:
  void AddObserver(SarObserver* observer) override;
  void RemoveObserver(SarObserver* observer) override;

  void AddSensor(int id, uint32_t role);
  void SendEvent(int id, UserProximity proximity);

 private:
  base::ObserverList<SarObserver> observers_;

  DISALLOW_COPY_AND_ASSIGN(SarWatcherStub);
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_SAR_WATCHER_STUB_H_
