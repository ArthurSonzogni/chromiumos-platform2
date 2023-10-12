// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/adbd/dbc.h"

#include <fcntl.h>
#include <sys/eventfd.h>
#include <sysexits.h>
#include <termios.h>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>

#include "arc/adbd/adbd.h"

namespace adbd {
Dbc::Dbc(uint32_t cid) : cid_(cid), dbc_bridge_started_(false) {}

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
  }

  VLOG(1) << "dbc init successful";
  return 0;
}

// Callback on dbc device node change.
void Dbc::OnDbcDevChange(const base::FilePath& dbc_path, bool error) {
  if (base::PathExists(dbc_path)) {
    // When connecting using a USB-C to USB-A cable, the PD negotiation
    // attempts fail triggering multiple hard resets. As a workaround,
    // sleep for a few secs to allow usb enumeration settle down.
    // TODO(ssradjacoumar) Remove workaround after (b/308471879) is fixed.
    base::PlatformThread::Sleep(base::Seconds(4));
    if (base::PathExists(dbc_path) && !dbc_bridge_started_) {
      VLOG(1) << "dbc device " << dbc_path.value().c_str()
              << " exists on file watcher event, starting arcvm adb bridge";
      StartArcVmAdbBridgeDbc();
    }
  } else {
    if (dbc_bridge_started_) {
      VLOG(1)
          << "dbc device " << dbc_path.value().c_str()
          << " does not exist on file watcher event, stopping arcvm adb bridge";
      StopArcVmAdbBridgeDbc();
    }
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

  vsock_sock_ = InitializeVSockConnection(cid_);
  while (!vsock_sock_.is_valid()) {
    if (--retries < 0) {
      LOG(ERROR) << "Too many retries to initialize dbc vsock; giving up";
      _exit(EXIT_FAILURE);
    }
    // This path may be taken when guest's adbd hasn't started listening to the
    // socket yet. To work around the case, retry connecting to the socket after
    // a short sleep.
    // TODO(crbug.com/1126289): Remove the retry hack.
    base::PlatformThread::Sleep(kConnectInterval);
    vsock_sock_ = InitializeVSockConnection(cid_);
  }

  dbc_bulk_usb_fd_ =
      base::ScopedFD(HANDLE_EINTR(open(dbc_adb_path.value().c_str(), O_RDWR)));
  if (!dbc_bulk_usb_fd_.is_valid()) {
    PLOG(ERROR) << "Failed to open dbc adb path "
                << dbc_adb_path.value().c_str();
    _exit(EXIT_FAILURE);
  }

  // Configure serial port in raw mode - see termio(7I) for modes.
  tcgetattr(dbc_bulk_usb_fd_.get(), &SerialPortSettings);

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

  tcsetattr(dbc_bulk_usb_fd_.get(), TCSANOW, &SerialPortSettings);

  stop_fd_ = base::ScopedFD(eventfd(0, 0));
  if (!stop_fd_.is_valid()) {
    PLOG(ERROR) << "Unable to create eventfd";
    _exit(EXIT_FAILURE);
  }

  auto sock_fd = vsock_sock_.get();
  ch_in_ = std::make_unique<ArcVmUsbToSock>(sock_fd, dbc_bulk_usb_fd_.get(),
                                            stop_fd_.get());
  if (!ch_in_->Start()) {
    LOG(ERROR) << "dbc vsock IN Channel failed to start";
    _exit(EXIT_FAILURE);
  }

  ch_out_ = std::make_unique<ArcVmSockToUsb>(sock_fd, dbc_bulk_usb_fd_.get(),
                                             stop_fd_.get());
  if (!ch_out_->Start()) {
    LOG(ERROR) << "dbc vsock OUT Channel failed to start";
    _exit(EXIT_FAILURE);
  }

  // Update the bridge status.
  dbc_bridge_started_ = true;

  LOG(WARNING) << "arcvm adb bridge for dbc started";
}

// Tear down ARCVM ADB bridge.
void Dbc::StopArcVmAdbBridgeDbc() {
  uint64_t counter = 1; /* Any non-zero counter value */

  // Send stop event to threads.
  if (!base::WriteFileDescriptor(
          stop_fd_.get(),
          std::string_view((const char*)&counter, sizeof(counter)))) {
    PLOG(ERROR) << "Unable to write to stop_fd; failed to stop threads";
  }

  // Wait for threads to complete.
  ch_in_->Stop();
  ch_out_->Stop();

  // Release memory.
  ch_in_.reset();
  ch_out_.reset();
  stop_fd_.reset();
  vsock_sock_.reset();
  dbc_bulk_usb_fd_.reset();

  // Update the ADB bridge status.
  dbc_bridge_started_ = false;

  LOG(WARNING) << "arcvm adb bridge for dbc stopped";
}

}  // namespace adbd
