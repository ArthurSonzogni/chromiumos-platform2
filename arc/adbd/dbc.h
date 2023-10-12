// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_ADBD_DBC_H_
#define ARC_ADBD_DBC_H_

#include <memory>

#include <base/files/file_path_watcher.h>
#include <base/files/scoped_file.h>
#include <brillo/daemons/daemon.h>

#include "arc/adbd/adbd.h"
#include "arc/adbd/arcvm_sock_to_usb.h"
#include "arc/adbd/arcvm_usb_to_sock.h"
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
  // Track ARCVM ADB bridge status.
  bool dbc_bridge_started_;
  // Udev monitor for usb hotplug events.
  std::unique_ptr<UdevMonitor> udev_monitor_;
  // File watcher for dbc device node.
  std::unique_ptr<base::FilePathWatcher> file_watcher_;
  // Usb To Sock thread.
  std::unique_ptr<ArcVmUsbToSock> ch_in_;
  // Sock to Usb thread.
  std::unique_ptr<ArcVmSockToUsb> ch_out_;
  // Vsock socket FD.
  base::ScopedFD vsock_sock_;
  // USB FD.
  base::ScopedFD dbc_bulk_usb_fd_;
  // Eventfd to stop the threads.
  base::ScopedFD stop_fd_;

  // Start ArcVM ADB bridge for dbc.
  void StartArcVmAdbBridgeDbc();
  // Stop ArcVM ADB bridge for dbc.
  void StopArcVmAdbBridgeDbc();
  // Callback on dbc device node changes.
  void OnDbcDevChange(const base::FilePath& path, bool error);
};

}  // namespace adbd
#endif  // ARC_ADBD_DBC_H_
