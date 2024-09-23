// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sysexits.h>
#include <termios.h>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>

#include "arc/adbd/adbd.h"
#include "arc/adbd/arcvm_sock_to_usb.h"
#include "arc/adbd/arcvm_usb_to_sock.h"
#include "arc/adbd/dbc.h"

namespace adbd {
Dbc::Dbc(uint32_t cid) : cid_(cid) {}

int Dbc::OnInit() {
  int exit_code = brillo::Daemon::OnInit();
  if (exit_code != EX_OK) {
    LOG(ERROR) << "dbc daemon init failed";
    return exit_code;
  }

  // Start udev monitor for usb hotplug events.
  udev_monitor_ = std::make_unique<UdevMonitor>();
  if (!udev_monitor_->Init()) {
    LOG(ERROR) << "dbc init failed initializing udev monitor";
    return -1;
  }

  // Add a file watcher for dbc device node.
  file_watcher_ = std::make_unique<base::FilePathWatcher>();
  // Unretained(this) is safe since file_watcher_ does not outlive |this|.
  auto cb = base::BindRepeating(&Dbc::OnDbcDevChange, base::Unretained(this));
  if (!file_watcher_->Watch(base::FilePath(kDbcAdbPath),
                            base::FilePathWatcher::Type::kNonRecursive, cb)) {
    LOG(ERROR) << "Failed to start file watcher for dbc";
    return -1;
  }

  // Start ArcVM ADB bridge if dbc device exists.
  if (base::PathExists(base::FilePath(kDbcAdbPath))) {
    VLOG(1) << "dbc device " << kDbcAdbPath
            << " exists, starting arcvm adb bridge.";
    StartArcVmAdbBridgeDbc();
    LOG(FATAL) << "ArcVM ADB bridge stopped unexpectedly.";
  }

  VLOG(1) << "dbc init successful";
  return 0;
}

// Callback on dbc device node change.
void Dbc::OnDbcDevChange(const base::FilePath& dbc_path, bool error) {
  // When connecting using a USB-C to USB-A cable, the PD negotiation
  // attempts fail triggering multiple hard resets. As a workaround,
  // sleep for a few secs to allow usb enumeration settle down.
  // TODO(ssradjacoumar) Remove workaround after (b/308471879) is fixed.
  base::PlatformThread::Sleep(base::Seconds(4));

  if (base::PathExists(dbc_path)) {
    VLOG(1) << "dbc device " << dbc_path.value().c_str()
            << " exists on file watcher event, starting arcvm adb bridge";
    StartArcVmAdbBridgeDbc();
    LOG(FATAL) << "ArcVM ADB bridge stopped unexpectedly.";
  }
}

// Start ArcVM ADB bridge for dbc.
void Dbc::StartArcVmAdbBridgeDbc() {
  constexpr base::TimeDelta kConnectInterval = base::Seconds(15);
  constexpr int kMaxRetries = 4;
  int retries = kMaxRetries;
  struct termios SerialPortSettings;

  const base::FilePath dbc_adb_path(kDbcAdbPath);
  if (!base::PathExists(dbc_adb_path)) {
    LOG(WARNING) << "dbc device does not exist "
                 << dbc_adb_path.value().c_str();
    return;
  }

  auto vsock_sock = InitializeVSockConnection(cid_);
  while (!vsock_sock.is_valid()) {
    if (--retries < 0) {
      LOG(ERROR) << "Too many retries to initialize dbc vsock; giving up";
      _exit(EXIT_FAILURE);
    }
    // This path may be taken when guest's adbd hasn't started listening to the
    // socket yet. To work around the case, retry connecting to the socket after
    // a short sleep.
    // TODO(crbug.com/1126289): Remove the retry hack.
    base::PlatformThread::Sleep(kConnectInterval);
    vsock_sock = InitializeVSockConnection(cid_);
  }

  base::ScopedFD dbc_bulk_usb_fd(
      HANDLE_EINTR(open(dbc_adb_path.value().c_str(), O_RDWR)));
  if (!dbc_bulk_usb_fd.is_valid()) {
    PLOG(ERROR) << "Failed to open dbc adb path "
                << dbc_adb_path.value().c_str();
    _exit(EXIT_FAILURE);
  }

  // Configure serial port in raw mode - see termio(7I) for modes.
  tcgetattr(dbc_bulk_usb_fd.get(), &SerialPortSettings);

  cfsetispeed(&SerialPortSettings, B9600);
  cfsetospeed(&SerialPortSettings, B9600);

  SerialPortSettings.c_cflag &= ~PARENB;
  SerialPortSettings.c_cflag &= ~CSTOPB;
  SerialPortSettings.c_cflag &= ~CSIZE;
  SerialPortSettings.c_cflag &= CS8;
  SerialPortSettings.c_cflag &= ~CRTSCTS;
  SerialPortSettings.c_cflag &= CREAD | CLOCAL;
  SerialPortSettings.c_lflag &= ~(ICANON | ECHO | IEXTEN | ISIG);
  SerialPortSettings.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  SerialPortSettings.c_oflag &= ~OPOST;
  SerialPortSettings.c_cc[VMIN] = 10;
  SerialPortSettings.c_cc[VTIME] = 10;

  tcsetattr(dbc_bulk_usb_fd.get(), TCSANOW, &SerialPortSettings);

  auto sock_fd = vsock_sock.get();
  std::unique_ptr<ArcVmUsbToSock> ch_in =
      std::make_unique<ArcVmUsbToSock>(sock_fd, dbc_bulk_usb_fd.get());
  if (!ch_in->Start()) {
    LOG(ERROR) << "dbc vsock IN Channel failed to start";
    _exit(EXIT_FAILURE);
  }

  std::unique_ptr<ArcVmSockToUsb> ch_out =
      std::make_unique<ArcVmSockToUsb>(sock_fd, dbc_bulk_usb_fd.get());
  if (!ch_out->Start()) {
    LOG(ERROR) << "dbc vsock OUT Channel failed to start";
    _exit(EXIT_FAILURE);
  }

  LOG(WARNING) << "arcvm adb bridge for dbc started";
  // The function will not return here because the execution is waiting
  // for threads to join but that won't happen in normal cases.
}

}  // namespace adbd
