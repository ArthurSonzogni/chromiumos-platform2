// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dbus-c++/dbus.h>
#include <stdio.h>

#include <base/json/json_writer.h>
#include <base/strings/string_util.h>
#include <base/values.h>
#include <chromeos/dbus/service_constants.h>
#include <debugd/src/dbus_utils.h>
#include <shill/dbus_proxies/org.chromium.flimflam.Device.h>
#include <shill/dbus_proxies/org.chromium.flimflam.IPConfig.h>
#include <shill/dbus_proxies/org.chromium.flimflam.Manager.h>
#include <shill/dbus_proxies/org.chromium.flimflam.Service.h>

using base::DictionaryValue;
using base::Value;

class DeviceProxy : public org::chromium::flimflam::Device_proxy,
                    public DBus::ObjectProxy {
 public:
  DeviceProxy(DBus::Connection* connection,
              const char* path,
              const char* service)
      : DBus::ObjectProxy(*connection, path, service) {}
  ~DeviceProxy() override = default;
  void PropertyChanged(const std::string&, const DBus::Variant&) override {}
};

class IPConfigProxy : public org::chromium::flimflam::IPConfig_proxy,
                      public DBus::ObjectProxy {
 public:
  IPConfigProxy(DBus::Connection* connection,
                const char* path,
                const char* service)
      : DBus::ObjectProxy(*connection, path, service) {}
  ~IPConfigProxy() override = default;
  void PropertyChanged(const std::string&, const DBus::Variant&) override {}
};

class ManagerProxy : public org::chromium::flimflam::Manager_proxy,
                     public DBus::ObjectProxy {
 public:
  ManagerProxy(DBus::Connection* connection,
               const char* path,
               const char* service)
      : DBus::ObjectProxy(*connection, path, service) {}
  ~ManagerProxy() override = default;
  void PropertyChanged(const std::string&, const DBus::Variant&) override {}
  void StateChanged(const std::string&) override {}
};

class ServiceProxy : public org::chromium::flimflam::Service_proxy,
                     public DBus::ObjectProxy {
 public:
  ServiceProxy(DBus::Connection* connection,
               const char* path,
               const char* service)
      : DBus::ObjectProxy(*connection, path, service) {}
  ~ServiceProxy() override = default;
  void PropertyChanged(const std::string&, const DBus::Variant&) override {}
};

Value* GetService(DBus::Connection* connection, const DBus::Path& path) {
  ServiceProxy service(connection, path.c_str(), shill::kFlimflamServiceName);
  std::map<std::string, DBus::Variant> props = service.GetProperties();
  Value* v = nullptr;
  debugd::DBusPropertyMapToValue(props, &v);
  return v;
}

Value* GetServices(DBus::Connection* connection, ManagerProxy* flimflam) {
  std::map<std::string, DBus::Variant> props = flimflam->GetProperties();
  DictionaryValue* dv = new DictionaryValue();
  const DBus::Variant& services = props["Services"];
  std::vector<DBus::Path> paths = services;
  for (std::vector<DBus::Path>::iterator it = paths.begin();
       it != paths.end();
       ++it) {
    Value* v = GetService(connection, *it);
    if (v)
      dv->Set(*it, v);
  }
  return dv;
}

Value* GetIPConfig(DBus::Connection* connection, const DBus::Path& path) {
  IPConfigProxy ipconfig(connection, path.c_str(), shill::kFlimflamServiceName);
  std::map<std::string, DBus::Variant> props = ipconfig.GetProperties();
  Value* v = nullptr;
  debugd::DBusPropertyMapToValue(props, &v);
  return v;
}

Value* GetDevice(DBus::Connection* connection, const DBus::Path& path) {
  DeviceProxy device(connection, path.c_str(), shill::kFlimflamServiceName);
  std::map<std::string, DBus::Variant> props = device.GetProperties();
  DictionaryValue* ipconfigs = nullptr;
  if (props.count("IPConfigs") == 1) {
    ipconfigs = new DictionaryValue();
    // Turn IPConfigs into real objects.
    DBus::Variant& ipconfig_paths = props["IPConfigs"];
    std::vector<DBus::Path> paths = ipconfig_paths;
    for (std::vector<DBus::Path>::iterator it = paths.begin();
         it != paths.end();
         ++it) {
      Value* v = GetIPConfig(connection, *it);
      if (v)
        ipconfigs->Set(*it, v);
    }
    props.erase("IPConfigs");
  }
  Value* v = nullptr;
  CHECK(debugd::DBusPropertyMapToValue(props, &v));
  DictionaryValue* dv = reinterpret_cast<DictionaryValue*>(v);
  if (ipconfigs)
    dv->Set("ipconfigs", ipconfigs);
  return v;
}

Value* GetDevices(DBus::Connection* connection, ManagerProxy* flimflam) {
  std::map<std::string, DBus::Variant> props = flimflam->GetProperties();
  DictionaryValue* dv = new DictionaryValue();
  const DBus::Variant& devices = props["Devices"];
  std::vector<DBus::Path> paths = devices;
  for (std::vector<DBus::Path>::iterator it = paths.begin();
       it != paths.end();
       ++it) {
    Value* v = GetDevice(connection, *it);
    if (v)
      dv->Set(*it, v);
  }
  return dv;
}

int main() {
  DBus::BusDispatcher dispatcher;
  DBus::default_dispatcher = &dispatcher;
  DBus::Connection connection = DBus::Connection::SystemBus();
  ManagerProxy manager(&connection,
                       shill::kFlimflamServicePath,
                       shill::kFlimflamServiceName);
  DictionaryValue result;

  Value* devices = GetDevices(&connection, &manager);
  result.Set("devices", devices);

  Value* services = GetServices(&connection, &manager);
  result.Set("services", services);

  std::string json;
  base::JSONWriter::WriteWithOptions(
      result, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json);
  printf("%s\n", json.c_str());
  return 0;
}
