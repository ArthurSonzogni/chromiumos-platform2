// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/sane_client_impl.h"

#include <optional>

#include <base/check.h>
#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>
#include <re2/re2.h>
#include <sane/saneopts.h>
#include <sane-airscan/airscan.h>

#include "lorgnette/dbus_adaptors/org.chromium.lorgnette.Manager.h"
#include "lorgnette/guess_source.h"
#include "lorgnette/sane_device_impl.h"
#include "lorgnette/scanner_match.h"

static const char* kDbusDomain = brillo::errors::dbus::kDomain;

namespace lorgnette {

// static
std::unique_ptr<SaneClientImpl> SaneClientImpl::Create(
    LibsaneWrapper* libsane) {
  SANE_Status status = libsane->sane_init(nullptr, nullptr);
  if (status != SANE_STATUS_GOOD) {
    LOG(ERROR) << "Unable to initialize SANE";
    return nullptr;
  }

  // Cannot use make_unique() with a private constructor.
  return std::unique_ptr<SaneClientImpl>(new SaneClientImpl(libsane));
}

SaneClientImpl::~SaneClientImpl() {
  libsane_->sane_exit();
}

std::optional<std::vector<ScannerInfo>> SaneClientImpl::ListDevices(
    brillo::ErrorPtr* error) {
  base::AutoLock auto_lock(lock_);
  const SANE_Device** device_list;
  SANE_Status status = libsane_->sane_get_devices(&device_list, SANE_FALSE);
  if (status != SANE_STATUS_GOOD) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "Unable to get device list from SANE");
    return std::nullopt;
  }

  return DeviceListToScannerInfo(device_list);
}

// static
std::optional<std::vector<ScannerInfo>> SaneClientImpl::DeviceListToScannerInfo(
    const SANE_Device** device_list) {
  if (!device_list) {
    LOG(ERROR) << "'device_list' cannot be NULL";
    return std::nullopt;
  }

  std::unordered_set<std::string> names;
  std::vector<ScannerInfo> scanners;
  for (int i = 0; device_list[i]; i++) {
    const SANE_Device* dev = device_list[i];
    if (!dev->name || strcmp(dev->name, "") == 0)
      continue;

    if (names.count(dev->name) != 0) {
      LOG(ERROR) << "Duplicate device name: " << dev->name;
      return std::nullopt;
    }
    names.insert(dev->name);

    ScannerInfo info;
    info.set_name(dev->name);
    info.set_manufacturer(dev->vendor ? dev->vendor : "");
    info.set_model(dev->model ? dev->model : "");
    info.set_type(dev->type ? dev->type : "");
    info.set_connection_type(ConnectionTypeForScanner(info));
    info.set_secure(info.connection_type() == lorgnette::CONNECTION_USB);
    scanners.push_back(info);
  }
  return scanners;
}

SaneClientImpl::SaneClientImpl(LibsaneWrapper* libsane)
    : libsane_(libsane), open_devices_(std::make_shared<DeviceSet>()) {}

std::unique_ptr<SaneDevice> SaneClientImpl::ConnectToDeviceInternal(
    brillo::ErrorPtr* error,
    SANE_Status* sane_status,
    const std::string& device_name) {
  LOG(INFO) << "Creating connection to device: " << device_name;
  base::AutoLock auto_lock(lock_);
  SANE_Handle handle;
  {
    base::AutoLock auto_lock(open_devices_->first);
    if (open_devices_->second.count(device_name) != 0) {
      brillo::Error::AddToPrintf(
          error, FROM_HERE, kDbusDomain, kManagerServiceError,
          "Device '%s' is currently in-use", device_name.c_str());
      return nullptr;
    }

    SANE_Status status = libsane_->sane_open(device_name.c_str(), &handle);
    if (status != SANE_STATUS_GOOD) {
      brillo::Error::AddToPrintf(error, FROM_HERE, kDbusDomain,
                                 kManagerServiceError,
                                 "Unable to open device '%s': %s",
                                 device_name.c_str(), sane_strstatus(status));
      if (sane_status)
        *sane_status = status;

      return nullptr;
    }

    open_devices_->second.insert(device_name);
  }

  // Cannot use make_unique() with a private constructor.
  auto device = std::unique_ptr<SaneDeviceImpl>(
      new SaneDeviceImpl(libsane_, handle, device_name, open_devices_));
  device->LoadOptions(error);
  return device;
}

}  // namespace lorgnette
