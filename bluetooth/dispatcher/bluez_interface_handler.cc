// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bluetooth/dispatcher/bluez_interface_handler.h"

#include <stdint.h>
#include <map>
#include <string>
#include <vector>

#include <dbus/object_path.h>

namespace bluetooth {

BluezAdapterInterfaceHandler::BluezAdapterInterfaceHandler() {
  AddPropertyFactory<std::string>(bluetooth_adapter::kAddressProperty);
  AddPropertyFactory<std::string>(bluetooth_adapter::kNameProperty,
                                  MergingRule::CONCATENATION);
  AddPropertyFactory<std::string>(bluetooth_adapter::kAliasProperty);
  AddPropertyFactory<uint32_t>(bluetooth_adapter::kClassProperty);
  AddPropertyFactory<bool>(bluetooth_adapter::kPoweredProperty,
                           MergingRule::AND);
  AddPropertyFactory<bool>(bluetooth_adapter::kDiscoverableProperty);
  AddPropertyFactory<bool>(bluetooth_adapter::kPairableProperty);
  AddPropertyFactory<uint32_t>(bluetooth_adapter::kPairableTimeoutProperty);
  AddPropertyFactory<uint32_t>(bluetooth_adapter::kDiscoverableTimeoutProperty);
  AddPropertyFactory<bool>(bluetooth_adapter::kDiscoveringProperty,
                           MergingRule::OR);
  AddPropertyFactory<std::vector<std::string>>(
      bluetooth_adapter::kUUIDsProperty, MergingRule::UNION);
  AddPropertyFactory<std::string>(bluetooth_adapter::kModaliasProperty);

  AddMethodForwarding(bluetooth_adapter::kStartDiscovery,
                      ForwardingRule::FORWARD_ALL);
  AddMethodForwarding(bluetooth_adapter::kSetDiscoveryFilter);
  AddMethodForwarding(bluetooth_adapter::kStopDiscovery,
                      ForwardingRule::FORWARD_ALL);
  AddMethodForwarding(bluetooth_adapter::kPauseDiscovery);
  AddMethodForwarding(bluetooth_adapter::kUnpauseDiscovery);
  AddMethodForwarding(bluetooth_adapter::kRemoveDevice,
                      ForwardingRule::FORWARD_ALL);
  AddMethodForwarding(bluetooth_adapter::kCreateServiceRecord);
  AddMethodForwarding(bluetooth_adapter::kRemoveServiceRecord);
  AddMethodForwarding(bluetooth_adapter::kHandleSuspendImminent,
                      ForwardingRule::FORWARD_ALL);
  AddMethodForwarding(bluetooth_adapter::kHandleSuspendDone,
                      ForwardingRule::FORWARD_ALL);
  AddMethodForwarding(bluetooth_adapter::kGetSupportedCapabilities);
  AddMethodForwarding(bluetooth_adapter::kSetLongTermKeys,
                      ForwardingRule::FORWARD_ALL);
}

BluezDeviceInterfaceHandler::BluezDeviceInterfaceHandler() {
  AddPropertyFactory<std::string>(bluetooth_device::kAddressProperty);
  AddPropertyFactory<std::string>(bluetooth_device::kNameProperty);
  AddPropertyFactory<std::string>(bluetooth_device::kIconProperty);
  AddPropertyFactory<uint32_t>(bluetooth_device::kClassProperty);
  AddPropertyFactory<std::string>(bluetooth_device::kTypeProperty);
  AddPropertyFactory<uint16_t>(bluetooth_device::kAppearanceProperty);
  AddPropertyFactory<std::vector<std::string>>(
      bluetooth_device::kUUIDsProperty);
  AddPropertyFactory<int16_t>(bluetooth_device::kTxPowerProperty);
  AddPropertyFactory<bool>(bluetooth_device::kPairedProperty);
  AddPropertyFactory<bool>(bluetooth_device::kConnectedProperty);
  AddPropertyFactory<bool>(bluetooth_device::kTrustedProperty);
  AddPropertyFactory<bool>(bluetooth_device::kBlockedProperty);
  AddPropertyFactory<std::string>(bluetooth_device::kAliasProperty);
  AddPropertyFactory<dbus::ObjectPath>(bluetooth_device::kAdapterProperty);
  AddPropertyFactory<bool>(bluetooth_device::kLegacyPairingProperty);
  AddPropertyFactory<std::string>(bluetooth_device::kModaliasProperty);
  AddPropertyFactory<int16_t>(bluetooth_device::kRSSIProperty);
  AddPropertyFactory<std::map<uint16_t, std::vector<uint8_t>>>(
      bluetooth_device::kManufacturerDataProperty);
  AddPropertyFactory<std::map<std::string, std::vector<uint8_t>>>(
      bluetooth_device::kServiceDataProperty);
  AddPropertyFactory<bool>(bluetooth_device::kServicesResolvedProperty);
  AddPropertyFactory<std::vector<uint8_t>>(
      bluetooth_device::kAdvertisingDataFlagsProperty);
  AddPropertyFactory<uint16_t>(bluetooth_device::kMTUProperty);
  AddPropertyFactory<std::vector<uint8_t>>(bluetooth_device::kEIRProperty);

  AddMethodForwarding(bluetooth_device::kConnect);
  AddMethodForwarding(bluetooth_device::kDisconnect);
  AddMethodForwarding(bluetooth_device::kConnectProfile);
  AddMethodForwarding(bluetooth_device::kDisconnectProfile);
  AddMethodForwarding(bluetooth_device::kPair);
  AddMethodForwarding(bluetooth_device::kCancelPairing);
  AddMethodForwarding(bluetooth_device::kGetServiceRecords);
  AddMethodForwarding(bluetooth_device::kExecuteWrite);
}

BluezGattCharacteristicInterfaceHandler::
    BluezGattCharacteristicInterfaceHandler() {
  AddPropertyFactory<std::string>(bluetooth_gatt_characteristic::kUUIDProperty);
  AddPropertyFactory<dbus::ObjectPath>(
      bluetooth_gatt_characteristic::kServiceProperty);
  AddPropertyFactory<std::vector<uint8_t>>(
      bluetooth_gatt_characteristic::kValueProperty);
  AddPropertyFactory<bool>(bluetooth_gatt_characteristic::kNotifyingProperty);
  AddPropertyFactory<std::vector<std::string>>(
      bluetooth_gatt_characteristic::kFlagsProperty);

  AddMethodForwarding(bluetooth_gatt_characteristic::kReadValue);
  AddMethodForwarding(bluetooth_gatt_characteristic::kWriteValue);
  AddMethodForwarding(bluetooth_gatt_characteristic::kStartNotify);
  AddMethodForwarding(bluetooth_gatt_characteristic::kStopNotify);
  AddMethodForwarding(bluetooth_gatt_characteristic::kPrepareWriteValue);
}

BluezGattManagerInterfaceHandler::BluezGattManagerInterfaceHandler() {
  AddMethodForwarding(bluetooth_gatt_manager::kRegisterApplication);
  AddMethodForwarding(bluetooth_gatt_manager::kUnregisterApplication);
}

BluezInputInterfaceHandler::BluezInputInterfaceHandler() {
  AddPropertyFactory<std::string>(bluetooth_input::kReconnectModeProperty);
}

BluezMediaInterfaceHandler::BluezMediaInterfaceHandler() {
  AddMethodForwarding(bluetooth_media::kRegisterEndpoint);
  AddMethodForwarding(bluetooth_media::kUnregisterEndpoint);
  AddMethodForwarding(bluetooth_media::kRegisterPlayer);
  AddMethodForwarding(bluetooth_media::kUnregisterPlayer);
}

BluezGattServiceInterfaceHandler::BluezGattServiceInterfaceHandler() {
  AddPropertyFactory<std::string>(bluetooth_gatt_service::kUUIDProperty);
  AddPropertyFactory<dbus::ObjectPath>(bluetooth_gatt_service::kDeviceProperty);
  AddPropertyFactory<bool>(bluetooth_gatt_service::kPrimaryProperty);
  AddPropertyFactory<std::vector<dbus::ObjectPath>>(
      bluetooth_gatt_service::kIncludesProperty);
}

BluezLeAdvertisingManagerInterfaceHandler::
    BluezLeAdvertisingManagerInterfaceHandler() {
  AddMethodForwarding(bluetooth_advertising_manager::kRegisterAdvertisement);
  AddMethodForwarding(bluetooth_advertising_manager::kUnregisterAdvertisement);
  AddMethodForwarding(bluetooth_advertising_manager::kSetAdvertisingIntervals);
  AddMethodForwarding(bluetooth_advertising_manager::kResetAdvertising);
}

BluezGattDescriptorInterfaceHandler::BluezGattDescriptorInterfaceHandler() {
  AddPropertyFactory<std::string>(bluetooth_gatt_descriptor::kUUIDProperty);
  AddPropertyFactory<dbus::ObjectPath>(
      bluetooth_gatt_descriptor::kCharacteristicProperty);
  AddPropertyFactory<std::vector<uint8_t>>(
      bluetooth_gatt_descriptor::kValueProperty);

  AddMethodForwarding(bluetooth_gatt_descriptor::kReadValue);
  AddMethodForwarding(bluetooth_gatt_descriptor::kWriteValue);
}

BluezMediaTransportInterfaceHandler::BluezMediaTransportInterfaceHandler() {
  AddPropertyFactory<dbus::ObjectPath>(
      bluetooth_media_transport::kDeviceProperty);
  AddPropertyFactory<std::string>(bluetooth_media_transport::kUUIDProperty);
  AddPropertyFactory<uint8_t>(bluetooth_media_transport::kCodecProperty);
  AddPropertyFactory<std::vector<uint8_t>>(
      bluetooth_media_transport::kConfigurationProperty);
  AddPropertyFactory<std::string>(bluetooth_media_transport::kStateProperty);
  AddPropertyFactory<uint16_t>(bluetooth_media_transport::kDelayProperty);
  AddPropertyFactory<uint16_t>(bluetooth_media_transport::kVolumeProperty);

  AddMethodForwarding(bluetooth_media_transport::kAcquire);
  AddMethodForwarding(bluetooth_media_transport::kTryAcquire);
  AddMethodForwarding(bluetooth_media_transport::kRelease);
}

BluezAgentManagerInterfaceHandler::BluezAgentManagerInterfaceHandler() {
  AddMethodForwarding(bluetooth_agent_manager::kRegisterAgent,
                      ForwardingRule::FORWARD_ALL);
  AddMethodForwarding(bluetooth_agent_manager::kUnregisterAgent,
                      ForwardingRule::FORWARD_ALL);
  AddMethodForwarding(bluetooth_agent_manager::kRequestDefaultAgent,
                      ForwardingRule::FORWARD_ALL);

  SetObjectExportRule(ObjectExportRule::ALL_SERVICES);
}

BluezProfileManagerInterfaceHandler::BluezProfileManagerInterfaceHandler() {
  AddMethodForwarding(bluetooth_profile_manager::kRegisterProfile);
  AddMethodForwarding(bluetooth_profile_manager::kUnregisterProfile);
}

ChromiumBluetoothDeviceInterfaceHandler::
    ChromiumBluetoothDeviceInterfaceHandler() {
  AddPropertyFactory<bool>(bluetooth_plugin::kSupportsLEServices);
  AddPropertyFactory<bool>(bluetooth_plugin::kSupportsConnInfo);

  AddMethodForwarding(bluetooth_plugin_device::kGetConnInfo);
  AddMethodForwarding(bluetooth_plugin_device::kSetLEConnectionParameters);
}

}  // namespace bluetooth
