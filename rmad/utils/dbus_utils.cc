// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/dbus_utils.h"

#include <base/memory/scoped_refptr.h>
#include <base/no_destructor.h>
#include <base/task/sequenced_task_runner.h>
#include <dbus/bus.h>

namespace rmad {

// static
DBus* DBus::GetInstance() {
  // This is thread-safe.
  static base::NoDestructor<DBus> instance;
  return instance.get();
}

const scoped_refptr<dbus::Bus>& DBus::bus() {
  CHECK(bus_);
  return bus_;
}

DBus::DBus() {
  CHECK(base::SequencedTaskRunner::HasCurrentDefault());
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  bus_ = base::MakeRefCounted<dbus::Bus>(options);
}

}  // namespace rmad
