// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/modem.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <base/containers/contains.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <base/unguessable_token.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <brillo/strings/string_utils.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/switches/modemfwd_switches.h>
#include <modemmanager/dbus-proxies.h>
#include <ModemManager/ModemManager.h>

#include "modemfwd/logging.h"
#include "modemfwd/modem_helper.h"
#include "modemfwd/modem_sandbox.h"

namespace {

constexpr base::TimeDelta kCmdKillDelay = base::Seconds(1);

class Inhibitor {
 public:
  Inhibitor(std::unique_ptr<org::freedesktop::ModemManager1Proxy> mm_proxy,
            const std::string& physdev_uid)
      : mm_proxy_(std::move(mm_proxy)), physdev_uid_(physdev_uid) {}

  bool SetInhibited(bool inhibited) {
    brillo::ErrorPtr error_unused;
    return mm_proxy_->InhibitDevice(physdev_uid_, inhibited, &error_unused);
  }

 private:
  std::unique_ptr<org::freedesktop::ModemManager1Proxy> mm_proxy_;
  std::string physdev_uid_;
};

std::unique_ptr<Inhibitor> GetInhibitor(
    dbus::Bus* bus, const dbus::ObjectPath& mm_object_path) {
  CHECK(mm_object_path.IsValid());

  // Get the MM object backing this modem, and retrieve its Device property.
  // This is the physdev_uid we use for inhibition during updates.
  auto mm_device = bus->GetObjectProxy(modemmanager::kModemManager1ServiceName,
                                       mm_object_path);
  if (!mm_device)
    return nullptr;

  brillo::ErrorPtr error;
  auto resp = brillo::dbus_utils::CallMethodAndBlock(
      mm_device, dbus::kDBusPropertiesInterface, dbus::kDBusPropertiesGet,
      &error, std::string(modemmanager::kModemManager1ModemInterface),
      std::string(MM_MODEM_PROPERTY_DEVICE));
  if (!resp)
    return nullptr;

  std::string mm_physdev_uid;
  dbus::MessageReader reader(resp.get());
  if (!reader.PopVariantOfString(&mm_physdev_uid)) {
    LOG(WARNING) << "Error popping string entry from D-Bus message";
    return nullptr;
  }
  EVLOG(1) << "Modem " << mm_object_path.value() << " has physdev UID "
           << mm_physdev_uid;
  auto mm_proxy = std::make_unique<org::freedesktop::ModemManager1Proxy>(
      bus, modemmanager::kModemManager1ServiceName);
  return std::make_unique<Inhibitor>(std::move(mm_proxy), mm_physdev_uid);
}

class HealthChecker {
 public:
  virtual ~HealthChecker() = default;

  virtual bool Check() = 0;
};

class MbimHealthChecker : public HealthChecker {
 public:
  explicit MbimHealthChecker(std::string port) : port_(port) {}
  ~MbimHealthChecker() override = default;

  bool Check() override {
    std::vector<std::string> cmd_args;

    cmd_args.push_back("/usr/bin/mbimcli");
    cmd_args.push_back("-d");
    cmd_args.push_back("/dev/" + port_);
    cmd_args.push_back("-p");
    cmd_args.push_back("--query-device-caps");

    const base::FilePath mbimcli_seccomp_policy_file(
        base::StringPrintf("%s/modemfwd-mbimcli-seccomp.policy",
                           modemfwd::kSeccompPolicyDirectory));
    return modemfwd::RunProcessInSandboxWithTimeout(
               cmd_args, mbimcli_seccomp_policy_file, true, nullptr, nullptr,
               kCmdKillDelay) == 0;
  }

 private:
  std::string port_;
};

std::unique_ptr<HealthChecker> GetHealthChecker(
    std::unique_ptr<org::freedesktop::ModemManager1::ModemProxy> modem_object) {
  modem_object->InitializeProperties(base::DoNothing());
  if (!modem_object->GetProperties()->primary_port.GetAndBlock()) {
    LOG(ERROR) << "Could not fetch primary port property";
    return nullptr;
  }
  std::string primary_port_name = modem_object->primary_port();

  if (!modem_object->GetProperties()->ports.GetAndBlock()) {
    LOG(ERROR) << "Could not fetch ports property";
    return nullptr;
  }
  for (const auto& [name, type] : modem_object->ports()) {
    if (name != primary_port_name)
      continue;

    switch (type) {
      case MM_MODEM_PORT_TYPE_MBIM:
        ELOG(INFO) << "Found MBIM port " << primary_port_name
                   << " for health checks";
        return std::unique_ptr<HealthChecker>(
            new MbimHealthChecker(primary_port_name));
      default:
        continue;
    }
  }

  ELOG(INFO) << "No suitable primary port found for health checks";
  return nullptr;
}

}  // namespace

