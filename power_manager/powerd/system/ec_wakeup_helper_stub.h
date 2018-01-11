// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_EC_WAKEUP_HELPER_STUB_H_
#define POWER_MANAGER_POWERD_SYSTEM_EC_WAKEUP_HELPER_STUB_H_

#include <map>
#include <string>

#include <base/macros.h>

#include "power_manager/powerd/system/ec_wakeup_helper_interface.h"

namespace power_manager {
namespace system {

class EcWakeupHelperStub : public EcWakeupHelperInterface {
 public:
  EcWakeupHelperStub();
  ~EcWakeupHelperStub() override;

  // Implementation of EcWakeupHelperInterface.
  bool IsSupported() override;
  bool AllowWakeupAsTablet(bool enabled) override;

  bool IsWakeupAsTabletAllowed();

 private:
  bool wakeup_as_tablet_allowed_;

  DISALLOW_COPY_AND_ASSIGN(EcWakeupHelperStub);
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_EC_WAKEUP_HELPER_STUB_H_
