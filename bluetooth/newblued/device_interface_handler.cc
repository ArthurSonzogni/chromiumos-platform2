// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bluetooth/newblued/device_interface_handler.h"

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/compiler_specific.h>
#include <base/stl_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>

#include "bluetooth/newblued/adapter_interface_handler.h"
#include "bluetooth/newblued/util.h"

namespace bluetooth {

namespace {

// Appended at the end of LE device names. This is a UX mark to indicate to user
// that this device originates from NewBlue stack. This will be removed once
// NewBlue is stable and enabled by default.
// TODO(sonnysasaka): Remove this when NewBlue is enabled by default.
constexpr char kNewblueNameSuffix[] = " \xF0\x9F\x90\xBE";

// The default values of Flags, TX Power, EIR Class, Appearance and
// Manufacturer ID come from the assigned numbers and Supplement to the
// Bluetooth Core Specification defined by Bluetooth SIG. These default values
// are used to determine whether the fields are ever received in EIR and set
// for a device.
constexpr int8_t kDefaultRssi = -128;
constexpr int8_t kDefaultTxPower = -128;
constexpr uint32_t kDefaultEirClass = 0x1F00;
constexpr uint16_t kDefaultAppearance = 0;
constexpr uint16_t kDefaultManufacturerId = 0xFFFF;

constexpr char kDeviceTypeLe[] = "LE";

// TODO(mcchou): Move GATT assigned number and masks to a separate file.
constexpr uint32_t kAppearanceMask = 0xffc0;

// Updates device alias based on its name or address.
void UpdateDeviceAlias(Device* device) {
  // In BlueZ, if alias is never provided or is set to empty for a device, the
  // value of Alias property is set to the value of Name property if
  // |device.name| is not empty. If |device.name| is also empty, then Alias
  // is set to |device.address| in the following string format.
  // xx-xx-xx-xx-xx-xx
  std::string alias;
  if (!device->internal_alias.empty()) {
    alias = device->internal_alias;
  } else if (!device->name.value().empty()) {
    alias = device->name.value();
  } else {
    alias = device->address;
    std::replace(alias.begin(), alias.end(), ':', '-');
  }
  device->alias.SetValue(std::move(alias));
}

// Canonicalizes UUIDs and wraps them as a vector for exposing or updating
// service UUIDs.
std::vector<std::string> CanonicalizeUuids(const std::set<Uuid>& uuids) {
  std::vector<std::string> result;
  result.reserve(uuids.size());
  for (const auto& uuid : uuids)
    result.push_back(uuid.canonical_value());

  return result;
}

// Canonicalizes UUIDs associated with service data for exposing or updating
// service data.
std::map<std::string, std::vector<uint8_t>> CanonicalizeServiceData(
    const std::map<Uuid, std::vector<uint8_t>>& service_data) {
  std::map<std::string, std::vector<uint8_t>> result;
  for (const auto& data : service_data)
    result.emplace(data.first.canonical_value(), data.second);

  return result;
}

// Converts pair state to string.
std::string ConvertPairStateToString(PairState state) {
  switch (state) {
    case PairState::CANCELED:
      return "canceled";
    case PairState::NOT_PAIRED:
      return "not paired";
    case PairState::FAILED:
      return "failed";
    case PairState::PAIRED:
      return "paired";
    case PairState::STARTED:
      return "started";
    default:
      return "unknown";
  }
}

std::string ConvertConnectStateToString(ConnectState state) {
  switch (state) {
    case ConnectState::CONNECTED:
      return "Connected";
    case ConnectState::DISCONNECTED:
      return "Disconnected";
    case ConnectState::ERROR:
      return "Disconnected with error";
    case ConnectState::DISCONNECTED_BY_US:
      return "Disconnected by us";
    default:
      return "Unknown";
  }
}

// These value are defined at https://www.bluetooth.com/specifications/gatt/
// viewer?attributeXmlFile=org.bluetooth.characteristic.gap.appearance.xml.
// The translated strings come from BlueZ.
std::string ConvertAppearanceToIcon(uint16_t appearance) {
  switch ((appearance & kAppearanceMask) >> 6) {
    case 0x00:
      return "unknown";
    case 0x01:
      return "phone";
    case 0x02:
      return "computer";
    case 0x03:
      return "watch";
    case 0x04:
      return "clock";
    case 0x05:
      return "video-display";
    case 0x06:
      return "remote-control";
    case 0x07:
      return "eye-glasses";
    case 0x08:
      return "tag";
    case 0x09:
      return "key-ring";
    case 0x0a:
      return "multimedia-player";
    case 0x0b:
      return "scanner";
    case 0x0c:
      return "thermometer";
    case 0x0d:
      return "heart-rate-sensor";
    case 0x0e:
      return "blood-pressure";
    case 0x0f:  // HID Generic
      switch (appearance & 0x3f) {
        case 0x01:
          return "input-keyboard";
        case 0x02:
          return "input-mouse";
        case 0x03:
        case 0x04:
          return "input-gaming";
        case 0x05:
          return "input-tablet";
        case 0x08:
          return "scanner";
      }
      break;
    case 0x10:
      return "glucose-meter";
    case 0x11:
      return "running-walking-sensor";
    case 0x12:
      return "cycling";
    case 0x31:
      return "pulse-oximeter";
    case 0x32:
      return "weight-scale";
    case 0x33:
      return "personal-mobility-device";
    case 0x34:
      return "continuous-glucose-monitor";
    case 0x35:
      return "insulin-pump";
    case 0x36:
      return "medication-delivery";
    case 0x51:
      return "outdoor-sports-activity";
    default:
      break;
  }
  return std::string();
}

// Converts the security manager pairing errors to BlueZ's D-Bus errors.
std::string ConvertSmPairErrorToDbusError(PairError error_code) {
  switch (error_code) {
    case PairError::NONE:
      // This comes along with the pairing states other than
      // SM_PAIR_STATE_FAILED.
      return std::string();
    case PairError::ALREADY_PAIRED:
      return bluetooth_device::kErrorAlreadyExists;
    case PairError::IN_PROGRESS:
      return bluetooth_device::kErrorInProgress;
    case PairError::INVALID_PAIR_REQ:
      return bluetooth_device::kErrorInvalidArguments;
    case PairError::L2C_CONN:
      return bluetooth_device::kErrorConnectionAttemptFailed;
    case PairError::NO_SUCH_DEVICE:
      return bluetooth_device::kErrorDoesNotExist;
    case PairError::PASSKEY_FAILED:
    case PairError::OOB_NOT_AVAILABLE:
    case PairError::AUTH_REQ_INFEASIBLE:
    case PairError::CONF_VALUE_MISMATCHED:
    case PairError::PAIRING_NOT_SUPPORTED:
    case PairError::ENCR_KEY_SIZE:
    case PairError::REPEATED_ATTEMPT:
    case PairError::INVALID_PARAM:
    case PairError::UNEXPECTED_SM_CMD:
    case PairError::SEND_SM_CMD:
    case PairError::ENCR_CONN:
    case PairError::UNEXPECTED_L2C_EVT:
      return bluetooth_device::kErrorAuthenticationFailed;
    case PairError::STALLED:
      return bluetooth_device::kErrorAuthenticationTimeout;
    case PairError::MEMORY:
    case PairError::UNKNOWN:
    default:
      return bluetooth_device::kErrorFailed;
  }
}

// Filters out non-ASCII characters from a string by replacing them with spaces.
std::string AsciiString(std::string name) {
  /* Replace all non-ASCII characters with spaces */
  for (auto& ch : name) {
    if (!isascii(ch))
      ch = ' ';
  }

  return name;
}

std::map<uint16_t, std::vector<uint8_t>> MakeManufacturer(
    uint16_t manufacturer_id, std::vector<uint8_t> manufacturer_data) {
  std::map<uint16_t, std::vector<uint8_t>> manufacturer;
  manufacturer.emplace(manufacturer_id, std::move(manufacturer_data));
  return manufacturer;
}

bool IsHid(uint16_t appearance) {
  return true;
  // TODO(mcchou): Check appearance after we memorize the value of properties.
  // This check is preventing the reconnection of HID devices.
  // return ((appearance & kAppearanceMask) >> 6) == 0x0f;
}

}  // namespace

Device::Device() : Device("") {}

Device::Device(const std::string& address)
    : address(address),
      is_random_address(false),
      paired(false),
      connected(false),
      trusted(false),
      blocked(false),
      services_resolved(false),
      tx_power(kDefaultTxPower),
      rssi(kDefaultRssi),
      eir_class(kDefaultEirClass),
      appearance(kDefaultAppearance),
      icon(ConvertAppearanceToIcon(kDefaultAppearance)),
      flags({0}),
      manufacturer(
          MakeManufacturer(kDefaultManufacturerId, std::vector<uint8_t>())),
      identity_address("") {}

DeviceInterfaceHandler::DeviceInterfaceHandler(
    scoped_refptr<dbus::Bus> bus,
    Newblue* newblue,
    ExportedObjectManagerWrapper* exported_object_manager_wrapper)
    : bus_(bus),
      newblue_(newblue),
      exported_object_manager_wrapper_(exported_object_manager_wrapper),
      weak_ptr_factory_(this) {}

bool DeviceInterfaceHandler::Init() {
  // Retrieve the previously saved scan results, and export only the paired
  // devices.
  std::vector<KnownDevice> known_devices = newblue_->GetKnownDevices();
  for (const auto& known_device : known_devices) {
    if (!known_device.is_paired)
      continue;

    Device* device = AddOrGetDiscoveredDevice(known_device.address,
                                              known_device.address_type);
    SetDevicePaired(device, true);
    device->name.SetValue(known_device.name + kNewblueNameSuffix);
    if (!known_device.identity_address.empty())
      device->identity_address.SetValue(known_device.identity_address);

    UpdateDeviceAlias(device);
    ExportOrUpdateDevice(device);
  }

  // Register for pairing state changed events.
  pair_observer_id_ = newblue_->RegisterAsPairObserver(
      base::Bind(&DeviceInterfaceHandler::OnPairStateChanged,
                 weak_ptr_factory_.GetWeakPtr()));
  if (pair_observer_id_ == kInvalidUniqueId)
    return false;

  if (!newblue_->RegisterGattClientConnectCallback(
          base::Bind(&DeviceInterfaceHandler::OnGattClientConnectCallback,
                     weak_ptr_factory_.GetWeakPtr()))) {
    return false;
  }

  return true;
}

void DeviceInterfaceHandler::SetScanManagementCallback(
    ScanManagementCallback callback) {
  CHECK(scan_management_callback_.is_null())
      << "ScanManagementCallback already defined";
  scan_management_callback_ = callback;
}

base::WeakPtr<DeviceInterfaceHandler> DeviceInterfaceHandler::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void DeviceInterfaceHandler::OnDeviceDiscovered(
    bool scanned_by_client,
    const std::string& address,
    uint8_t address_type,
    int8_t rssi,
    uint8_t reply_type,
    const std::vector<uint8_t>& eir) {
  Device* device = FindDevice(address);
  if (device && device->paired.value())
    ConnectInternal(address, /* connect_response */ nullptr,
                    /* connect_by_us */ true);

  if (!scanned_by_client)
    return;

  device = AddOrGetDiscoveredDevice(address, address_type);

  device->rssi.SetValue(rssi);

  UpdateEir(device, eir);
  ExportOrUpdateDevice(device);
}

bool DeviceInterfaceHandler::RemoveDevice(const std::string& address,
                                          std::string* dbus_error) {
  Device* device = FindDevice(address);
  // If the device does not exist, assume the operation has succeeded. This is
  // because the dispatcher may forward RemoveDevice to both BlueZ and NewBlue
  // without knowing who owns the device.
  if (!device)
    return true;

  // For ongoing pairing, reply to D-Bus calls to prevent timeout.
  if (ongoing_pairing_.address == address) {
    ongoing_pairing_.pair_response->ReplyWithError(
        FROM_HERE, brillo::errors::dbus::kDomain,
        bluetooth_device::kErrorAuthenticationCanceled, "Pairing canceled");

    if (ongoing_pairing_.cancel_pair_response) {
      ongoing_pairing_.cancel_pair_response->SendRawResponse(
          ongoing_pairing_.cancel_pair_response->CreateCustomResponse());
    }
    ongoing_pairing_.address.clear();
    ongoing_pairing_.pair_response.reset();
    ongoing_pairing_.cancel_pair_response.reset();
  }
  // Clear internal pairing state.
  newblue_->CancelPair(address, device->is_random_address);

  // For ongoing connection, reply to D-Bus calls to prevent timeout.
  auto connection_session = connection_sessions_.find(address);
  if (connection_session != connection_sessions_.end()) {
    // The ongoing connection would be canceled and the disconnection would be
    // honored.
    if (connection_session->second.disconnect_response ||
        connection_session->second.disconnect_by_us) {
      ConnectReply(address, true, "");
    } else if (connection_session->second.connect_response ||
               connection_session->second.connect_by_us) {
      ConnectReply(address, false, bluetooth_device::kErrorFailed);
    }
  }

  // For device which is either connected or to-be-connected, disconnect it.
  if (base::ContainsKey(connections_, address) ||
      base::ContainsKey(connection_attempts_, address)) {
    DisconnectInternal(address, /* disconnect_response */ nullptr,
                       /* disconnect_by_us */ true);
  }

  dbus::ObjectPath device_path(ConvertDeviceAddressToObjectPath(address));
  exported_object_manager_wrapper_->RemoveExportedInterface(
      device_path, bluetooth_device::kBluetoothDeviceInterface);
  discovered_devices_.erase(address);
  UpdateBackgroundScan();
  return true;
}

void DeviceInterfaceHandler::AddDeviceObserver(
    DeviceInterfaceHandler::DeviceObserver* observer) {
  CHECK(observer);
  observers_.AddObserver(observer);
}

void DeviceInterfaceHandler::RemoveDeviceObserver(
    DeviceInterfaceHandler::DeviceObserver* observer) {
  CHECK(observer);
  observers_.RemoveObserver(observer);
}

std::string DeviceInterfaceHandler::GetAddressByConnectionId(
    gatt_client_conn_t conn_id) {
  if (conn_id == kInvalidGattConnectionId)
    return "";

  for (auto& connection : connections_) {
    if (connection.second.conn_id == conn_id)
      return connection.first;
  }

  return "";
}

Device* DeviceInterfaceHandler::AddOrGetDiscoveredDevice(
    const std::string& address, uint8_t address_type) {
  Device* device = FindDevice(address);
  if (!device) {
    discovered_devices_[address] = std::make_unique<Device>(address);
    device = discovered_devices_[address].get();
  }
  device->is_random_address = (address_type == BT_ADDR_TYPE_LE_RANDOM);
  return device;
}

void DeviceInterfaceHandler::ExportOrUpdateDevice(Device* device) {
  bool is_new_device = false;
  dbus::ObjectPath device_path(
      ConvertDeviceAddressToObjectPath(device->address.c_str()));

  ExportedInterface* device_interface =
      exported_object_manager_wrapper_->GetExportedInterface(
          device_path, bluetooth_device::kBluetoothDeviceInterface);
  // The first time a device of this address is discovered, create the D-Bus
  // object representing that device.
  if (device_interface == nullptr) {
    VLOG(1) << base::StringPrintf(
        "Discovered a new device with %s address %s, rssi %d",
        device->is_random_address ? "random" : "public",
        device->address.c_str(), device->rssi.value());
    is_new_device = true;

    exported_object_manager_wrapper_->AddExportedInterface(
        device_path, bluetooth_device::kBluetoothDeviceInterface,
        base::Bind(
            &ExportedObjectManagerWrapper::SetupStandardPropertyHandlers));

    device_interface = exported_object_manager_wrapper_->GetExportedInterface(
        device_path, bluetooth_device::kBluetoothDeviceInterface);

    AddDeviceMethodHandlers(device_interface);

    device_interface
        ->EnsureExportedPropertyRegistered<dbus::ObjectPath>(
            bluetooth_device::kAdapterProperty)
        ->SetValue(dbus::ObjectPath(kAdapterObjectPath));
  } else {
    VLOG(2) << base::StringPrintf(
        "Discovered device with %s address %s, rssi %d",
        device->is_random_address ? "random" : "public",
        device->address.c_str(), device->rssi.value());
  }

  UpdateDeviceProperties(device_interface, *device, is_new_device);

  // The property updates above have to be done before ExportAndBlock() to make
  // sure that client receives the newly added object complete with its
  // populated properties.
  if (is_new_device)
    device_interface->ExportAndBlock();
}

void DeviceInterfaceHandler::AddDeviceMethodHandlers(
    ExportedInterface* device_interface) {
  CHECK(device_interface);

  device_interface->AddMethodHandlerWithMessage(
      bluetooth_device::kPair,
      base::Bind(&DeviceInterfaceHandler::HandlePair, base::Unretained(this)));
  device_interface->AddMethodHandlerWithMessage(
      bluetooth_device::kCancelPairing,
      base::Bind(&DeviceInterfaceHandler::HandleCancelPairing,
                 base::Unretained(this)));
  device_interface->AddMethodHandlerWithMessage(
      bluetooth_device::kConnect,
      base::Bind(&DeviceInterfaceHandler::HandleConnect,
                 base::Unretained(this)));
  device_interface->AddMethodHandlerWithMessage(
      bluetooth_device::kDisconnect,
      base::Bind(&DeviceInterfaceHandler::HandleDisconnect,
                 base::Unretained(this)));
}

void DeviceInterfaceHandler::HandlePair(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
    dbus::Message* message) {
  CHECK(message);

  std::string device_address =
      ConvertDeviceObjectPathToAddress(message->GetPath().value());

  VLOG(1) << "Handling Pair for device " << device_address;

  if (!ongoing_pairing_.address.empty()) {
    LOG(WARNING) << "Pairing in progress with " << device_address;
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             bluetooth_device::kErrorInProgress,
                             "Pairing in progress");
    return;
  }

