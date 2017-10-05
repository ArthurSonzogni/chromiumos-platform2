// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wimax_manager/manager.h"

#include <fcntl.h>

#include <memory>
#include <utility>

#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/text_format.h>

#include "wimax_manager/dbus_service_dbus_proxy.h"
#include "wimax_manager/device.h"
#include "wimax_manager/device_dbus_adaptor.h"
#include "wimax_manager/event_dispatcher.h"
#include "wimax_manager/gdm_driver.h"
#include "wimax_manager/manager_dbus_adaptor.h"
#include "wimax_manager/proto_bindings/config.pb.h"
#include "wimax_manager/proto_bindings/network_operator.pb.h"

namespace wimax_manager {

namespace {

const int kMaxNumberOfDeviceScans = 15;
const int kDefaultDeviceScanIntervalInSeconds = 1;
const int kDeviceScanDelayAfterResumeInSeconds = 3;
const char kDefaultConfigFile[] = "/usr/share/wimax-manager/default.conf";

}  // namespace

Manager::Manager(EventDispatcher* dispatcher)
    : dispatcher_(dispatcher), num_device_scans_(0), dbus_service_(this) {}

Manager::~Manager() {
  Finalize();
}

bool Manager::Initialize() {
  if (driver_.get())
    return true;

  if (!LoadConfig(base::FilePath(kDefaultConfigFile))) {
    LOG(ERROR) << "Failed to load config";
    return false;
  }

  dbus_service_.CreateDBusProxy();
  dbus_service_.Initialize();

  driver_.reset(new GdmDriver(this));
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
  device_scan_timer_.Stop();

  if (!devices_.empty())
    return true;

  if (!driver_->GetDevices(&devices_)) {
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
    device_scan_timer_.Start(
        FROM_HERE,
        base::TimeDelta::FromSeconds(kDefaultDeviceScanIntervalInSeconds), this,
        &Manager::OnDeviceScan);
  }
  return true;
}

void Manager::OnDeviceScan() {
  ScanDevices();
}

void Manager::CancelDeviceScan() {
  // Cancel any pending device scan.
  device_scan_timer_.Stop();
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
  device_scan_timer_.Start(
      FROM_HERE,
      base::TimeDelta::FromSeconds(kDeviceScanDelayAfterResumeInSeconds), this,
      &Manager::OnDeviceScan);
}

bool Manager::LoadConfig(const base::FilePath& file_path) {
  int fd = HANDLE_EINTR(open(file_path.MaybeAsASCII().c_str(), O_RDONLY));
  if (fd == -1) {
    PLOG(ERROR) << "Failed to read config file '" << file_path.MaybeAsASCII()
                << "'";
    return false;
  }

  base::ScopedFD scoped_fd(fd);
  google::protobuf::io::FileInputStream file_stream(fd);

  auto config = std::make_unique<Config>();
  if (!google::protobuf::TextFormat::Parse(&file_stream, config.get()))
    return false;

  config_ = std::move(config);
  return true;
}

const NetworkOperator* Manager::GetNetworkOperator(
    Network::Identifier network_id) const {
  if (!config_.get())
    return nullptr;

  for (int i = 0; i < config_->network_operator_size(); ++i) {
    const NetworkOperator& network_operator = config_->network_operator(i);
    if (network_operator.identifier() == network_id)
      return &network_operator;
  }

  return nullptr;
}

}  // namespace wimax_manager
