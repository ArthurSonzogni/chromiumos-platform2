// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wimax_manager/device_dbus_adaptor.h"

#include <vector>

#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/values.h>
#include <chromeos/dbus/service_constants.h>

#include "wimax_manager/device.h"
#include "wimax_manager/network.h"
#include "wimax_manager/network_dbus_adaptor.h"

using org::chromium::WiMaxManager::Device_adaptor;
using std::map;
using std::string;
using std::vector;

namespace wimax_manager {

namespace {

bool ConvertDBusDictionaryToDictionaryValue(
    const map<string, DBus::Variant> &dbus_dictionary,
    base::DictionaryValue *dictionary_value) {
  CHECK(dictionary_value);

  dictionary_value->Clear();
  for (const auto &key_value : dbus_dictionary) {
    const string &key = key_value.first;
    const DBus::Signature &value_signature = key_value.second.signature();
    DBus::MessageIter value_reader = key_value.second.reader();
    if (value_signature == DBus::type<string>::sig()) {
      dictionary_value->SetString(key, value_reader.get_string());
    } else if (value_signature == DBus::type<bool>::sig()) {
      dictionary_value->SetBoolean(key, value_reader.get_bool());
    } else if (value_signature == DBus::type<int32_t>::sig()) {
      dictionary_value->SetInteger(key, value_reader.get_int32());
    } else if (value_signature == DBus::type<uint32_t>::sig()) {
      dictionary_value->SetInteger(key, value_reader.get_uint32());
    } else if (value_signature == DBus::type<int16_t>::sig()) {
      dictionary_value->SetInteger(key, value_reader.get_int16());
    } else if (value_signature == DBus::type<uint16_t>::sig()) {
      dictionary_value->SetInteger(key, value_reader.get_uint16());
    } else if (value_signature == DBus::type<uint8_t>::sig()) {
      dictionary_value->SetInteger(key, value_reader.get_byte());
    } else if (value_signature == DBus::type<double>::sig()) {
      dictionary_value->SetDouble(key, value_reader.get_double());
    } else {
      LOG(ERROR) << "DBus type '" << value_signature
                 << "' is not supported in dictionary conversion.";
      return false;
    }
  }
  return true;
}

}  // namespace

DeviceDBusAdaptor::DeviceDBusAdaptor(DBus::Connection *connection,
                                     Device *device)
    : DBusAdaptor(connection, GetDeviceObjectPath(*device)),
      device_(device) {
  Index = device->index();
  Name = device->name();
  MACAddress = device->mac_address().GetHexString();
  BaseStationId = device->base_station_id().GetHexString();
  Frequency = device->frequency();
  CINRs = device->cinr();
  RSSIs = device->rssi();
  Networks = vector<DBus::Path>();
  Status = device->status();
  NetworkScanInterval = device->network_scan_interval();
  StatusUpdateInterval = device->status_update_interval();
}

DeviceDBusAdaptor::~DeviceDBusAdaptor() {
}

// static
string DeviceDBusAdaptor::GetDeviceObjectPath(const Device &device) {
  return base::StringPrintf("%s%s", kDeviceObjectPathPrefix,
                            device.name().c_str());
}

void DeviceDBusAdaptor::Enable(DBus::Error &error) {  // NOLINT
  if (!device_->Enable()) {
    SetError(&error, "Failed to enable device " + device_->name());
  }
}

void DeviceDBusAdaptor::Disable(DBus::Error &error) {  // NOLINT
  if (!device_->Disable()) {
    SetError(&error, "Failed to disable device " + device_->name());
  }
}

void DeviceDBusAdaptor::ScanNetworks(DBus::Error &error) {  // NOLINT
  if (!device_->ScanNetworks()) {
    SetError(&error, "Failed to scan networks from device " + device_->name());
  }
}

void DeviceDBusAdaptor::Connect(const DBus::Path &network_object_path,
                                const map<string, DBus::Variant> &parameters,
                                DBus::Error &error) {  // NOLINT
  NetworkRefPtr network = FindNetworkByDBusObjectPath(network_object_path);
  if (!network) {
    SetError(&error, "Could not find network ' " + network_object_path + "'.");
    return;
  }
  base::DictionaryValue parameters_dictionary;
  if (!ConvertDBusDictionaryToDictionaryValue(
      parameters, &parameters_dictionary)) {
    SetError(&error, "Invalid connect parameters value.");
    return;
  }
  if (!device_->Connect(*network, parameters_dictionary)) {
    SetError(&error,
             "Failed to connect device " + device_->name() + " to network");
  }
}

void DeviceDBusAdaptor::Disconnect(DBus::Error &error) {  // NOLINT
  if (!device_->Disconnect()) {
    SetError(&error,
             "Failed to disconnect device " + device_->name() +
             " from network");
  }
}

void DeviceDBusAdaptor::UpdateMACAddress() {
  MACAddress = device_->mac_address().GetHexString();
}

void DeviceDBusAdaptor::UpdateNetworks() {
  vector<DBus::Path> network_paths;
  for (const auto &network : device_->networks()) {
    network_paths.push_back(network.second->dbus_object_path());
  }
  Networks = network_paths;
  NetworksChanged(network_paths);
}

void DeviceDBusAdaptor::UpdateRFInfo() {
  BaseStationId = device_->base_station_id().GetHexString();
  Frequency = device_->frequency();
  CINRs = device_->cinr();
  RSSIs = device_->rssi();
}

void DeviceDBusAdaptor::UpdateStatus() {
  int status = device_->status();
  Status = status;
  StatusChanged(status);
}

NetworkRefPtr DeviceDBusAdaptor::FindNetworkByDBusObjectPath(
    const DBus::Path &network_object_path) const {
  for (const auto &network : device_->networks()) {
    if (network.second->dbus_object_path() == network_object_path)
      return network.second;
  }
  return NULL;
}

void DeviceDBusAdaptor::on_set_property(
    DBus::InterfaceAdaptor& interface,  // NOLINT(runtime/references)
    const string& property, const DBus::Variant& value) {
  if (property == "NetworkScanInterval") {
    device_->SetNetworkScanInterval(value.reader().get_uint32());
  } else if (property == "StatusUpdateInterval") {
    device_->SetStatusUpdateInterval(value.reader().get_uint32());
  }
}

}  // namespace wimax_manager