  ongoing_pairing_.address = device_address;
  ongoing_pairing_.pair_response = std::move(response);
  ongoing_pairing_.cancel_pair_response.reset();

  Device* device = FindDevice(device_address);
  if (!device || !newblue_->Pair(device->address, device->is_random_address,
                                 DetermineSecurityRequirements(*device))) {
    ongoing_pairing_.pair_response->ReplyWithError(
        FROM_HERE, brillo::errors::dbus::kDomain,
        bluetooth_device::kErrorFailed, "Unknown device");

    // Clear the existing pairing to allow the new pairing request.
    ongoing_pairing_.address.clear();
    ongoing_pairing_.pair_response.reset();
  }
}

void DeviceInterfaceHandler::HandleCancelPairing(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
    dbus::Message* message) {
  CHECK(message);

  std::string device_address =
      ConvertDeviceObjectPathToAddress(message->GetPath().value());

  VLOG(1) << "Handling CancelPairing for device " << device_address;

  if (device_address.empty() || !ongoing_pairing_.pair_response) {
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             bluetooth_device::kErrorDoesNotExist,
                             "No ongoing pairing");
    return;
  }

  ongoing_pairing_.cancel_pair_response = std::move(response);

  Device* device = FindDevice(device_address);
  if (!device ||
      !newblue_->CancelPair(device->address, device->is_random_address)) {
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             bluetooth_device::kErrorFailed, "Unknown device");
    ongoing_pairing_.cancel_pair_response.reset();
  }
}

