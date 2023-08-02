// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_FAKE_SHILL_CLIENT_H_
#define PATCHPANEL_FAKE_SHILL_CLIENT_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/memory/ref_counted.h>
// Ignore Wconversion warnings in dbus headers.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#pragma GCC diagnostic pop
#include <dbus/object_path.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "patchpanel/shill_client.h"
#include "patchpanel/system.h"

using testing::_;
using testing::AnyNumber;
using testing::Return;

namespace patchpanel {

// TODO(b/273741099): Migrate all interface name arguments to device object path
// arguments in this class.
class FakeShillClient : public ShillClient {
 public:
  explicit FakeShillClient(scoped_refptr<dbus::Bus> bus, System* system)
      : ShillClient(bus, system) {}

  std::vector<dbus::ObjectPath> GetServices() override {
    std::vector<dbus::ObjectPath> services;
    if (fake_default_logical_ifname_) {
      services.emplace_back(*fake_default_logical_ifname_);
    }
    if (fake_default_physical_ifname_) {
      services.emplace_back(*fake_default_physical_ifname_);
    }
    return services;
  }

  std::optional<Device> GetDeviceFromServicePath(
      const dbus::ObjectPath& service_path) override {
    Device device = {.type = Device::Type::kUnknown};
    if (service_path.value() == fake_default_logical_ifname_) {
      device.ifname = *fake_default_logical_ifname_;
      return device;
    }
    if (service_path.value() == fake_default_physical_ifname_) {
      device.ifname = *fake_default_physical_ifname_;
      return device;
    }
    return std::nullopt;
  }

  void SetFakeDefaultLogicalDevice(std::optional<std::string> ifname) {
    fake_default_logical_ifname_ = ifname;
  }

  void SetFakeDefaultPhysicalDevice(std::optional<std::string> ifname) {
    fake_default_physical_ifname_ = ifname;
  }

  void SetIfname(const std::string& device_path, const std::string& ifname) {
    interface_names_[device_path] = ifname;
  }

  void SetFakeDeviceProperties(const dbus::ObjectPath& device_path,
                               const Device& device) {
    fake_device_properties_[device_path] = device;
  }

  void NotifyManagerPropertyChange(const std::string& name,
                                   const brillo::Any& value) {
    OnManagerPropertyChange(name, value);
  }

  void NotifyDevicePropertyChange(const dbus::ObjectPath& device_path,
                                  const std::string& name,
                                  const brillo::Any& value) {
    OnDevicePropertyChange(device_path, name, value);
  }

  bool GetDeviceProperties(const dbus::ObjectPath& device_path,
                           Device* output) override {
    get_device_properties_calls_.insert(device_path);
    if (fake_device_properties_.find(device_path) !=
        fake_device_properties_.end()) {
      *output = fake_device_properties_[device_path];
    }
    return true;
  }

  const ShillClient::Device* GetDeviceByShillDeviceName(
      const std::string& shill_device_interface_property) const override {
    for (const auto& [_, device] : fake_device_properties_) {
      if (device.shill_device_interface_property ==
          shill_device_interface_property) {
        return &device;
      }
    }
    return nullptr;
  }

  const std::set<dbus::ObjectPath>& get_device_properties_calls() {
    return get_device_properties_calls_;
  }

 private:
  std::map<std::string, std::string> interface_names_;
  std::optional<std::string> fake_default_logical_ifname_;
  std::optional<std::string> fake_default_physical_ifname_;
  std::map<dbus::ObjectPath, Device> fake_device_properties_;
  std::set<dbus::ObjectPath> get_device_properties_calls_;
};

class FakeShillClientHelper {
 public:
  FakeShillClientHelper() {
    mock_proxy_ = new dbus::MockObjectProxy(
        mock_bus_.get(), "org.chromium.flimflam", dbus::ObjectPath("/path"));
    // Set these expectations rather than just ignoring them to confirm
    // the ShillClient obtains the expected proxy and registers for
    // property changes.
    EXPECT_CALL(*mock_bus_, GetObjectProxy("org.chromium.flimflam", _))
        .WillRepeatedly(Return(mock_proxy_.get()));
    EXPECT_CALL(*mock_proxy_, DoConnectToSignal("org.chromium.flimflam.Manager",
                                                "PropertyChanged", _, _))
        .Times(AnyNumber());
    EXPECT_CALL(*mock_proxy_, DoConnectToSignal("org.chromium.flimflam.Device",
                                                "PropertyChanged", _, _))
        .Times(AnyNumber());

    client_ = std::make_unique<FakeShillClient>(mock_bus_, nullptr);
  }

  std::unique_ptr<ShillClient> Client() { return std::move(client_); }

  std::unique_ptr<FakeShillClient> FakeClient() { return std::move(client_); }

  dbus::MockObjectProxy* mock_proxy() { return mock_proxy_.get(); }
  scoped_refptr<dbus::MockBus> mock_bus() { return mock_bus_; }

 private:
  scoped_refptr<dbus::MockBus> mock_bus_{
      new dbus::MockBus{dbus::Bus::Options{}}};
  scoped_refptr<dbus::MockObjectProxy> mock_proxy_;

  std::unique_ptr<FakeShillClient> client_;
};

}  // namespace patchpanel

#endif  // PATCHPANEL_FAKE_SHILL_CLIENT_H_
