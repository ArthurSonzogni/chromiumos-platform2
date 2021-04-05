// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/peripheral_battery_watcher.h"

#include <fcntl.h>

#include <cerrno>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>
#include <re2/re2.h>

#include "power_manager/common/util.h"
#include "power_manager/powerd/system/dbus_wrapper.h"
#include "power_manager/proto_bindings/peripheral_battery_status.pb.h"

namespace power_manager {
namespace system {

namespace {

// Default path examined for peripheral battery directories.
const char kDefaultPeripheralBatteryPath[] = "/sys/class/power_supply/";

// Default interval for polling the device battery info.
const int kDefaultPollIntervalMs = 600000;

constexpr char kBluetoothAddressRegex[] =
    "^([0-9A-Fa-f]{2}:){5}([0-9A-Fa-f]{2})$";

constexpr char kPeripheralChargerRegex[] = ".*/PCHG([0-9]+)$";

// Reads |path| to |value_out| and trims trailing whitespace. False is returned
// if the file doesn't exist or can't be read.
bool ReadStringFromFile(const base::FilePath& path, std::string* value_out) {
  if (!base::ReadFileToString(path, value_out))
    return false;

  base::TrimWhitespaceASCII(*value_out, base::TRIM_TRAILING, value_out);
  return true;
}

std::string SysnameFromBluetoothAddress(const std::string& address) {
  return "hid-" + base::ToLowerASCII(address) + "-battery";
}

bool ExtractBluetoothAddress(const base::FilePath& path, std::string* address) {
  // Standard HID devices have the convention of "hid-{btaddr}-battery"
  // file name in /sys/class/power_supply."
  if (RE2::FullMatch(path.value(), ".*hid-(.+)-battery", address))
    return true;

  if (path.value().find("wacom") == std::string::npos)
    return false;

  // Handle wacom specifically, the Bluetooth address is in
  // /sys/class/power_suply/wacom_xxx/powers/uevent having HID_UNIQ= prefix.
  std::string uevent;
  return (ReadStringFromFile(path.Append("powers/uevent"), &uevent) &&
          RE2::PartialMatch(uevent, "HID_UNIQ=(.+)", address));
}

}  // namespace

const char PeripheralBatteryWatcher::kScopeFile[] = "scope";
const char PeripheralBatteryWatcher::kScopeValueDevice[] = "Device";
const char PeripheralBatteryWatcher::kStatusFile[] = "status";
const char PeripheralBatteryWatcher::kPowersUeventFile[] = "powers/uevent";
const char PeripheralBatteryWatcher::kStatusValueUnknown[] = "Unknown";
const char PeripheralBatteryWatcher::kStatusValueFull[] = "Full";
const char PeripheralBatteryWatcher::kStatusValueCharging[] = "Charging";
const char PeripheralBatteryWatcher::kStatusValueDischarging[] = "Discharging";
const char PeripheralBatteryWatcher::kStatusValueNotcharging[] = "Not charging";
const char PeripheralBatteryWatcher::kModelNameFile[] = "model_name";
const char PeripheralBatteryWatcher::kHealthFile[] = "health";
const char PeripheralBatteryWatcher::kHealthValueUnknown[] = "Unknown";
const char PeripheralBatteryWatcher::kHealthValueGood[] = "Good";
const char PeripheralBatteryWatcher::kCapacityFile[] = "capacity";
const char PeripheralBatteryWatcher::kUdevSubsystem[] = "power_supply";

PeripheralBatteryWatcher::PeripheralBatteryWatcher()
    : dbus_wrapper_(nullptr),
      peripheral_battery_path_(kDefaultPeripheralBatteryPath),
      poll_interval_ms_(kDefaultPollIntervalMs),
      bluez_battery_provider_(std::make_unique<BluezBatteryProvider>()),
      weak_ptr_factory_(this) {}

PeripheralBatteryWatcher::~PeripheralBatteryWatcher() {
  if (udev_)
    udev_->RemoveSubsystemObserver(kUdevSubsystem, this);
}

void PeripheralBatteryWatcher::Init(DBusWrapperInterface* dbus_wrapper,
                                    UdevInterface* udev) {
  udev_ = udev;
  udev_->AddSubsystemObserver(kUdevSubsystem, this);

  dbus_wrapper_ = dbus_wrapper;
  ReadBatteryStatuses();

  dbus_wrapper->ExportMethod(
      kRefreshBluetoothBatteryMethod,
      base::BindRepeating(
          &PeripheralBatteryWatcher::OnRefreshBluetoothBatteryMethodCall,
          weak_ptr_factory_.GetWeakPtr()));

  bluez_battery_provider_->Init(dbus_wrapper_->GetBus());
}

void PeripheralBatteryWatcher::OnUdevEvent(const UdevEvent& event) {
  base::FilePath path = base::FilePath(peripheral_battery_path_)
                            .Append(event.device_info.sysname);
  if (event.action == UdevEvent::Action::REMOVE || !IsPeripheralDevice(path))
    return;

  // An event of a peripheral device is detected through udev, Refresh the
  // battery status of that device.
  ReadBatteryStatus(path, true);
}

bool PeripheralBatteryWatcher::IsPeripheralDevice(
    const base::FilePath& device_path) const {
  // Peripheral batteries have device scopes.
  std::string scope;
  return (ReadStringFromFile(device_path.Append(kScopeFile), &scope) &&
          scope == kScopeValueDevice);
}

bool PeripheralBatteryWatcher::IsPeripheralChargerDevice(
    const base::FilePath& device_path) const {
  // Peripheral chargers have specific names.
  return (RE2::FullMatch(device_path.value(), kPeripheralChargerRegex));
}

void PeripheralBatteryWatcher::GetBatteryList(
    std::vector<base::FilePath>* battery_list) {
  battery_list->clear();
  base::FileEnumerator dir_enumerator(peripheral_battery_path_, false,
                                      base::FileEnumerator::DIRECTORIES);

  for (base::FilePath device_path = dir_enumerator.Next(); !device_path.empty();
       device_path = dir_enumerator.Next()) {
    if (!IsPeripheralDevice(device_path))
      continue;

    // Some devices may initially have an unknown status; avoid reporting
    // them: http://b/64392016. Unknown status for chargers is always
    // interesting.
    std::string status;
    if (!IsPeripheralChargerDevice(device_path) &&
        ReadStringFromFile(device_path.Append(kStatusFile), &status) &&
        status == kStatusValueUnknown)
      continue;

    battery_list->push_back(device_path);
  }
}

int PeripheralBatteryWatcher::ReadChargeStatus(
    const base::FilePath& path) const {
  // sysfs entry "status" has the current charge status, "health" has battery
  // health.
  base::FilePath status_path = path.Append(kStatusFile);
  base::FilePath health_path = path.Append(kHealthFile);

  // NOTE: This code is assuming that the status and health sysfs files are
  // relatively fast to read, and will not trigger significant delays, i.e.,
  // do not involve Bluetooth traffic to possibly non-responsive receivers.

  // First check health; if it is known and not good, report an error.
  std::string health;
  if (ReadStringFromFile(health_path, &health)) {
    if (health != kHealthValueUnknown && health != kHealthValueGood) {
      return PeripheralBatteryStatus_ChargeStatus_CHARGE_STATUS_ERROR;
    }
  }

  // Then check general status, looking for known states.
  std::string status;
  if (!ReadStringFromFile(status_path, &status))
    return PeripheralBatteryStatus_ChargeStatus_CHARGE_STATUS_UNKNOWN;

  if (status == kStatusValueCharging)
    return PeripheralBatteryStatus_ChargeStatus_CHARGE_STATUS_CHARGING;
  else if (status == kStatusValueDischarging)
    return PeripheralBatteryStatus_ChargeStatus_CHARGE_STATUS_DISCHARGING;
  else if (status == kStatusValueNotcharging)
    return PeripheralBatteryStatus_ChargeStatus_CHARGE_STATUS_NOT_CHARGING;
  else if (status == kStatusValueFull)
    return PeripheralBatteryStatus_ChargeStatus_CHARGE_STATUS_FULL;
  else
    return PeripheralBatteryStatus_ChargeStatus_CHARGE_STATUS_UNKNOWN;
}

void PeripheralBatteryWatcher::ReadBatteryStatus(const base::FilePath& path,
                                                 bool active_update) {
  // sysfs entry "capacity" has the current battery level.
  base::FilePath capacity_path = path.Append(kCapacityFile);
  if (!base::PathExists(capacity_path))
    return;

  std::string model_name;
  if (!IsPeripheralChargerDevice(path) &&
      !ReadStringFromFile(path.Append(kModelNameFile), &model_name))
    return;

  int status;
  status = ReadChargeStatus(path);

  battery_readers_.push_back(std::make_unique<AsyncFileReader>());
  AsyncFileReader* reader = battery_readers_.back().get();

  if (reader->Init(capacity_path)) {
    reader->StartRead(base::Bind(&PeripheralBatteryWatcher::ReadCallback,
                                 base::Unretained(this), path, model_name,
                                 status, active_update),
                      base::Bind(&PeripheralBatteryWatcher::ErrorCallback,
                                 base::Unretained(this), path, model_name));
  } else {
    LOG(ERROR) << "Can't read battery capacity " << capacity_path.value();
  }
}

void PeripheralBatteryWatcher::ReadBatteryStatuses() {
  battery_readers_.clear();

  std::vector<base::FilePath> new_battery_list;
  GetBatteryList(&new_battery_list);

  for (const base::FilePath& path : new_battery_list) {
    ReadBatteryStatus(path, false);
  }

  poll_timer_.Start(FROM_HERE,
                    base::TimeDelta::FromMilliseconds(poll_interval_ms_), this,
                    &PeripheralBatteryWatcher::ReadBatteryStatuses);
}

void PeripheralBatteryWatcher::SendBatteryStatus(const base::FilePath& path,
                                                 const std::string& model_name,
                                                 int level,
                                                 int charge_status,
                                                 bool active_update) {
  std::string address;
  if (ExtractBluetoothAddress(path, &address) &&
      RE2::FullMatch(address, kBluetoothAddressRegex)) {
    // Bluetooth batteries is reported separately to BlueZ.
    bluez_battery_provider_->UpdateDeviceBattery(address, level);
    return;
  }

  PeripheralBatteryStatus proto;
  proto.set_path(path.value());
  proto.set_name(model_name);
  proto.set_charge_status(
      (power_manager::PeripheralBatteryStatus_ChargeStatus)charge_status);
  if (level >= 0)
    proto.set_level(level);
  proto.set_active_update(active_update);

  dbus_wrapper_->EmitSignalWithProtocolBuffer(kPeripheralBatteryStatusSignal,
                                              proto);
}

void PeripheralBatteryWatcher::ReadCallback(const base::FilePath& path,
                                            const std::string& model_name,
                                            int status,
                                            bool active_update,
                                            const std::string& data) {
  std::string trimmed_data;
  base::TrimWhitespaceASCII(data, base::TRIM_ALL, &trimmed_data);
  int level = -1;
  if (base::StringToInt(trimmed_data, &level)) {
    SendBatteryStatus(path, model_name, level, status, active_update);
  } else {
    LOG(ERROR) << "Invalid battery level reading : [" << data << "]"
               << " from " << path.value();
  }
}

void PeripheralBatteryWatcher::ErrorCallback(const base::FilePath& path,
                                             const std::string& model_name) {
  SendBatteryStatus(path, model_name, -1,
                    PeripheralBatteryStatus_ChargeStatus_CHARGE_STATUS_UNKNOWN,
                    false);
}

void PeripheralBatteryWatcher::OnRefreshBluetoothBatteryMethodCall(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  dbus::MessageReader reader(method_call);

  std::string address;
  if (!reader.PopString(&address)) {
    LOG(WARNING) << "Failed to pop Bluetooth device address from "
                 << kRefreshBluetoothBatteryMethod << " D-Bus method call";
    std::move(response_sender)
        .Run(
            std::unique_ptr<dbus::Response>(dbus::ErrorResponse::FromMethodCall(
                method_call, DBUS_ERROR_INVALID_ARGS,
                "Expected device address string")));
    return;
  }

  // Only process requests for valid Bluetooth addresses.
  if (RE2::FullMatch(address, kBluetoothAddressRegex)) {
    base::FilePath path = base::FilePath(peripheral_battery_path_)
                              .Append(SysnameFromBluetoothAddress(address));
    ReadBatteryStatus(path,
                      true /* active, as bluetooth will interrogate device */);
  }

  // Best effort, always return success.
  std::unique_ptr<dbus::Response> response =
      dbus::Response::FromMethodCall(method_call);
  std::move(response_sender).Run(std::move(response));
}

}  // namespace system
}  // namespace power_manager