void DeviceInterfaceHandler::HandleConnect(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
    dbus::Message* message) {
  CHECK(message);

  std::string device_address =
      ConvertDeviceObjectPathToAddress(message->GetPath().value());

  VLOG(1) << "Handling Connect for device " << device_address;

  if (base::ContainsKey(connection_sessions_, device_address)) {
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             bluetooth_device::kErrorInProgress,
                             "Connection/disconnection in progress");
    return;
  }

  ConnectInternal(device_address, std::move(response),
                  /* connect_by_us */ false);
}

void DeviceInterfaceHandler::HandleDisconnect(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
    dbus::Message* message) {
  CHECK(message);

  std::string device_address =
      ConvertDeviceObjectPathToAddress(message->GetPath().value());

  VLOG(1) << "Handling Disconnect for device " << device_address;

  if (base::ContainsKey(connection_sessions_, device_address)) {
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             bluetooth_device::kErrorInProgress,
                             "Connection/disconnection in progress");
    return;
  }

  VLOG(1) << "Disconnect from device " << device_address << " with conn ID "
          << connections_[device_address].conn_id;

  DisconnectInternal(device_address, std::move(response),
                     /* disconnect_by_us */ false);
}

void DeviceInterfaceHandler::ConnectInternal(
    const std::string& device_address,
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> connect_response,
    bool connect_by_us) {
  CHECK((!connect_by_us && connect_response != nullptr) ||
        (connect_by_us && connect_response == nullptr));

  struct ConnectSession session;
  session.connect_by_us = connect_by_us;
  session.connect_response = std::move(connect_response);
  connection_sessions_.emplace(device_address, std::move(session));

  const Device* device = FindDevice(device_address);
  if (!device) {
    LOG(WARNING) << "device " << device_address << " not found";
    ConnectReply(device_address, false, bluetooth_device::kErrorDoesNotExist);
    return;
  }

  struct bt_addr address;
  CHECK(ConvertToBtAddr(device->is_random_address, device_address, &address));

  if (base::ContainsKey(connection_attempts_, device_address)) {
    LOG(WARNING) << "Connection with device " << device_address
                 << " in progress";
    ConnectReply(device_address, false, bluetooth_device::kErrorInProgress);
    return;
  }

  if (connections_.find(device_address) != connections_.end()) {
    LOG(WARNING) << "Connection with device " << device_address
                 << " already exists";
    ConnectReply(device_address, false, bluetooth_device::kErrorAlreadyExists);
    return;
  }

  gatt_client_conn_t conn_id =
      newblue_->GattClientConnect(device->address, device->is_random_address);
  if (conn_id == kInvalidGattConnectionId) {
    LOG(WARNING) << "Failed GATT client connect";
    ConnectReply(device_address, false, bluetooth_device::kErrorFailed);
    return;
  }

  connection_attempts_.emplace(device_address, conn_id);
}

