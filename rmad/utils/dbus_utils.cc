// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/dbus_utils.h"

#include <base/memory/scoped_refptr.h>
#include <dbus/bus.h>

namespace rmad {

scoped_refptr<dbus::Bus> GetSystemBus() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  return base::MakeRefCounted<dbus::Bus>(options);
}

}  // namespace rmad
