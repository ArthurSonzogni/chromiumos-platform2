// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libbrillo/brillo/dump_kernel_config.h"

#include <optional>
#include <string>

#include <base/logging.h>
#include <vboot/vboot_host.h>

namespace brillo {

std::optional<std::string> DumpKernelConfig(const base::FilePath& kernel_dev) {
  char* config =
      FindKernelConfig(kernel_dev.value().c_str(), USE_PREAMBLE_LOAD_ADDR);
  if (!config) {
    LOG(ERROR) << "Error retrieving kernel config from " << kernel_dev;
    return std::nullopt;
  }

  std::string result = std::string(config, MAX_KERNEL_CONFIG_SIZE);
  free(config);

  return result;
}

}  // namespace brillo
