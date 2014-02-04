// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/device_dbus_adaptor.h"

#include <map>
#include <string>

#include <base/bind.h>

#include "shill/device.h"
#include "shill/error.h"
#include "shill/logging.h"

using base::Bind;
using std::map;
using std::string;

namespace shill {

// static
const char DeviceDBusAdaptor::kPath[] = "/device/";

DeviceDBusAdaptor::DeviceDBusAdaptor(DBus::Connection* conn, Device *device)
    : DBusAdaptor(conn, kPath + device->UniqueName()),
      device_(device),
      connection_name_(conn->unique_name()) {
}

DeviceDBusAdaptor::~DeviceDBusAdaptor() {
  device_ = NULL;
}
const std::string &DeviceDBusAdaptor::GetRpcIdentifier() {
  return path();
}

const std::string &DeviceDBusAdaptor::GetRpcConnectionIdentifier() {
  return connection_name_;
}

void DeviceDBusAdaptor::EmitBoolChanged(const std::string& name, bool value) {
  SLOG(DBus, 2) << __func__ << ": " << name;
  PropertyChanged(name, DBusAdaptor::BoolToVariant(value));
}

void DeviceDBusAdaptor::EmitUintChanged(const std::string& name, uint32 value) {
  SLOG(DBus, 2) << __func__ << ": " << name;
  PropertyChanged(name, DBusAdaptor::Uint32ToVariant(value));
}

void DeviceDBusAdaptor::EmitUint16Changed(const string &name, uint16 value) {
  SLOG(DBus, 2) << __func__ << ": " << name;
  PropertyChanged(name, DBusAdaptor::Uint16ToVariant(value));
}

void DeviceDBusAdaptor::EmitIntChanged(const std::string& name, int value) {
  SLOG(DBus, 2) << __func__ << ": " << name;
  PropertyChanged(name, DBusAdaptor::Int32ToVariant(value));
}

void DeviceDBusAdaptor::EmitStringChanged(const std::string& name,
                                          const std::string& value) {
  SLOG(DBus, 2) << __func__ << ": " << name;
  PropertyChanged(name, DBusAdaptor::StringToVariant(value));
}

void DeviceDBusAdaptor::EmitStringmapChanged(const std::string &name,
                                             const Stringmap &value) {
  SLOG(DBus, 2) << __func__ << ": " << name;
  PropertyChanged(name, DBusAdaptor::StringmapToVariant(value));
}

void DeviceDBusAdaptor::EmitStringmapsChanged(const std::string &name,
                                              const Stringmaps &value) {
  SLOG(DBus, 2) << __func__ << ": " << name;
  PropertyChanged(name, DBusAdaptor::StringmapsToVariant(value));
}

void DeviceDBusAdaptor::EmitStringsChanged(const std::string &name,
                                              const Strings &value) {
  SLOG(DBus, 2) << __func__ << ": " << name;
  PropertyChanged(name, DBusAdaptor::StringsToVariant(value));
}

void DeviceDBusAdaptor::EmitKeyValueStoreChanged(const std::string &name,
                                                 const KeyValueStore &value) {
  SLOG(DBus, 2) << __func__ << ": " << name;
  PropertyChanged(name, DBusAdaptor::KeyValueStoreToVariant(value));
}

map<string, ::DBus::Variant> DeviceDBusAdaptor::GetProperties(
    ::DBus::Error &error) {
  SLOG(DBus, 2) << __func__ << " " << device_->FriendlyName();
  map<string, ::DBus::Variant> properties;
  DBusAdaptor::GetProperties(device_->store(), &properties, &error);
  return properties;
}

void DeviceDBusAdaptor::SetProperty(const string &name,
                                    const ::DBus::Variant &value,
                                    ::DBus::Error &error) {
  SLOG(DBus, 2) << __func__ << ": " << name;
  DBusAdaptor::SetProperty(device_->mutable_store(), name, value, &error);
}

void DeviceDBusAdaptor::ClearProperty(const std::string &name,
                                      ::DBus::Error &error) {
  SLOG(DBus, 2) << __func__ << ": " << name;
  DBusAdaptor::ClearProperty(device_->mutable_store(), name, &error);
}

void DeviceDBusAdaptor::Enable(::DBus::Error &error) {
  SLOG(DBus, 2) << __func__;
  Error e(Error::kOperationInitiated);
  DBus::Tag *tag = new DBus::Tag();
  device_->SetEnabledPersistent(true, &e, GetMethodReplyCallback(tag));
  ReturnResultOrDefer(tag, e, &error);
}

void DeviceDBusAdaptor::Disable(::DBus::Error &error) {
  SLOG(DBus, 2) << __func__;
  Error e(Error::kOperationInitiated);
  DBus::Tag *tag = new DBus::Tag();
  device_->SetEnabledPersistent(false, &e, GetMethodReplyCallback(tag));
  ReturnResultOrDefer(tag, e, &error);
}

void DeviceDBusAdaptor::ProposeScan(::DBus::Error &error) {
  SLOG(DBus, 2) << __func__;
  Error e;
  // User scan requests, which are the likely source of DBus requests, probably
  // aren't time-critical so we might as well perform a complete scan.  It
  // also provides a failsafe for progressive scan.
  device_->Scan(Device::kFullScan, &e, __func__);
  e.ToDBusError(&error);
}

::DBus::Path DeviceDBusAdaptor::AddIPConfig(const string& ,
                                            ::DBus::Error &error) {
  SLOG(DBus, 2) << __func__;
  Error e(Error::kNotSupported, "This function is deprecated in shill");
  e.ToDBusError(&error);
  return "/";
}

void DeviceDBusAdaptor::Register(const string &network_id,
                                 ::DBus::Error &error) {
  SLOG(DBus, 2) << __func__ << "(" << network_id << ")";
  Error e(Error::kOperationInitiated);
  DBus::Tag *tag = new DBus::Tag();
  device_->RegisterOnNetwork(network_id, &e, GetMethodReplyCallback(tag));
  ReturnResultOrDefer(tag, e, &error);
}

void DeviceDBusAdaptor::RequirePin(
    const string &pin, const bool &require, DBus::Error &error) {
  SLOG(DBus, 2) << __func__;
  Error e(Error::kOperationInitiated);
  DBus::Tag *tag = new DBus::Tag();
  device_->RequirePIN(pin, require, &e, GetMethodReplyCallback(tag));
  ReturnResultOrDefer(tag, e, &error);
}

void DeviceDBusAdaptor::EnterPin(const string &pin, DBus::Error &error) {
  SLOG(DBus, 2) << __func__;
  Error e(Error::kOperationInitiated);
  DBus::Tag *tag = new DBus::Tag();
  device_->EnterPIN(pin, &e, GetMethodReplyCallback(tag));
  ReturnResultOrDefer(tag, e, &error);
}

void DeviceDBusAdaptor::UnblockPin(
    const string &unblock_code, const string &pin, DBus::Error &error) {
  SLOG(DBus, 2) << __func__;
  Error e(Error::kOperationInitiated);
  DBus::Tag *tag = new DBus::Tag();
  device_->UnblockPIN(unblock_code, pin, &e, GetMethodReplyCallback(tag));
  ReturnResultOrDefer(tag, e, &error);
}

void DeviceDBusAdaptor::ChangePin(
    const string &old_pin, const string &new_pin, DBus::Error &error) {
  SLOG(DBus, 2) << __func__;
  Error e(Error::kOperationInitiated);
  DBus::Tag *tag = new DBus::Tag();
  device_->ChangePIN(old_pin, new_pin, &e, GetMethodReplyCallback(tag));
  ReturnResultOrDefer(tag, e, &error);
}

void DeviceDBusAdaptor::Reset(::DBus::Error &error) {
  SLOG(DBus, 2) << __func__;
  Error e(Error::kOperationInitiated);
  DBus::Tag *tag = new DBus::Tag();
  device_->Reset(&e, GetMethodReplyCallback(tag));
  ReturnResultOrDefer(tag, e, &error);
}

string DeviceDBusAdaptor::PerformTDLSOperation(const string &operation,
                                               const string &peer,
                                               DBus::Error &error) {
  Error e;
  string return_value = device_->PerformTDLSOperation(operation, peer, &e);
  e.ToDBusError(&error);
  return return_value;
}

void DeviceDBusAdaptor::ResetByteCounters(DBus::Error &error) {
  device_->ResetByteCounters();
}

void DeviceDBusAdaptor::SetCarrier(const string &carrier, DBus::Error &error) {
  SLOG(DBus, 2) << __func__ << "(" << carrier << ")";
  Error e(Error::kOperationInitiated);
  DBus::Tag *tag = new DBus::Tag();
  device_->SetCarrier(carrier, &e, GetMethodReplyCallback(tag));
  ReturnResultOrDefer(tag, e, &error);
}

}  // namespace shill