void DeviceInterfaceHandler::DisconnectInternal(
    const std::string& device_address,
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>>
        disconnect_response,
    bool disconnect_by_us) {
  CHECK((!disconnect_by_us && disconnect_response != nullptr) ||
        (disconnect_by_us && disconnect_response == nullptr));

  struct ConnectSession session;
  session.disconnect_by_us = disconnect_by_us;
  session.disconnect_response = std::move(disconnect_response);
  connection_sessions_.emplace(device_address, std::move(session));

  if (!FindDevice(device_address)) {
    LOG(WARNING) << "device " << device_address << " not found";
    ConnectReply(device_address, false, bluetooth_device::kErrorDoesNotExist);
    return;
  }

  gatt_client_conn_t id = kInvalidGattConnectionId;

  auto attempt = connection_attempts_.find(device_address);
  auto connection = connections_.find(device_address);
  bool is_attempt = (attempt != connection_attempts_.end());
  bool is_connected = (connection != connections_.end());

  if (!is_attempt && !is_connected) {
    VLOG(1) << "device " << device_address
            << " doesn't have an active connection nor an ongoing connection";
    ConnectReply(device_address, false, bluetooth_device::kErrorNotConnected);
    return;
  }

  // There can be either connection attempt or existing connection but not both.
  CHECK(!(is_attempt && is_connected));

  id = is_attempt ? attempt->second : connection->second.conn_id;
  if (id == kInvalidGattConnectionId) {
    LOG(WARNING) << "Invalid conn ID to disconnect from device "
                 << device_address;
    ConnectReply(device_address, false, bluetooth_device::kErrorFailed);
    return;
  }

  if (connection->second.hid_id)
    newblue_->libnewblue()->BtleHidDetach(connection->second.hid_id);

  if (newblue_->GattClientDisconnect(id) != GattClientOperationStatus::OK) {
    LOG(ERROR) << "Failed to disconnect from device " << device_address;
    ConnectReply(device_address, false, bluetooth_device::kErrorFailed);
  }
}

