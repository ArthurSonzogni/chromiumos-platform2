// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/mmc_host.h"

#include <utility>

#include <base/files/file_util.h>

#include "runtime_probe/system/context.h"
#include "runtime_probe/utils/bus_utils.h"
#include "runtime_probe/utils/file_utils.h"

namespace runtime_probe {

MmcHostFunction::DataType MmcHostFunction::EvalImpl() const {
  DataType results;

  base::FilePath pattern =
      Context::Get()->root_dir().Append("sys/class/mmc_host/*");
  for (const auto& mmc_host_path : Glob(pattern)) {
    auto node_res = GetDeviceBusDataFromSysfsNode(mmc_host_path);
    if (!node_res) {
      continue;
    }
    results.Append(std::move(*node_res));
  }

  return results;
}

}  // namespace runtime_probe
