// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wimax_manager/manager.h"

#include <fcntl.h>

#include <base/file_util.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/stl_util.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/text_format.h>

#include "wimax_manager/dbus_service_dbus_proxy.h"
#include "wimax_manager/device.h"
#include "wimax_manager/device_dbus_adaptor.h"
#include "wimax_manager/gdm_driver.h"
#include "wimax_manager/manager_dbus_adaptor.h"
#include "wimax_manager/proto_bindings/config.pb.h"

namespace wimax_manager {

namespace {

const int kMaxNumberOfDeviceScans = 15;
const int kDefaultDeviceScanIntervalInSeconds = 1;
const int kDeviceScanDelayAfterResumeInSeconds = 3;

gboolean OnDeviceScanNeeded(gpointer data) {
  CHECK(data);

  reinterpret_cast<Manager *>(data)->ScanDevices();
  // ScanDevices decides if a rescan is needed later, so return FALSE
  // to prevent this function from being called repeatedly.
  return FALSE;
}

}  // namespace

Manager::Manager()
    : num_device_scans_(0),
      device_scan_timeout_id_(0),
      dbus_service_(this) {
}

Manager::~Manager() {
  Finalize();
}

bool Manager::Initialize() {
  if (driver_.get())
    return true;

  dbus_service_.CreateDBusProxy();
  dbus_service_.Initialize();

  driver_.reset(new(std::nothrow) GdmDriver(this));
  if (!driver_.get()) {
    LOG(ERROR) << "Failed to create driver";
    return false;
  }

  if (!driver_->Initialize()) {
    LOG(ERROR) << "Failed to initialize driver";
    return false;
  }

  if (!ScanDevices())
    return false;

  return true;
}

bool Manager::Finalize() {
  CancelDeviceScan();
  devices_.clear();

  if (dbus_adaptor())
    dbus_adaptor()->UpdateDevices();

  if (!driver_.get())
    return true;

  if (!driver_->Finalize()) {
    LOG(ERROR) << "Failed to de-initialize driver";
    return false;
  }

  driver_.reset();
  dbus_service_.Finalize();
  return true;
}

bool Manager::ScanDevices() {
  device_scan_timeout_id_ = 0;

  if (!devices_.empty())
    return true;

  if (!driver_->GetDevices(&devices_.get())) {
    LOG(ERROR) << "Failed to get list of devices";
    return false;
  }

  if (!devices_.empty()) {
    for (size_t i = 0; i < devices_.size(); ++i)
      devices_[i]->CreateDBusAdaptor();

    dbus_adaptor()->UpdateDevices();
    return true;
  }

  // Some platforms may not have any WiMAX device, so instead of scanning
  // indefinitely, stop the device scan after a number of attempts.
  if (++num_device_scans_ < kMaxNumberOfDeviceScans) {
    VLOG(1) << "No WiMAX devices detected. Rescan later.";
    device_scan_timeout_id_ = g_timeout_add_seconds(
        kDefaultDeviceScanIntervalInSeconds, OnDeviceScanNeeded, this);
  }
  return true;
}

void Manager::CancelDeviceScan() {
  // Cancel any pending device scan.
  if (device_scan_timeout_id_ != 0) {
    g_source_remove(device_scan_timeout_id_);
    device_scan_timeout_id_ = 0;
  }
  num_device_scans_ = 0;
}

void Manager::Suspend() {
  CancelDeviceScan();
  devices_.clear();
  dbus_adaptor()->UpdateDevices();
}

void Manager::Resume() {
  // After resuming from suspend, the old device may not have been cleaned up.
  // Delay the device scan to avoid getting the old device.
  g_timeout_add_seconds(kDeviceScanDelayAfterResumeInSeconds,
                        OnDeviceScanNeeded, this);
}

bool Manager::LoadConfig(const base::FilePath &file_path) {
  int fd = HANDLE_EINTR(open(file_path.MaybeAsASCII().c_str(), O_RDONLY));
  if (fd == -1) {
    PLOG(ERROR) << "Failed to read config file '"
                << file_path.MaybeAsASCII() << "'";
    return false;
  }

  file_util::ScopedFD scoped_fd(&fd);
  google::protobuf::io::FileInputStream file_stream(fd);

  scoped_ptr<Config> config(new(std::nothrow) Config());
  if (!google::protobuf::TextFormat::Parse(&file_stream, config.get()))
    return false;

  config_ = config.Pass();
  return true;
}

}  // namespace wimax_manager