void DeviceInterfaceHandler::ConnectReply(const std::string& device_address,
                                          bool success,
                                          const std::string& dbus_error) {
  // In cases where connection/disconnection events were issued by the remote
  // device OR RemoveDevice() was called, there wouldn't be connection session.
  auto iter = connection_sessions_.find(device_address);
  if (iter == connection_sessions_.end()) {
    VLOG(1) << "No connection session with device " << device_address
            << ", ignoring";
    return;
  }

  if (iter->second.connect_by_us || iter->second.disconnect_by_us) {
    connection_sessions_.erase(iter);
    return;
  }

  if (!(iter->second.connect_response || iter->second.disconnect_response)) {
    LOG(WARNING) << "Cannot find ongoing connection session or response for "
                 << "device " << device_address;
    return;
  }

  std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response =
      iter->second.connect_response
          ? std::move(iter->second.connect_response)
          : std::move(iter->second.disconnect_response);

  if (success) {
    response->SendRawResponse(response->CreateCustomResponse());
  } else {
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             dbus_error, "");
  }

  connection_sessions_.erase(iter);
}

void DeviceInterfaceHandler::OnGattClientConnectCallback(
    gatt_client_conn_t conn_id, uint8_t status) {
  Device* dev_to_notify = nullptr;
  Device* dev_to_be_connected = nullptr;
  auto temp_connections = connections_;
  std::map<std::string, gatt_client_conn_t>::iterator iter =
      connection_attempts_.end();

  // See if there is a match in ongoing connection attempts.
  for (auto it = connection_attempts_.begin(); it != connection_attempts_.end();
       ++it) {
    if (it->second == conn_id) {
      dev_to_be_connected = FindDevice(it->first);
      dev_to_notify = dev_to_be_connected;
      iter = it;
      break;
    }
  }

  if (iter != connection_attempts_.end())
    connection_attempts_.erase(iter);

  ConnectState state = static_cast<ConnectState>(status);
  switch (state) {
    case ConnectState::CONNECTED:
      // Skip updating connection state if there is no device to associate with,
      // e.g. the device could be removed if RemoveDevice() was called.
      if (dev_to_be_connected == nullptr) {
        VLOG(1) << "Ignoring connected event with conn ID " << conn_id;
        return;
      }

      VLOG(1) << "Connection with ID " << conn_id << " established for device "
              << dev_to_be_connected->address;

      struct Connection connection;
      connection.conn_id = conn_id;
      connection.hid_id = 0;

      // Once GATT is connected, start the link encryption if we can.
      newblue_->StartEncryption(dev_to_be_connected->address,
                                dev_to_be_connected->is_random_address);

      // Obtain a HID ID for a HID generic device.
      if (IsHid(dev_to_be_connected->appearance.value()))
        connection.hid_id = newblue_->libnewblue()->BtleHidAttach(
            conn_id, dev_to_be_connected->name.value().c_str());

      // Track the new connection and close the attempt.
      connections_.emplace(dev_to_be_connected->address, connection);
      ConnectReply(dev_to_be_connected->address, true, "");

      for (auto& observer : observers_)
        observer.OnGattConnected(dev_to_be_connected->address, conn_id);
      break;
    case ConnectState::ERROR:
      VLOG(1) << "Unexpected GATT connection error";
      FALLTHROUGH;
    case ConnectState::DISCONNECTED:
    case ConnectState::DISCONNECTED_BY_US:
      // Close the connection attempt when there is a match.
      if (dev_to_be_connected) {
        VLOG(1) << "Connection with ID " << conn_id
                << " failed to established for device "
                << dev_to_be_connected->address;

        ConnectReply(dev_to_be_connected->address, false,
                     bluetooth_device::kErrorFailed);
        break;
      }

      VLOG(1) << "GATT disconnection on conn id " << conn_id << " with status "
              << static_cast<int>(status);

      // If there is no match on connection attempt, check if the update is for
      // the existing connection and update the connection state accordingly.
      for (auto& connection : temp_connections) {
        if (connection.second.conn_id != conn_id)
          continue;

        connections_.erase(connection.first);
        ConnectReply(connection.first, true, "");
        dev_to_notify = FindDevice(connection.first);

        for (auto& observer : observers_)
          observer.OnGattDisconnected(connection.first, conn_id);
        break;
      }
      break;
    default:
      VLOG(1) << "Unexpected GATT connection result, conn " << conn_id
              << " status = " << status;
      return;
  }

  if (dev_to_notify) {
    VLOG(1) << "Connection state changed to "
            << ConvertConnectStateToString(state) << " for device "
            << dev_to_notify->address;

    SetDeviceConnected(dev_to_notify, state == ConnectState::CONNECTED);
    ExportOrUpdateDevice(dev_to_notify);
  }
}

void DeviceInterfaceHandler::SetDeviceConnected(Device* device,
                                                bool is_connected) {
  CHECK(device != nullptr);

  device->connected.SetValue(is_connected);
  UpdateBackgroundScan();
}

void DeviceInterfaceHandler::SetDevicePaired(Device* device, bool is_paired) {
  CHECK(device != nullptr);

  device->paired.SetValue(is_paired);
  UpdateBackgroundScan();
}

void DeviceInterfaceHandler::UpdateBackgroundScan() {
  bool needs_background_scan = false;
  for (const auto& kv : discovered_devices_) {
    Device* dev = kv.second.get();
    if (dev->paired.value() && !dev->connected.value()) {
      needs_background_scan = true;
    }
  }
  scan_management_callback_.Run(needs_background_scan);
}

