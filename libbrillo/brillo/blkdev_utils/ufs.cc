// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "brillo/blkdev_utils/ufs.h"

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/brillo_export.h>

namespace brillo {

namespace {

constexpr char kDevToController[] = "../../../../../";
constexpr char kUnitDescriptorDir[] = "device/unit_descriptor";

}  // namespace

bool IsUfs(const base::FilePath& dev_node) {
  base::FilePath unit_descriptor_node = dev_node.Append(kUnitDescriptorDir);
  return base::DirectoryExists(unit_descriptor_node);
}

base::FilePath UfsSysfsToControllerNode(const base::FilePath& dev_node) {
  if (!base::PathExists(dev_node)) {
    PLOG(ERROR) << "Node doesn't exists: " << dev_node;
    return base::FilePath();
  }

  base::FilePath path = dev_node.Append(kDevToController);
  base::FilePath normalized_path;

  normalized_path = base::MakeAbsoluteFilePath(path);
  if (normalized_path.empty()) {
    LOG(ERROR) << "Couldn't normalize: " << path;
    return base::FilePath();
  }

  return normalized_path;
}

}  // namespace brillo
