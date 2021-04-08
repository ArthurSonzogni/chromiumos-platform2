// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_DAEMON_H_
#define TYPECD_DAEMON_H_

#include <memory>

#include <base/memory/weak_ptr.h>
#include <brillo/daemons/dbus_daemon.h>

#include "typecd/chrome_features_service_client.h"
#include "typecd/cros_ec_util.h"
#include "typecd/metrics.h"
#include "typecd/notification_manager.h"
#include "typecd/port_manager.h"
#include "typecd/session_manager_proxy.h"
#include "typecd/udev_monitor.h"

namespace typecd {

class Daemon : public brillo::DBusServiceDaemon {
 public:
  Daemon();
  Daemon(const Daemon&) = delete;
  Daemon& operator=(const Daemon&) = delete;

  ~Daemon() override;

 protected:
  int OnInit() override;
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

 private:
  // Set the initial UserActive state for the |port_manager_| using
  // state retrieved from the |session_manager_proxy_|.
  void InitUserActiveState();

  std::unique_ptr<UdevMonitor> udev_monitor_;
  std::unique_ptr<PortManager> port_manager_;
  std::unique_ptr<SessionManagerProxy> session_manager_proxy_;
  std::unique_ptr<CrosECUtil> cros_ec_util_;
  std::unique_ptr<NotificationManager> notify_mgr_;
  std::unique_ptr<ChromeFeaturesServiceClient> features_client_;
  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;
  Metrics metrics_;
  base::WeakPtrFactory<Daemon> weak_factory_;
};

}  // namespace typecd

#endif  // TYPECD_DAEMON_H__
