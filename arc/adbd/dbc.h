// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_ADBD_DBC_H_
#define ARC_ADBD_DBC_H_

#include <memory>

#include <base/files/file_path_watcher.h>
#include <brillo/daemons/daemon.h>

#include "arc/adbd/adbd.h"
#include "arc/adbd/udev_monitor.h"

namespace adbd {

// The path to ttyDBC interface.
// See:
// https://www.kernel.org/doc/html/v5.4/driver-api/usb/usb3-debug-port.html.
constexpr char kDbcAdbPath[] = "/dev/dbc/ttyDBC0";

// Dbc daemon provides monitoring of dbc devices and handles the
// connection to ArcVM for ADB.
class Dbc : public brillo::Daemon {
 public:
  explicit Dbc(uint32_t cid);
  ~Dbc() = default;

 protected:
  int OnInit() override;

 private:
  // ArcVM CID required to create vsock.
  const uint32_t cid_;
  // Udev monitor for usb hotplug events.
  std::unique_ptr<UdevMonitor> udev_monitor_;
  // File watcher for dbc device node.
  std::unique_ptr<base::FilePathWatcher> file_watcher_;

  // Start ArcVM ADB bridge for dbc.
  void StartArcVmAdbBridgeDbc();
  // Callback on dbc device node changes.
  void OnDbcDevChange(const base::FilePath& path, bool error);
};

}  // namespace adbd
#endif  // ARC_ADBD_DBC_H_
