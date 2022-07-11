// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "discod/utils/ufs.h"

#include <optional>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>

namespace discod {

namespace {

constexpr char kSysBlock[] = "sys/block";
constexpr char kDescriptorDir[] = "device/unit_descriptor";
constexpr char kWbNode[] = "../../../../../wb_on";

}  // namespace

bool IsUfs(const base::FilePath& root_device, const base::FilePath& root) {
  base::FilePath unit_descriptor_node = root.Append(kSysBlock)
                                            .Append(root_device.BaseName())
                                            .Append(kDescriptorDir);
  return base::DirectoryExists(unit_descriptor_node);
}

base::FilePath GetUfsDeviceNode(const base::FilePath& root_device,
                                const base::FilePath& root) {
  if (!IsUfs(root_device, root)) {
    return base::FilePath();
  }
  base::FilePath device_node =
      root.Append(kSysBlock).Append(root_device.BaseName());

  VLOG(2) << "Candidade device_node=" << device_node;

  return base::PathExists(device_node) ? device_node : base::FilePath();
}

base::FilePath GetUfsWriteBoosterNode(const base::FilePath& root_device,
                                      const base::FilePath& root) {
  if (!IsUfs(root_device, root)) {
    return base::FilePath();
  }

  base::FilePath linked_dev_node =
      root.Append(kSysBlock).Append(root_device.BaseName());
  base::FilePath raw_dev_node;
  if (!base::ReadSymbolicLink(linked_dev_node, &raw_dev_node)) {
    return base::FilePath();
  }

  base::FilePath wb_node;
  if (raw_dev_node.IsAbsolute()) {
    wb_node = raw_dev_node.Append(kWbNode);
  } else {
    wb_node = linked_dev_node.DirName().Append(raw_dev_node).Append(kWbNode);
  }
  VLOG(2) << "Candidade wb_node=" << wb_node;
  return base::PathExists(wb_node) ? wb_node : base::FilePath();
}

}  // namespace discod
