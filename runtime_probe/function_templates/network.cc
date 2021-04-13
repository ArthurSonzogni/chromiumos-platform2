// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/function_templates/network.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/containers/span.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/values.h>
#include <brillo/dbus/dbus_connection.h>
#include <brillo/variant_dictionary.h>
#include <chromeos/dbus/service_constants.h>
#include <shill/dbus-proxies.h>

#include "runtime_probe/utils/file_utils.h"
#include "runtime_probe/utils/type_utils.h"
#include "runtime_probe/utils/value_utils.h"

namespace runtime_probe {
namespace {
constexpr auto kNetworkDirPath("/sys/class/net/");

constexpr auto kBusTypePci("pci");
constexpr auto kBusTypeSdio("sdio");
constexpr auto kBusTypeUsb("usb");

using FieldType = std::pair<std::string, std::string>;

const std::vector<FieldType> kPciFields = {{"vendor_id", "vendor"},
                                           {"device_id", "device"}};
const std::vector<FieldType> kPciOptionalFields = {
    {"revision", "revision"}, {"subsystem", "subsystem_device"}};
const std::vector<FieldType> kSdioFields = {{"vendor_id", "vendor"}};
const std::vector<FieldType> kSdioOptionalFields = {
    {"manufacturer", "manufacturer"},
    {"product", "product"},
    {"bcd_device", "bcdDevice"}};
const std::vector<FieldType> kUsbFields = {{"vendor_id", "idVendor"},
                                           {"product_id", "idProduct"}};
const std::vector<FieldType> kUsbOptionalFields = {{"bcd_device", "bcdDevice"}};

constexpr int PCI_REVISION_ID_OFFSET = 0x08;

// For linux kernels of versions before 4.10-rc1, there is no standalone file
// `revision` describing the revision id of the PCI component.  The revision is
// still available at offset 8 of the binary file `config`.
base::Optional<uint8_t> GetPciRevisionIdFromConfig(base::FilePath node_path) {
  const auto file_path = node_path.Append("config");
  if (!base::PathExists(file_path)) {
    LOG(ERROR) << file_path.value() << " doesn't exist.";
    return base::nullopt;
  }
  base::File config{file_path, base::File::FLAG_OPEN | base::File::FLAG_READ};
  uint8_t revision_array[1];
  base::span<uint8_t> revision_span(revision_array);
  if (!config.ReadAndCheck(PCI_REVISION_ID_OFFSET, revision_span)) {
    LOG(ERROR) << "Cannot read file " << file_path << " at offset "
               << PCI_REVISION_ID_OFFSET;
    return base::nullopt;
  }
  return revision_array[0];
}

std::vector<brillo::VariantDictionary> GetDevicesProps(
    base::Optional<std::string> type) {
  std::vector<brillo::VariantDictionary> devices_props{};

  brillo::DBusConnection dbus_connection;
  const auto bus = dbus_connection.Connect();
  if (bus == nullptr) {
    LOG(ERROR) << "Failed to connect to system D-Bus service.";
    return {};
  }

  auto shill_proxy =
      std::make_unique<org::chromium::flimflam::ManagerProxy>(bus);
  brillo::VariantDictionary props;
  if (!shill_proxy->GetProperties(&props, nullptr)) {
    LOG(ERROR) << "Unable to get manager properties.";
    return {};
  }
  const auto it = props.find(shill::kDevicesProperty);
  if (it == props.end()) {
    LOG(ERROR) << "Manager properties is missing devices.";
    return {};
  }

  for (const auto& path : it->second.TryGet<std::vector<dbus::ObjectPath>>()) {
    auto device =
        std::make_unique<org::chromium::flimflam::DeviceProxy>(bus, path);
    brillo::VariantDictionary device_props;
    if (!device->GetProperties(&device_props, nullptr)) {
      VLOG(2) << "Unable to get device properties of " << path.value()
              << ". Skipped.";
      continue;
    }
    auto device_type = device_props[shill::kTypeProperty].TryGet<std::string>();
    if (!type || device_type == type) {
      devices_props.push_back(std::move(device_props));
    }
  }

  return devices_props;
}

base::Optional<base::Value> GetNetworkData(const base::FilePath& node_path) {
  const auto dev_path = node_path.Append("device");
  const auto dev_subsystem_path = dev_path.Append("subsystem");
  base::FilePath dev_subsystem_link_path;
  if (!base::ReadSymbolicLink(dev_subsystem_path, &dev_subsystem_link_path)) {
    LOG(ERROR) << "Cannot get real path of " << dev_subsystem_path.value();
    return base::nullopt;
  }

  auto bus_type_idx = dev_subsystem_link_path.value().find_last_of('/') + 1;
  const std::string bus_type =
      dev_subsystem_link_path.value().substr(bus_type_idx);

  const std::vector<FieldType>*fields, *optional_fields;
  base::FilePath field_path;
  if (bus_type == kBusTypePci) {
    field_path = dev_path;
    fields = &kPciFields;
    optional_fields = &kPciOptionalFields;
  } else if (bus_type == kBusTypeSdio) {
    field_path = dev_path;
    fields = &kSdioFields;
    optional_fields = &kSdioOptionalFields;
  } else if (bus_type == kBusTypeUsb) {
    field_path = base::MakeAbsoluteFilePath(dev_path.Append(".."));
    fields = &kUsbFields;
    optional_fields = &kUsbOptionalFields;
  } else {
    LOG(ERROR) << "Unknown bus_type " << bus_type;
    return base::nullopt;
  }

  auto res = MapFilesToDict(field_path, *fields, *optional_fields);
  if (!res) {
    LOG(ERROR) << "Cannot find " << bus_type << "-specific fields on network \""
               << dev_path.value() << "\"";
    return base::nullopt;
  }

  if (bus_type == kBusTypePci && !res->FindKey("revision")) {
    auto revision_id = GetPciRevisionIdFromConfig(dev_path);
    if (revision_id) {
      res->SetStringKey("revision", ByteToHexString(*revision_id));
    }
  }
  PrependToDVKey(&*res, std::string(bus_type) + "_");
  res->SetStringKey("bus_type", bus_type);

  return res;
}

}  // namespace

NetworkFunction::DataType NetworkFunction::EvalImpl() const {
  const auto devices_props = GetDevicesProps(GetNetworkType());
  NetworkFunction::DataType result{};

  for (const auto& device_props : devices_props) {
    base::FilePath node_path(
        kNetworkDirPath +
        device_props.at(shill::kInterfaceProperty).TryGet<std::string>());
    std::string device_type =
        device_props.at(shill::kTypeProperty).TryGet<std::string>();

    VLOG(2) << "Processing the node \"" << node_path.value() << "\".";

    // Get type specific fields and their values.
    auto node_res = GetNetworkData(node_path);
    if (!node_res)
      continue;

    // Report the absolute path we probe the reported info from.
    VLOG_IF(2, node_res->FindStringKey("path"))
        << "Attribute \"path\" already existed. Overrided.";
    node_res->SetStringKey("path", node_path.value());

    VLOG_IF(2, node_res->FindStringKey("type"))
        << "Attribute \"type\" already existed. Overrided.";
    // Align with the category name.
    if (device_type == shill::kTypeWifi) {
      node_res->SetStringKey("type", kTypeWireless);
    } else {
      node_res->SetStringKey("type", device_type);
    }

    result.push_back(std::move(*node_res));
  }

  return result;
}

}  // namespace runtime_probe
