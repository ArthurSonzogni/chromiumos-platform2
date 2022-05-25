// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/gpu.h"

#include <optional>
#include <string>
#include <utility>

#include <base/containers/fixed_flat_set.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/strings/string_piece.h>
#include <base/strings/string_util.h>

#include "runtime_probe/system/context.h"
#include "runtime_probe/utils/file_utils.h"

namespace runtime_probe {
namespace {

constexpr char kPCIDevicesPath[] = "sys/bus/pci/devices";
constexpr auto kGPUFields = base::MakeFixedFlatSet<base::StringPiece>(
    {"vendor", "device", "subsystem_vendor", "subsystem_device"});

bool IsDGPUDevice(const base::FilePath& sysfs_node) {
  base::FilePath class_file = sysfs_node.Append("class");
  std::string class_value;
  if (!ReadFileToString(class_file, &class_value))
    return false;
  // 0x03 is the class code of PCI display controllers.
  // 0x00 is the subclass code of VGA compatible controller.
  return base::StartsWith(class_value, "0x0300");
}

}  // namespace

GpuFunction::DataType GpuFunction::EvalImpl() const {
  DataType results{};

  base::FileEnumerator it(
      Context::Get()->root_dir().Append(kPCIDevicesPath), false,
      base::FileEnumerator::SHOW_SYM_LINKS | base::FileEnumerator::FILES |
          base::FileEnumerator::DIRECTORIES);
  for (auto path = it.Next(); !path.empty(); path = it.Next()) {
    if (!IsDGPUDevice(path))
      continue;
    std::optional<base::Value> res = MapFilesToDict(path, kGPUFields);
    if (res.has_value())
      results.push_back(std::move(res).value());
  }

  return results;
}

}  // namespace runtime_probe
