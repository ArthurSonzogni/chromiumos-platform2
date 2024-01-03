// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OOBE_CONFIG_LOAD_OOBE_CONFIG_FLEX_H_
#define OOBE_CONFIG_LOAD_OOBE_CONFIG_FLEX_H_

#include <string>

#include "oobe_config/filesystem/file_handler.h"
#include "oobe_config/load_oobe_config_interface.h"

namespace oobe_config {

// Class responsible for loading the Flex Auto-Enrollment configuration,
// previously written to the stateful partition before or during Flex
// installation (depending on the installation method).
class LoadOobeConfigFlex : public LoadOobeConfigInterface {
 public:
  explicit LoadOobeConfigFlex(FileHandler file_handler = FileHandler());
  LoadOobeConfigFlex(const LoadOobeConfigFlex&) = delete;
  LoadOobeConfigFlex& operator=(const LoadOobeConfigFlex&) = delete;

  ~LoadOobeConfigFlex() = default;

  bool GetOobeConfigJson(std::string* config) override;

 private:
  FileHandler file_handler_;
};

}  // namespace oobe_config

#endif  // OOBE_CONFIG_LOAD_OOBE_CONFIG_FLEX_H_
