// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WIMAX_MANAGER_DBUS_ADAPTABLE_H_
#define WIMAX_MANAGER_DBUS_ADAPTABLE_H_

#include <memory>

#include <base/macros.h>
#include <dbus-c++/dbus.h>

#include "wimax_manager/dbus_control.h"

namespace wimax_manager {

template <typename Adaptee, typename Adaptor>
class DBusAdaptable {
 public:
  DBusAdaptable() = default;
  ~DBusAdaptable() = default;

  void CreateDBusAdaptor() {
    if (dbus_adaptor_.get())
      return;

    dbus_adaptor_.reset(
        new Adaptor(DBusControl::GetConnection(), static_cast<Adaptee*>(this)));
  }

  Adaptor *dbus_adaptor() const { return dbus_adaptor_.get(); }

  DBus::Path dbus_object_path() const {
    return dbus_adaptor_.get() ? dbus_adaptor_->path() : DBus::Path();
  }

 private:
  std::unique_ptr<Adaptor> dbus_adaptor_;

  DISALLOW_COPY_AND_ASSIGN(DBusAdaptable);
};

}  // namespace wimax_manager

#endif  // WIMAX_MANAGER_DBUS_ADAPTABLE_H_