Device* DeviceInterfaceHandler::FindDevice(const std::string& device_address) {
  auto iter = discovered_devices_.find(device_address);
  return iter != discovered_devices_.end() ? iter->second.get() : nullptr;
}

void DeviceInterfaceHandler::UpdateDeviceProperties(
    ExportedInterface* interface, const Device& device, bool is_new_device) {
  CHECK(interface);

  // TODO(mcchou): Properties Modalias and MTU is not yet sorted out.

  // The following properties are exported when |is_new_device| is true or when
  // they are updated.
  if (is_new_device) {
    // Expose immutable and non-optional properties for the new device.
    // The address property might be overwritten later by identity address.
    interface->EnsureExportedPropertyRegistered<std::string>(
        bluetooth_device::kAddressProperty)->SetValue(device.address);
    interface->EnsureExportedPropertyRegistered<std::string>(
        bluetooth_device::kTypeProperty)->SetValue(kDeviceTypeLe);
    interface->EnsureExportedPropertyRegistered<bool>(
        bluetooth_device::kLegacyPairingProperty)->SetValue(false);
  }

  ExportDBusProperty(interface, bluetooth_device::kPairedProperty,
                     device.paired, is_new_device);
  ExportDBusProperty(interface, bluetooth_device::kConnectedProperty,
                     device.connected, is_new_device);
  ExportDBusProperty(interface, bluetooth_device::kTrustedProperty,
                     device.trusted, is_new_device);
  ExportDBusProperty(interface, bluetooth_device::kBlockedProperty,
                     device.blocked, is_new_device);
  ExportDBusProperty(interface, bluetooth_device::kAliasProperty, device.alias,
                     is_new_device);
  ExportDBusProperty(interface, bluetooth_device::kServicesResolvedProperty,
                     device.services_resolved, is_new_device);
  ExportDBusProperty(interface, bluetooth_device::kAdvertisingDataFlagsProperty,
                     device.flags, is_new_device);
  // Although RSSI is an optional device property in BlueZ, it is always
  // provided by libnewblue, thus it is exposed by default.
  ExportDBusProperty(interface, bluetooth_device::kRSSIProperty, device.rssi,
                     is_new_device);

  // The following properties are exported only when they are updated.
  ExportDBusProperty(interface, bluetooth_device::kUUIDsProperty,
                     device.service_uuids, &CanonicalizeUuids, false);
  ExportDBusProperty(interface, bluetooth_device::kServiceDataProperty,
                     device.service_data, &CanonicalizeServiceData, false);
  ExportDBusProperty(interface, bluetooth_device::kNameProperty, device.name,
                     false);
  ExportDBusProperty(interface, bluetooth_device::kTxPowerProperty,
                     device.tx_power, false);
  ExportDBusProperty(interface, bluetooth_device::kClassProperty,
                     device.eir_class, false);
  ExportDBusProperty(interface, bluetooth_device::kAppearanceProperty,
                     device.appearance, false);
  ExportDBusProperty(interface, bluetooth_device::kIconProperty, device.icon,
                     false);
  ExportDBusProperty(interface, bluetooth_device::kManufacturerDataProperty,
                     device.manufacturer, false);
  // Overwrite the address property with identity address.
  ExportDBusProperty(interface, bluetooth_device::kAddressProperty,
                     device.identity_address, false);
}

void DeviceInterfaceHandler::UpdateEir(Device* device,
                                       const std::vector<uint8_t>& eir) {
  CHECK(device);

  uint8_t pos = 0;
  std::set<Uuid> service_uuids;
  std::map<Uuid, std::vector<uint8_t>> service_data;

  while (pos + 1 < eir.size()) {
    uint8_t field_len = eir[pos];

    // End of EIR
    if (field_len == 0)
      break;

    // Corrupt EIR data
    if (pos + field_len >= eir.size())
      break;

    EirType eir_type = static_cast<EirType>(eir[pos + 1]);
    const uint8_t* data = &eir[pos + 2];

    // A field consists of 1 byte field type + data:
    // | 1-byte field_len | 1-byte type | (field length - 1) bytes data ... |
    uint8_t data_len = field_len - 1;

    switch (eir_type) {
      case EirType::FLAGS:
        // No default value should be set for flags according to Supplement to
        // the Bluetooth Core Specification. Flags field can be 0 or more octets
        // long. If the length is 1, then flags[0] is octet[0]. Store only
        // octet[0] for now due to lack of definition of the following octets
        // in Supplement to the Bluetooth Core Specification.
        if (data_len > 0)
          device->flags.SetValue(std::vector<uint8_t>(data, data + 1));
        // If |data_len| is 0, we avoid setting zero-length advertising flags as
        // this currently causes Chrome to crash.
        // TODO(crbug.com/876908): Fix Chrome to not crash with zero-length
        // advertising flags.
        break;

      // If there are more than one instance of either COMPLETE or INCOMPLETE
      // type of a UUID size, the later one(s) will be cached as well.
      case EirType::UUID16_INCOMPLETE:
      case EirType::UUID16_COMPLETE:
        UpdateServiceUuids(&service_uuids, kUuid16Size, data, data_len);
        break;
      case EirType::UUID32_INCOMPLETE:
      case EirType::UUID32_COMPLETE:
        UpdateServiceUuids(&service_uuids, kUuid32Size, data, data_len);
        break;
      case EirType::UUID128_INCOMPLETE:
      case EirType::UUID128_COMPLETE:
        UpdateServiceUuids(&service_uuids, kUuid128Size, data, data_len);
        break;

      // Name
      case EirType::NAME_SHORT:
      case EirType::NAME_COMPLETE: {
        char c_name[HCI_DEV_NAME_LEN + 1];

        // Some device has trailing '\0' at the end of the name data.
        // So make sure we only take the characters before '\0' and limited
        // to the max length allowed by Bluetooth spec (HCI_DEV_NAME_LEN).
        uint8_t name_len =
            std::min(data_len, static_cast<uint8_t>(HCI_DEV_NAME_LEN));
        strncpy(c_name, reinterpret_cast<const char*>(data), name_len);
        c_name[name_len] = '\0';
        device->name.SetValue(AsciiString(c_name) + kNewblueNameSuffix);
      } break;

      case EirType::TX_POWER:
        if (data_len == 1)
          device->tx_power.SetValue(static_cast<int8_t>(*data));
        break;
      case EirType::CLASS_OF_DEV:
        // 24-bit little endian data
        if (data_len == 3)
          device->eir_class.SetValue(GetNumFromLE24(data));
        break;

      // If the UUID already exists, the service data will be updated.
      case EirType::SVC_DATA16:
        UpdateServiceData(&service_data, kUuid16Size, data, data_len);
        break;
      case EirType::SVC_DATA32:
        UpdateServiceData(&service_data, kUuid32Size, data, data_len);
        break;
      case EirType::SVC_DATA128:
        UpdateServiceData(&service_data, kUuid128Size, data, data_len);
        break;

      case EirType::GAP_APPEARANCE:
        // 16-bit little endian data
        if (data_len == 2) {
          uint16_t appearance = GetNumFromLE16(data);
          device->appearance.SetValue(appearance);
          device->icon.SetValue(ConvertAppearanceToIcon(appearance));
        }
        break;
      case EirType::MANUFACTURER_DATA:
        if (data_len >= 2) {
          // The order of manufacturer data is not specified explicitly in
          // Supplement to the Bluetooth Core Specification, so the original
          // order used in BlueZ is adopted.
          device->manufacturer.SetValue(MakeManufacturer(
              GetNumFromLE16(data),
              std::vector<uint8_t>(data + 2,
                                   data + std::max<uint8_t>(data_len, 2))));
        }
        break;
      default:
        // Do nothing for unhandled EIR type.
        break;
    }

    pos += field_len + 1;
  }

  UpdateDeviceAlias(device);

  // This is different from BlueZ where it memorizes all service UUIDs and
  // service data ever received for the same device. If there is no service
  // UUIDs/service data being presented, service UUIDs/servicedata will not
  // be updated.
  if (!service_uuids.empty())
    device->service_uuids.SetValue(std::move(service_uuids));
  if (!service_data.empty())
    device->service_data.SetValue(std::move(service_data));
}

