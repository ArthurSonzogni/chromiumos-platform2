// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/events/udev_events_impl.h"

#include <libusb.h>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_enumerator.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <brillo/udev/udev_device.h>

#include "diagnostics/base/file_utils.h"
#include "diagnostics/cros_healthd/utils/usb_utils.h"
#include "diagnostics/cros_healthd/utils/usb_utils_constants.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom-forward.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

std::string GetString(const char* str) {
  if (str) {
    return std::string(str);
  }

  return "";
}

void FillUsbCategory(const std::unique_ptr<brillo::UdevDevice>& device,
                     mojom::UsbEventInfo* info) {
  auto sys_path = GetString(device->GetSysPath());
  uint32_t class_code = 0;
  std::set<std::string> categories;

  ReadInteger(base::FilePath(sys_path), kFileUsbDevClass,
              &base::HexStringToUInt, &class_code);
  if (class_code != libusb_class_code::LIBUSB_CLASS_PER_INTERFACE) {
    categories.insert(LookUpUsbDeviceClass(class_code));
  } else {  // The category is determined by interfaces.
    base::FileEnumerator file_enum(base::FilePath(sys_path), false,
                                   base::FileEnumerator::FileType::DIRECTORIES);
    for (auto path = file_enum.Next(); !path.empty(); path = file_enum.Next()) {
      std::string content;
      ReadAndTrimString(path.Append(kFileUsbIFClass), &content);
      if (!base::HexStringToUInt(content, &class_code))
        continue;
      categories.insert(LookUpUsbDeviceClass(class_code));
    }
  }

  categories.erase("Unknown");
  for (const auto& category : categories) {
    info->categories.push_back(category);
  }
}

void FillUsbEventInfo(const std::unique_ptr<brillo::UdevDevice>& device,
                      mojom::UsbEventInfo* info) {
  info->vendor = GetUsbVendorName(device);
  info->name = GetUsbProductName(device);
  std::tie(info->vid, info->pid) = GetUsbVidPid(device);
  FillUsbCategory(device, info);
}

}  // namespace

UdevEventsImpl::UdevEventsImpl(Context* context) : context_(context) {
  DCHECK(context_);
}

bool UdevEventsImpl::Initialize() {
  if (!context_->udev_monitor()->EnableReceiving()) {
    LOG(ERROR) << "Failed to enable receiving for udev monitor.";
    return false;
  }

  int fd = context_->udev_monitor()->GetFileDescriptor();
  if (fd == brillo::UdevMonitor::kInvalidFileDescriptor) {
    LOG(ERROR) << "Failed to get udev monitor fd.";
    return false;
  }

  udev_monitor_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      fd, base::BindRepeating(&UdevEventsImpl::OnUdevEvent,
                              base::Unretained(this)));

  if (!udev_monitor_watcher_) {
    LOG(ERROR) << "Failed to start watcher for udev monitor fd.";
    return false;
  }

  return true;
}

void UdevEventsImpl::OnUdevEvent() {
  auto device = context_->udev_monitor()->ReceiveDevice();
  if (!device) {
    LOG(ERROR) << "Udev receive device failed.";
    return;
  }

  auto action = GetString(device->GetAction());
  auto subsystem = GetString(device->GetSubsystem());
  auto device_type = GetString(device->GetDeviceType());

  // Distinguished events by subsystem and action here.
  if (subsystem == "thunderbolt") {
    if (action == "add") {
      OnThunderboltAddEvent();
    } else if (action == "remove") {
      OnThunderboltRemoveEvent();
    } else if (action == "change") {
      auto path = base::FilePath(device->GetSysPath());
      std::string authorized;
      if (ReadAndTrimString(path.Append("authorized"), &authorized)) {
        unsigned auth;
        base::StringToUint(authorized, &auth);
        auth ? OnThunderboltAuthorizedEvent()
             : OnThunderboltUnAuthorizedEvent();
      }
    }
  } else if (subsystem == "usb" && device_type == "usb_device") {
    if (action == "add") {
      OnUsbAdd(device);
    } else if (action == "remove") {
      OnUsbRemove(device);
    }
  } else if (subsystem == "mmc") {
    if (action == "add") {
      OnSdCardAdd();
    } else if (action == "remove") {
      OnSdCardRemove();
    }
  } else if (subsystem == "drm" && device_type == "drm_minor") {
    if (action == "change") {
      auto device_type = GetString(device->GetDeviceType());
      OnHdmiChange();
    }
  }
}

void UdevEventsImpl::AddThunderboltObserver(
    mojo::PendingRemote<mojom::EventObserver> observer) {
  thunderbolt_observers_.Add(std::move(observer));
}

