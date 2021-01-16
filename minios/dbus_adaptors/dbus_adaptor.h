// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_DBUS_ADAPTORS_DBUS_ADAPTOR_H_
#define MINIOS_DBUS_ADAPTORS_DBUS_ADAPTOR_H_

#include <memory>

#include <minios/proto_bindings/minios.pb.h>

#include "minios/dbus_adaptors/org.chromium.MiniOsInterface.h"
#include "minios/minios_interface.h"

namespace minios {

class DBusService : public org::chromium::MiniOsInterfaceInterface {
 public:
  explicit DBusService(std::shared_ptr<MiniOsInterface> mini_os);
  ~DBusService() = default;

  DBusService(const DBusService&) = delete;
  DBusService& operator=(const DBusService&) = delete;

  bool GetState(brillo::ErrorPtr* error, State* state_out) override;

 private:
  std::shared_ptr<MiniOsInterface> mini_os_;
};

class DBusAdaptor : public org::chromium::MiniOsInterfaceAdaptor {
 public:
  explicit DBusAdaptor(std::unique_ptr<DBusService> dbus_service);
  ~DBusAdaptor() = default;

  DBusAdaptor(const DBusAdaptor&) = delete;
  DBusAdaptor& operator=(const DBusAdaptor&) = delete;

 private:
  std::unique_ptr<DBusService> dbus_service_;
};

}  // namespace minios

#endif  // MINIOS_DBUS_ADAPTORS_DBUS_ADAPTOR_H_