void DeviceInterfaceHandler::UpdateServiceUuids(std::set<Uuid>* service_uuids,
                                                uint8_t uuid_size,
                                                const uint8_t* data,
                                                uint8_t data_len) {
  CHECK(service_uuids && data);

  if (!data_len || data_len % uuid_size != 0) {
    LOG(WARNING) << "Failed to parse EIR service UUIDs";
    return;
  }

  // Service UUIDs are presented in little-endian order.
  for (uint8_t i = 0; i < data_len; i += uuid_size) {
    Uuid uuid(GetBytesFromLE(data + i, uuid_size));
    CHECK(uuid.format() != UuidFormat::UUID_INVALID);
    service_uuids->insert(uuid);
  }
}

void DeviceInterfaceHandler::UpdateServiceData(
    std::map<Uuid, std::vector<uint8_t>>* service_data,
    uint8_t uuid_size,
    const uint8_t* data,
    uint8_t data_len) {
  CHECK(service_data && data);

  if (!data_len || data_len <= uuid_size) {
    LOG(WARNING) << "Failed to parse EIR service data";
    return;
  }

  // A service UUID and its data are presented in little-endian order where the
  // format is {<bytes of service UUID>, <bytes of service data>}. For instance,
  // the service data associated with the battery service can be
  // {0x0F, 0x18, 0x22, 0x11}
  // where {0x18 0x0F} is the UUID and {0x11, 0x22} is the data.
  Uuid uuid(GetBytesFromLE(data, uuid_size));
  CHECK_NE(UuidFormat::UUID_INVALID, uuid.format());
  service_data->emplace(uuid,
                        GetBytesFromLE(data + uuid_size, data_len - uuid_size));
}

void DeviceInterfaceHandler::ClearPropertiesUpdated(Device* device) {
  device->paired.ClearUpdated();
  device->connected.ClearUpdated();
  device->trusted.ClearUpdated();
  device->blocked.ClearUpdated();
  device->services_resolved.ClearUpdated();
  device->alias.ClearUpdated();
  device->name.ClearUpdated();
  device->tx_power.ClearUpdated();
  device->rssi.ClearUpdated();
  device->eir_class.ClearUpdated();
  device->appearance.ClearUpdated();
  device->icon.ClearUpdated();
  device->flags.ClearUpdated();
  device->service_uuids.ClearUpdated();
  device->service_data.ClearUpdated();
  device->manufacturer.ClearUpdated();
  device->identity_address.ClearUpdated();
}