void UdevEventsImpl::AddThunderboltObserver(
    mojo::PendingRemote<mojom::CrosHealthdThunderboltObserver> observer) {
  deprecated_thunderbolt_observers_.Add(std::move(observer));
}

void UdevEventsImpl::OnThunderboltAddEvent() {
  mojom::ThunderboltEventInfo info;
  info.state = mojom::ThunderboltEventInfo::State::kAdd;
  for (auto& observer : thunderbolt_observers_)
    observer->OnEvent(mojom::EventInfo::NewThunderboltEventInfo(info.Clone()));
  for (auto& observer : deprecated_thunderbolt_observers_)
    observer->OnAdd();
}

void UdevEventsImpl::OnThunderboltRemoveEvent() {
  mojom::ThunderboltEventInfo info;
  info.state = mojom::ThunderboltEventInfo::State::kRemove;
  for (auto& observer : thunderbolt_observers_)
    observer->OnEvent(mojom::EventInfo::NewThunderboltEventInfo(info.Clone()));
  for (auto& observer : deprecated_thunderbolt_observers_)
    observer->OnRemove();
}

void UdevEventsImpl::OnThunderboltAuthorizedEvent() {
  mojom::ThunderboltEventInfo info;
  info.state = mojom::ThunderboltEventInfo::State::kAuthorized;
  for (auto& observer : thunderbolt_observers_)
    observer->OnEvent(mojom::EventInfo::NewThunderboltEventInfo(info.Clone()));
  for (auto& observer : deprecated_thunderbolt_observers_)
    observer->OnAuthorized();
}

void UdevEventsImpl::OnThunderboltUnAuthorizedEvent() {
  mojom::ThunderboltEventInfo info;
  info.state = mojom::ThunderboltEventInfo::State::kUnAuthorized;
  for (auto& observer : thunderbolt_observers_)
    observer->OnEvent(mojom::EventInfo::NewThunderboltEventInfo(info.Clone()));
  for (auto& observer : deprecated_thunderbolt_observers_)
    observer->OnUnAuthorized();
}

void UdevEventsImpl::AddUsbObserver(
    mojo::PendingRemote<mojom::CrosHealthdUsbObserver> observer) {
  deprecated_usb_observers_.Add(std::move(observer));
}

void UdevEventsImpl::AddUsbObserver(
    mojo::PendingRemote<mojom::EventObserver> observer) {
  usb_observers_.Add(std::move(observer));
}

void UdevEventsImpl::OnUsbAdd(
    const std::unique_ptr<brillo::UdevDevice>& device) {
  mojom::UsbEventInfo info;
  FillUsbEventInfo(device, &info);
  info.state = mojom::UsbEventInfo::State::kAdd;

  for (auto& observer : usb_observers_)
    observer->OnEvent(mojom::EventInfo::NewUsbEventInfo(info.Clone()));
  for (auto& observer : deprecated_usb_observers_)
    observer->OnAdd(info.Clone());
}

void UdevEventsImpl::OnUsbRemove(
    const std::unique_ptr<brillo::UdevDevice>& device) {
  mojom::UsbEventInfo info;
  FillUsbEventInfo(device, &info);
  info.state = mojom::UsbEventInfo::State::kRemove;

  for (auto& observer : usb_observers_)
    observer->OnEvent(mojom::EventInfo::NewUsbEventInfo(info.Clone()));
  for (auto& observer : deprecated_usb_observers_)
    observer->OnRemove(info.Clone());
}

void UdevEventsImpl::AddSdCardObserver(
    mojo::PendingRemote<mojom::EventObserver> observer) {
  sd_card_observers_.Add(std::move(observer));
}

void UdevEventsImpl::OnSdCardAdd() {
  mojom::SdCardEventInfo info;
  info.state = mojom::SdCardEventInfo::State::kAdd;
  for (auto& observer : sd_card_observers_) {
    observer->OnEvent(mojom::EventInfo::NewSdCardEventInfo(info.Clone()));
  }
}

void UdevEventsImpl::OnSdCardRemove() {
  mojom::SdCardEventInfo info;
  info.state = mojom::SdCardEventInfo::State::kRemove;
  for (auto& observer : sd_card_observers_)
    observer->OnEvent(mojom::EventInfo::NewSdCardEventInfo(info.Clone()));
}

void UdevEventsImpl::AddHdmiObserver(
    mojo::PendingRemote<mojom::EventObserver> observer) {
  hdmi_observers_.Add(std::move(observer));
}

void UdevEventsImpl::OnHdmiChange() {
  NOTIMPLEMENTED();
}

}  // namespace diagnostics
