// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RGBKBD_DBUS_SERVICE_H_
#define RGBKBD_DBUS_SERVICE_H_

#include <memory>

#include <brillo/daemons/dbus_daemon.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/dbus/dbus_object.h>
#include <dbus/bus.h>

namespace rgbkbd {

class DBusService : public brillo::DBusServiceDaemon {
 public:
  DBusService();
  DBusService(const DBusService&) = delete;
  DBusService& operator=(const DBusService&) = delete;

  ~DBusService() override;

 protected:
  // brillo::DBusServiceDaemon overrides.
  int OnInit() override;
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

 private:
  friend class DBusServiceTest;

  template <typename ResponseType, typename ReplyProtobufType>
  void ReplyAndQuit(std::shared_ptr<ResponseType> response,
                    const ReplyProtobufType& reply);
  // Schedule an asynchronous D-Bus shutdown and exit the daemon.
  void PostQuitTask();

  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;
};

}  // namespace rgbkbd

#endif  // RGBKBD_DBUS_SERVICE_H_