struct smPairSecurityRequirements
DeviceInterfaceHandler::DetermineSecurityRequirements(const Device& device) {
  struct smPairSecurityRequirements security_requirement = {.bond = false,
                                                            .mitm = false};
  bool security_determined = false;

  // TODO(mcchou): Determine the security requirement for different type of
  // devices based on appearance.
  // These value are defined at https://www.bluetooth.com/specifications/gatt/
  // viewer?attributeXmlFile=org.bluetooth.characteristic.gap.appearance.xml.
  // The translated strings come from BlueZ.
  switch ((device.appearance.value() & kAppearanceMask) >> 6) {
    case 0x01:  // phone
    case 0x02:  // computer
      security_requirement.bond = true;
      security_requirement.mitm = true;
      security_determined = true;
      break;
    case 0x03:  // watch
      security_requirement.bond = true;
      security_determined = true;
      break;
    case 0x0f:  // HID Generic
      switch (device.appearance.value() & 0x3f) {
        case 0x01:  // keyboard
        case 0x05:  // tablet
          security_requirement.bond = true;
          security_requirement.mitm = true;
          security_determined = true;
          break;
        case 0x02:  // mouse
        case 0x03:  // joystick
        case 0x04:  // gamepad
          security_requirement.bond = true;
          security_determined = true;
          break;
        case 0x08:  // barcode-scanner
        default:
          break;
      }
      break;
    case 0x00:  // unknown
    case 0x04:  // clock
    case 0x05:  // video-display
    case 0x06:  // remote-control
    case 0x07:  // eye-glasses
    case 0x08:  // tag
    case 0x09:  // key-ring
    case 0x0a:  // multimedia-player
    case 0x0b:  // barcode-scanner
    case 0x0c:  // thermometer
    case 0x0d:  // heart-rate-sensor
    case 0x0e:  // blood-pressure
    case 0x10:  // glucose-meter
    case 0x11:  // running-walking-sensor
    case 0x12:  // cycling
    case 0x31:  // pulse-oximeter
    case 0x32:  // weight-scale
    case 0x33:  // personal-mobility-device
    case 0x34:  // continuous-glucose-monitor
    case 0x35:  // insulin-pump
    case 0x36:  // medication-delivery
    case 0x51:  // outdoor-sports-activity
    default:
      break;
  }

  if (!security_determined) {
    security_requirement.bond = true;
    LOG(WARNING) << base::StringPrintf(
        "The default security level (bond:%s MITM:%s) will be "
        "used in pairing with device with appearance 0x%4X",
        security_requirement.bond ? "true" : "false",
        security_requirement.mitm ? "true" : "false",
        device.appearance.value());
  }

  return security_requirement;
}

void DeviceInterfaceHandler::OnPairStateChanged(
    const std::string& address,
    PairState pair_state,
    PairError pair_error,
    const std::string& identity_address) {
  // The device D-Bus object may have already been unexported, e.g. pairing
  // information is removed during the device removal.
  Device* device = FindDevice(address);
  if (!device)
    return;

  VLOG(1) << "Pairing state changed to " << ConvertPairStateToString(pair_state)
          << " for device " << device->address;

  std::string dbus_error;

  switch (pair_state) {
    case PairState::CANCELED:
      dbus_error = bluetooth_device::kErrorAuthenticationCanceled;
      FALLTHROUGH;
    case PairState::NOT_PAIRED:
      SetDevicePaired(device, false);
      break;
    case PairState::FAILED:
      // If a device is previously paired, security manager will throw a
      // SM_PAIR_ERR_ALREADY_PAIRED error which should not set the pairing state
      // to false.
      SetDevicePaired(device, pair_error == PairError::ALREADY_PAIRED);
      dbus_error = ConvertSmPairErrorToDbusError(pair_error);
      break;
    case PairState::PAIRED:
      if (!identity_address.empty())
        device->identity_address.SetValue(identity_address);
      SetDevicePaired(device, true);
      break;
    case PairState::STARTED:
    default:
      // Do not change paired property.
      break;
  }

  if (device->address != ongoing_pairing_.address) {
    ExportOrUpdateDevice(device);
    return;
  }

  CHECK(!ongoing_pairing_.address.empty());
  CHECK(ongoing_pairing_.pair_response);

  // Reply to the Pair/CancelPairing method calls according to the pairing
  // state.
  switch (pair_state) {
    case PairState::NOT_PAIRED:
      // Falling back to this state indicate an unknown error, so the cancel
      // pairing request should fail as well.
      ongoing_pairing_.pair_response->ReplyWithError(
          FROM_HERE, brillo::errors::dbus::kDomain,
          bluetooth_device::kErrorFailed, "Unknown");

      if (ongoing_pairing_.cancel_pair_response) {
        ongoing_pairing_.cancel_pair_response->ReplyWithError(
            FROM_HERE, brillo::errors::dbus::kDomain,
            bluetooth_device::kErrorDoesNotExist, "No ongoing pairing");
      }
      break;
    case PairState::STARTED:
      // For the start of the pairing, we wait for the result.
      CHECK(!ongoing_pairing_.cancel_pair_response);
      break;
    case PairState::PAIRED:
      ongoing_pairing_.pair_response->SendRawResponse(
          ongoing_pairing_.pair_response->CreateCustomResponse());

      if (ongoing_pairing_.cancel_pair_response) {
        ongoing_pairing_.cancel_pair_response->ReplyWithError(
            FROM_HERE, brillo::errors::dbus::kDomain,
            bluetooth_device::kErrorFailed, "Unknown - pairing done");
      }
      break;
    case PairState::CANCELED:
      ongoing_pairing_.pair_response->ReplyWithError(
          FROM_HERE, brillo::errors::dbus::kDomain, dbus_error,
          "Pairing canceled");

      if (ongoing_pairing_.cancel_pair_response) {
        ongoing_pairing_.cancel_pair_response->SendRawResponse(
            ongoing_pairing_.cancel_pair_response->CreateCustomResponse());
      }
      break;
    case PairState::FAILED:
      ongoing_pairing_.pair_response->ReplyWithError(
          FROM_HERE, brillo::errors::dbus::kDomain, dbus_error,
          "Pairing failed");

      if (ongoing_pairing_.cancel_pair_response) {
        ongoing_pairing_.cancel_pair_response->ReplyWithError(
            FROM_HERE, brillo::errors::dbus::kDomain,
            bluetooth_device::kErrorDoesNotExist, "No ongoing pairing");
      }
      break;
    default:
      LOG(WARNING) << "Unexpected pairing state change";

      ongoing_pairing_.pair_response->ReplyWithError(
          FROM_HERE, brillo::errors::dbus::kDomain,
          bluetooth_device::kErrorFailed, "Unknown");

      if (ongoing_pairing_.cancel_pair_response) {
        ongoing_pairing_.cancel_pair_response->ReplyWithError(
            FROM_HERE, brillo::errors::dbus::kDomain,
            bluetooth_device::kErrorFailed, "Unknown");
      }
      break;
  }

  if (pair_state != PairState::STARTED) {
    ongoing_pairing_.address.clear();
    ongoing_pairing_.pair_response.reset();
  }

  ongoing_pairing_.cancel_pair_response.reset();

  ExportOrUpdateDevice(device);
}

}  // namespace bluetooth
