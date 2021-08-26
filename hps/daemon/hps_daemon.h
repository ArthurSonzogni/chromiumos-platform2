// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HPS_DAEMON_HPS_DAEMON_H_
#define HPS_DAEMON_HPS_DAEMON_H_

#include <memory>
#include <string>

#include <base/macros.h>
#include <base/threading/thread.h>
#include <brillo/daemons/dbus_daemon.h>

#include "hps/hps.h"

#include "dbus_adaptors/org.chromium.Hps.h"

namespace hps {

class HpsDaemon : public brillo::DBusServiceDaemon,
                  public org::chromium::HpsAdaptor,
                  public org::chromium::HpsInterface {
 public:
  explicit HpsDaemon(std::unique_ptr<HPS>);
  // Used to inject a mock bus.
  HpsDaemon(scoped_refptr<dbus::Bus> bus, std::unique_ptr<HPS> hps);

  HpsDaemon(const HpsDaemon&) = delete;
  HpsDaemon& operator=(const HpsDaemon&) = delete;
  virtual ~HpsDaemon();

 private:
  friend class HpsDaemonTest;

  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

  // Methods for HpsInterface
  bool EnableFeature(brillo::ErrorPtr* error, uint8_t feature) override;

  bool DisableFeature(brillo::ErrorPtr* error, uint8_t feature) override;

  bool GetFeatureResult(brillo::ErrorPtr* error,
                        uint8_t feature,
                        uint16_t* result) override;

  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;
  std::unique_ptr<HPS> hps_;
};

}  // namespace hps

#endif  // HPS_DAEMON_HPS_DAEMON_H_
