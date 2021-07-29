// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/nvme_storage.h"

#include <pcrecpp.h>

#include <utility>

#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <brillo/errors/error.h>
#include <debugd/dbus-proxies.h>

#include "runtime_probe/system/context_instance.h"
#include "runtime_probe/utils/file_utils.h"
#include "runtime_probe/utils/value_utils.h"

namespace runtime_probe {
namespace {
constexpr auto kNvmeDevicePath = "device/device";
constexpr auto kNvmeDriverPath = "device/device/driver";

// Storage-speicific fields to probe for NVMe.
const std::vector<std::string> kNvmeFields{"vendor", "device", "class"};
constexpr auto kNvmeType = "NVMe";
constexpr auto kNvmePrefix = "pci_";

bool CheckStorageTypeMatch(const base::FilePath& node_path) {
  VLOG(2) << "Checking if \"" << node_path.value() << "\" is NVMe.";
  const auto nvme_driver_path = node_path.Append(kNvmeDriverPath);
  base::FilePath driver_symlink_target;
  if (!base::ReadSymbolicLink(nvme_driver_path, &driver_symlink_target)) {
    VLOG(1) << "\"" << nvme_driver_path.value() << "\" is not a symbolic link";
    VLOG(2) << "\"" << node_path.value() << "\" is not NVMe.";
    return false;
  }
  pcrecpp::RE nvme_driver_re(R"(drivers/nvme)", pcrecpp::RE_Options());
  if (!nvme_driver_re.PartialMatch(driver_symlink_target.value())) {
    return false;
  }
  VLOG(2) << "\"" << node_path.value() << "\" is NVMe.";
  return true;
}

bool NvmeCliList(std::string* output) {
  brillo::ErrorPtr error;
  if (ContextInstance::Get()->debugd_proxy()->Nvme(/*option=*/"list", output,
                                                   &error)) {
    return true;
  }
  LOG(ERROR) << "Debugd::Nvme failed: " << error->GetMessage();
  return false;
}

base::Optional<base::Value> GetStorageToolData() {
  std::string output;
  if (!NvmeCliList(&output))
    return base::nullopt;

  auto value = base::JSONReader::Read(output);
  if (!value) {
    LOG(ERROR) << "Debugd::Nvme failed to parse output as json:\n" << output;
    return base::nullopt;
  }
  return value;
}

}  // namespace

base::Optional<base::Value> NvmeStorageFunction::ProbeFromSysfs(
    const base::FilePath& node_path) const {
  VLOG(2) << "Processnig the node \"" << node_path.value() << "\"";
  if (!CheckStorageTypeMatch(node_path))
    return base::nullopt;

  const auto nvme_path = node_path.Append(kNvmeDevicePath);
  auto nvme_res = MapFilesToDict(nvme_path, kNvmeFields, {});
  if (!nvme_res)
    return base::nullopt;
  PrependToDVKey(&*nvme_res, kNvmePrefix);
  nvme_res->SetStringKey("type", kNvmeType);
  return nvme_res;
}

base::Optional<base::Value> NvmeStorageFunction::ProbeFromStorageTool(
    const base::FilePath& node_path) const {
  auto nvme_data = GetStorageToolData();
  if (!nvme_data)
    return base::nullopt;
  const auto* devices = nvme_data->FindListKey("Devices");
  if (!devices) {
    LOG(ERROR) << "Cannot find \"Devices\" in nvme output.";
    return base::nullopt;
  }

  const auto& device_name = node_path.BaseName();
  base::Value result(base::Value::Type::DICTIONARY);
  for (const auto& device : devices->GetList()) {
    const auto* path = device.FindStringKey("DevicePath");
    if (!path || base::FilePath(*path).BaseName() != device_name)
      continue;
    const auto* firmware = device.FindStringKey("Firmware");
    if (firmware) {
      result.SetStringKey("storage_fw_version", *firmware);
    }
    const auto* model = device.FindStringKey("ModelNumber");
    if (model) {
      result.SetStringKey("storage_model", *model);
    }
    break;
  }
  return result;
}

}  // namespace runtime_probe
