// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/function_templates/network.h"

#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <base/containers/span.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/values.h>
#include <brillo/dbus/dbus_connection.h>
#include <brillo/variant_dictionary.h>
#include <chromeos/dbus/service_constants.h>
#include <shill/dbus-proxies.h>

#include "runtime_probe/system/context.h"
#include "runtime_probe/utils/file_utils.h"
#include "runtime_probe/utils/type_utils.h"
#include "runtime_probe/utils/value_utils.h"

namespace runtime_probe {
namespace {

constexpr auto kBusTypePci("pci");
constexpr auto kBusTypeSdio("sdio");
constexpr auto kBusTypeUsb("usb");

using FieldType = std::pair<std::string, std::string>;

const std::vector<FieldType> kPciFields = {{"vendor_id", "vendor"},
                                           {"device_id", "device"}};
const std::vector<FieldType> kPciOptionalFields = {
    {"revision", "revision"}, {"subsystem", "subsystem_device"}};
const std::vector<FieldType> kSdioFields = {{"vendor_id", "vendor"},
                                            {"device_id", "device"}};
const std::vector<FieldType> kSdioOptionalFields = {};
const std::vector<FieldType> kUsbFields = {{"vendor_id", "idVendor"},
                                           {"product_id", "idProduct"}};
const std::vector<FieldType> kUsbOptionalFields = {{"bcd_device", "bcdDevice"}};

constexpr int PCI_REVISION_ID_OFFSET = 0x08;

// For linux kernels of versions before 4.10-rc1, there is no standalone file
// `revision` describing the revision id of the PCI component.  The revision is
// still available at offset 8 of the binary file `config`.
std::optional<uint8_t> GetPciRevisionIdFromConfig(base::FilePath node_path) {
  const auto file_path = node_path.Append("config");
  if (!base::PathExists(file_path)) {
    LOG(ERROR) << file_path.value() << " doesn't exist.";
    return std::nullopt;
  }
  base::File config{file_path, base::File::FLAG_OPEN | base::File::FLAG_READ};
  uint8_t revision_array[1];
  base::span<uint8_t> revision_span(revision_array);
  if (!config.ReadAndCheck(PCI_REVISION_ID_OFFSET, revision_span)) {
    LOG(ERROR) << "Cannot read file " << file_path << " at offset "
               << PCI_REVISION_ID_OFFSET;
    return std::nullopt;
  }
  return revision_array[0];
}

std::map<std::string, std::string> GetDevicesType() {
  std::map<std::string, std::string> result;

  auto shill_proxy = Context::Get()->shill_manager_proxy();
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
    auto device = Context::Get()->CreateShillDeviceProxy(path);
    brillo::VariantDictionary device_props;
    if (!device->GetProperties(&device_props, nullptr)) {
      VLOG(2) << "Unable to get device properties of " << path.value()
              << ". Skipped.";
      continue;
    }
    std::string interface =
        device_props.at(shill::kInterfaceProperty).TryGet<std::string>();
    std::string type =
        device_props.at(shill::kTypeProperty).TryGet<std::string>();
    result[interface] = type;
  }

  return result;
}

std::optional<base::Value> GetNetworkData(const base::FilePath& node_path) {
  const auto dev_path = node_path.Append("device");
  const auto dev_subsystem_path = dev_path.Append("subsystem");
  base::FilePath dev_subsystem_link_path;
  if (!base::ReadSymbolicLink(dev_subsystem_path, &dev_subsystem_link_path)) {
    VLOG(2) << "Cannot get real path of " << dev_subsystem_path;
    return std::nullopt;
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
    return std::nullopt;
  }

  auto res = MapFilesToDict(field_path, *fields, *optional_fields);
  if (!res) {
    LOG(ERROR) << "Cannot find " << bus_type << "-specific fields on network \""
               << dev_path.value() << "\"";
    return std::nullopt;
  }

  auto& res_dict = res->GetDict();
  if (bus_type == kBusTypePci && !res_dict.Find("revision")) {
    auto revision_id = GetPciRevisionIdFromConfig(dev_path);
    if (revision_id) {
      res_dict.Set("revision", ByteToHexString(*revision_id));
    }
  }
  PrependToDVKey(&*res, std::string(bus_type) + "_");
  res_dict.Set("bus_type", bus_type);

  return res;
}

}  // namespace

NetworkFunction::DataType NetworkFunction::EvalImpl() const {
  DataType results;
  base::FilePath net_dev_pattern =
      Context::Get()->root_dir().Append("sys/class/net/*");
  for (const auto& net_dev_path : Glob(net_dev_pattern)) {
    auto node_res = GetNetworkData(net_dev_path);
    if (!node_res) {
      continue;
    }
    CHECK(!node_res->GetDict().FindString("path"));
    node_res->GetDict().Set("path", net_dev_path.value());
    results.Append(std::move(*node_res));
  }

  return results;
}

void NetworkFunction::PostHelperEvalImpl(DataType* results) const {
  const std::optional<std::string> target_type = GetNetworkType();
  const auto devices_type = GetDevicesType();
  auto helper_results = std::move(*results);
  *results = DataType();

  for (auto& helper_result : helper_results) {
    auto& dict = helper_result.GetDict();
    auto* path = dict.FindString("path");
    CHECK(path);
    std::string interface = base::FilePath{*path}.BaseName().value();
    auto it = devices_type.find(interface);
    if (it == devices_type.end()) {
      LOG(ERROR) << "Cannot get type of interface " << interface;
      continue;
    }
    if (target_type && target_type.value() != it->second) {
      VLOG(3) << "Interface " << interface << " doesn't match the target type "
              << target_type.value();
      continue;
    }
    CHECK(!dict.FindString("type"));
    dict.Set("type", it->second);
    results->Append(std::move(helper_result));
  }
}

}  // namespace runtime_probe
