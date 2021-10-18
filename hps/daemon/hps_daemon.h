// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HPS_DAEMON_HPS_DAEMON_H_
#define HPS_DAEMON_HPS_DAEMON_H_

#include <memory>
#include <string>

#include <brillo/daemons/dbus_daemon.h>
#include <hps/daemon/dbus_adaptor.h>
#include <hps/hps.h>

namespace hps {

class HpsDaemon : public brillo::DBusServiceDaemon {
 public:
  explicit HpsDaemon(std::unique_ptr<HPS>, uint32_t poll_time_ms);

  HpsDaemon(const HpsDaemon&) = delete;
  HpsDaemon& operator=(const HpsDaemon&) = delete;
  ~HpsDaemon() override;

 private:
  friend class HpsDaemonTest;

  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

  std::unique_ptr<DBusAdaptor> adaptor_;
  std::unique_ptr<HPS> hps_;
  const uint32_t poll_time_ms_;
};

}  // namespace hps

#endif  // HPS_DAEMON_HPS_DAEMON_H_