namespace modemfwd {

std::ostream& operator<<(std::ostream& os, const Modem::State& rhs) {
  switch (rhs) {
    case Modem::State::FAILED:
      os << "FAILED";
      break;
    case Modem::State::UNKNOWN:
      os << "UNKNOWN";
      break;
    case Modem::State::INITIALIZING:
      os << "INITIALIZING";
      break;
    case Modem::State::LOCKED:
      os << "LOCKED";
      break;
    case Modem::State::DISABLED:
      os << "DISABLED";
      break;
    case Modem::State::DISABLING:
      os << "DISABLING";
      break;
    case Modem::State::ENABLING:
      os << "ENABLING";
      break;
    case Modem::State::ENABLED:
      os << "ENABLED";
      break;
    case Modem::State::SEARCHING:
      os << "SEARCHING";
      break;
    case Modem::State::REGISTERED:
      os << "REGISTERED";
      break;
    case Modem::State::DISCONNECTING:
      os << "DISCONNECTING";
      break;
    case Modem::State::CONNECTING:
      os << "CONNECTING";
      break;
    case Modem::State::CONNECTED:
      os << "CONNECTED";
      break;
    default:
      os << "INVALID";
      break;
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const Modem::PowerState& rhs) {
  switch (rhs) {
    case (Modem::PowerState::UNKNOWN):
      os << "UNKNOWN";
      break;
    case (Modem::PowerState::OFF):
      os << "OFF";
      break;
    case (Modem::PowerState::LOW):
      os << "LOW";
      break;
    case (Modem::PowerState::ON):
      os << "ON";
      break;
    default:
      os << "INVALID";
      break;
  }
  return os;
}

class ModemImpl : public Modem {
 public:
  ModemImpl(const std::string& device_id,
            const std::string& equipment_id,
            const std::string& carrier_id,
            std::unique_ptr<HealthChecker> health_checker,
            std::unique_ptr<Inhibitor> inhibitor,
            ModemHelper* helper,
            FirmwareInfo installed_firmware)
      : device_id_(device_id),
        equipment_id_(equipment_id),
        carrier_id_(carrier_id),
        health_checker_(std::move(health_checker)),
        inhibitor_(std::move(inhibitor)),
        installed_firmware_(installed_firmware),
        helper_(helper) {}

  ModemImpl(const ModemImpl&) = delete;
  ModemImpl& operator=(const ModemImpl&) = delete;

  ~ModemImpl() override = default;

  // modemfwd::Modem overrides.
  bool IsPresent() const override { return true; }

  std::string GetDeviceId() const override { return device_id_; }

  std::string GetEquipmentId() const override { return equipment_id_; }

  std::string GetCarrierId() const override { return carrier_id_; }

  ModemHelper* GetHelper() const override { return helper_; }

  std::string GetMainFirmwareVersion() const override {
    return installed_firmware_.main_version;
  }

  std::string GetOemFirmwareVersion() const override {
    return installed_firmware_.oem_version;
  }

  std::string GetCarrierFirmwareId() const override {
    return installed_firmware_.carrier_uuid;
  }

  std::string GetCarrierFirmwareVersion() const override {
    return installed_firmware_.carrier_version;
  }

  std::string GetAssocFirmwareVersion(std::string fw_tag) const override {
    std::map<std::string, std::string>::const_iterator pos =
        installed_firmware_.assoc_versions.find(fw_tag);
    if (pos == installed_firmware_.assoc_versions.end())
      return "";
    else
      return pos->second;
  }

  bool SetInhibited(bool inhibited) override {
    if (!inhibitor_) {
      EVLOG(1) << "Inhibiting unavailable on this modem";
      return false;
    }
    return inhibitor_->SetInhibited(inhibited);
  }

  bool FlashFirmwares(const std::vector<FirmwareConfig>& configs) override {
    return helper_->FlashFirmwares(configs);
  }

  bool ClearAttachAPN(const std::string& carrier_uuid) override {
    // TODO(b/298680267): Revert this as part of Attach APN cleanup
    // We only need to clear attach apn on L850.
    if (base::Contains(device_id_, "usb:2cb7:0007")) {
      return helper_->ClearAttachAPN(carrier_uuid);
    }
    return true;
  }

  bool SupportsHealthCheck() const override { return !!health_checker_; }

  bool CheckHealth() override {
    return health_checker_ && health_checker_->Check();
  }

  State GetState() const override { return state_; }

  PowerState GetPowerState() const override { return power_state_; }

  bool UpdateState(State new_state) override {
    EVLOG(1) << __func__ << ": new modem state: " << new_state;
    if (new_state > State::CONNECTED || new_state < State::FAILED) {
      LOG(ERROR) << "Invalid modem state: " << new_state;
      return false;
    }

    if (state_ == new_state) {
      ELOG(WARNING) << "State (" << state_ << ") did not change.";
      return false;
    } else {
      state_ = new_state;
    }
    return true;
  }

  bool UpdatePowerState(PowerState new_power_state) override {
    EVLOG(1) << __func__ << ": new power state: " << new_power_state;
    if (new_power_state > PowerState::ON ||
        new_power_state < PowerState::UNKNOWN) {
      LOG(ERROR) << "Invalid power state: " << new_power_state;
      return false;
    }

    if (power_state_ == new_power_state) {
      ELOG(WARNING) << "Power state (" << power_state_ << ") did not change.";
      return false;
    } else {
      power_state_ = new_power_state;
    }
    return true;
  }

 private:
  int heartbeat_failures_;
  State state_;
  PowerState power_state_;
  std::string heartbeat_port_;
  std::string device_id_;
  std::string equipment_id_;
  std::string carrier_id_;
  std::unique_ptr<HealthChecker> health_checker_;
  std::unique_ptr<Inhibitor> inhibitor_;
  FirmwareInfo installed_firmware_;
  ModemHelper* helper_;
};

std::unique_ptr<Modem> CreateModem(
    dbus::Bus* bus,
    std::unique_ptr<org::chromium::flimflam::DeviceProxyInterface> device,
    ModemHelperDirectory* helper_directory) {
  std::string object_path = device->GetObjectPath().value();
  DVLOG(1) << "Creating modem proxy for " << object_path;

  brillo::ErrorPtr error;
  brillo::VariantDictionary properties;
  if (!device->GetProperties(&properties, &error)) {
    LOG(WARNING) << "Could not get properties for modem " << object_path;
    return nullptr;
  }

  // If we don't have a device ID, modemfwd can't do anything with this modem,
  // so check it first and just return if we can't find it.
  std::string device_id;
  if (!properties[shill::kDeviceIdProperty].GetValue(&device_id)) {
    LOG(INFO) << "Modem " << object_path << " has no device ID, ignoring";
    return nullptr;
  }

  // Equipment ID is also pretty important since we use it as a stable
  // identifier that can distinguish between modems of the same type.
  std::string equipment_id;
  if (!properties[shill::kEquipmentIdProperty].GetValue(&equipment_id)) {
    LOG(INFO) << "Modem " << object_path << " has no equipment ID, ignoring";
    return nullptr;
  }
  std::string firmware_revision;
  if (!properties[shill::kFirmwareRevisionProperty].GetValue(
          &firmware_revision)) {
    LOG(INFO) << "Modem " << object_path << " has no firmware revision";
  }
  // This property may not exist and it's not a big deal if it doesn't.
  std::map<std::string, std::string> operator_info;
  std::string carrier_id;
  if (properties[shill::kHomeProviderProperty].GetValue(&operator_info))
    carrier_id = operator_info[shill::kOperatorUuidKey];

  // Get a helper object for inhibiting the modem, if possible.
  std::unique_ptr<Inhibitor> inhibitor;
  std::string mm_object_path_prop;
  if (!properties[shill::kDBusObjectProperty].GetValue(&mm_object_path_prop)) {
    LOG(INFO) << "Modem " << object_path << " has no ModemManager object";
    return nullptr;
  }
  dbus::ObjectPath mm_object_path(mm_object_path_prop);
  if (!mm_object_path.IsValid()) {
    LOG(WARNING) << "Modem " << object_path
                 << " has invalid ModemManager object " << mm_object_path_prop;
    return nullptr;
  }

  inhibitor = GetInhibitor(bus, dbus::ObjectPath(mm_object_path));
  if (!inhibitor)
    LOG(INFO) << "Inhibiting modem " << object_path << " will not be possible";

  // Use the device ID to grab a helper.
  ModemHelper* helper = helper_directory->GetHelperForDeviceId(device_id);
  if (!helper) {
    LOG(INFO) << "No helper found to update modems with ID [" << device_id
              << "]";
    return nullptr;
  }

  FirmwareInfo installed_firmware;
  if (!helper->GetFirmwareInfo(&installed_firmware, firmware_revision)) {
    LOG(WARNING) << "Could not fetch installed firmware information";
    return nullptr;
  }

  std::unique_ptr<HealthChecker> health_checker;
  auto mm_object =
      std::make_unique<org::freedesktop::ModemManager1::ModemProxy>(
          bus, MM_DBUS_SERVICE, dbus::ObjectPath(mm_object_path));
  if (!mm_object) {
    LOG(WARNING) << "Could not fetch primary port information";
  } else {
    health_checker = GetHealthChecker(std::move(mm_object));
  }

  return std::make_unique<ModemImpl>(
      device_id, equipment_id, carrier_id, std::move(health_checker),
      std::move(inhibitor), helper, installed_firmware);
}

// StubModem acts like a modem with a particular device ID but does not
// actually talk to a real modem. This allows us to use it for force-
// flashing.
class StubModem : public Modem {
 public:
  StubModem(const std::string& device_id,
            ModemHelper* helper,
            FirmwareInfo installed_firmware)
      : device_id_(device_id),
        equipment_id_(base::UnguessableToken().Create().ToString()),
        helper_(helper),
        installed_firmware_(installed_firmware) {}
  StubModem(const StubModem&) = delete;
  StubModem& operator=(const StubModem&) = delete;

  ~StubModem() override = default;

  // modemfwd::Modem overrides.
  bool IsPresent() const override { return false; }

  std::string GetDeviceId() const override { return device_id_; }

  std::string GetEquipmentId() const override { return equipment_id_; }

  std::string GetCarrierId() const override { return ""; }

  ModemHelper* GetHelper() const override { return helper_; }

  std::string GetMainFirmwareVersion() const override {
    return installed_firmware_.main_version;
  }

  std::string GetOemFirmwareVersion() const override {
    return installed_firmware_.oem_version;
  }

  std::string GetCarrierFirmwareId() const override {
    return installed_firmware_.carrier_uuid;
  }

  std::string GetCarrierFirmwareVersion() const override {
    return installed_firmware_.carrier_version;
  }

  std::string GetAssocFirmwareVersion(std::string) const override { return ""; }

  bool SetInhibited(bool inhibited) override { return true; }

  bool FlashFirmwares(const std::vector<FirmwareConfig>& configs) override {
    return helper_->FlashFirmwares(configs);
  }

  bool ClearAttachAPN(const std::string& carrier_uuid) override {
    // TODO(b/298680267): Revert this as part of Attach APN cleanup
    // We only need to clear attach apn on L850.
    if (base::Contains(device_id_, "usb:2cb7:0007")) {
      return helper_->ClearAttachAPN(carrier_uuid);
    }
    return true;
  }

  bool SupportsHealthCheck() const override { return false; }

  bool CheckHealth() override { return false; }

  State GetState() const override { return state_; }
  PowerState GetPowerState() const override { return power_state_; }

  bool UpdateState(State new_state) override { return true; }

  bool UpdatePowerState(PowerState new_power_state) override { return true; }

 private:
  int heartbeat_failures_;
  State state_;
  PowerState power_state_;
  std::string heartbeat_port_;
  std::string carrier_id_;
  std::string device_id_;
  std::string equipment_id_;
  ModemHelper* helper_;
  FirmwareInfo installed_firmware_;
};

std::unique_ptr<Modem> CreateStubModem(const std::string& device_id,
                                       ModemHelperDirectory* helper_directory,
                                       bool use_real_fw_info) {
  // Use the device ID to grab a helper.
  ModemHelper* helper = helper_directory->GetHelperForDeviceId(device_id);
  if (!helper) {
    LOG(INFO) << "No helper found to update modems with ID [" << device_id
              << "]";
    return nullptr;
  }
  FirmwareInfo installed_firmware;
  if (use_real_fw_info && !helper->GetFirmwareInfo(&installed_firmware, "")) {
    LOG(ERROR) << "Could not fetch installed firmware information";
    return nullptr;
  }
  return std::make_unique<StubModem>(device_id, helper,
                                     std::move(installed_firmware));
}

}  // namespace modemfwd
