// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "oobe_config/filesystem/file_handler.h"
#include "oobe_config/load_oobe_config_flex.h"

#include <base/logging.h>

namespace oobe_config {

LoadOobeConfigFlex::LoadOobeConfigFlex(FileHandler file_handler)
    : file_handler_(file_handler) {}

bool LoadOobeConfigFlex::GetOobeConfigJson(std::string* config) {
  *config = "";
  if (file_handler_.HasFlexConfigFile()) {
    if (file_handler_.ReadFlexConfig(config)) {
      return true;
    } else {
      LOG(ERROR) << "Could not read Flex config.json file.";
    }
  }
  return false;
}

}  // namespace oobe_config
