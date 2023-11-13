// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGMON_DAEMON_REGMON_DAEMON_H_
#define REGMON_DAEMON_REGMON_DAEMON_H_

#include <memory>

#include <brillo/daemons/dbus_daemon.h>

#include "regmon/dbus/dbus_adaptor.h"
#include "regmon/regmon/regmon_impl.h"
#include "regmon/regmon/regmon_service.h"

namespace regmon {

class RegmonDaemon : public brillo::DBusServiceDaemon {
 public:
  explicit RegmonDaemon(
      std::unique_ptr<RegmonService> regmon = std::make_unique<RegmonImpl>());
  RegmonDaemon(const RegmonDaemon&) = delete;
  RegmonDaemon& operator=(const RegmonDaemon&) = delete;
  virtual ~RegmonDaemon();

 private:
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

  std::unique_ptr<DBusAdaptor> adaptor_;
  std::unique_ptr<RegmonService> regmon_;
};
}  // namespace regmon

#endif  // REGMON_DAEMON_REGMON_DAEMON_H_
