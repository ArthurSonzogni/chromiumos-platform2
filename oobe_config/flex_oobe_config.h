// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OOBE_CONFIG_FLEX_OOBE_CONFIG_H_
#define OOBE_CONFIG_FLEX_OOBE_CONFIG_H_

#include <memory>
#include <string>

#include <brillo/errors/error_codes.h>

#include "oobe_config/filesystem/file_handler.h"
#include "oobe_config/load_oobe_config_interface.h"
#include "oobe_config/proto_bindings/oobe_config.pb.h"

namespace oobe_config {

// Class responsible for loading and clearing the Flex Auto-Enrollment
// configuration, previously written to the stateful partition before or during
// Flex installation (depending on the installation method).
class FlexOobeConfig : public LoadOobeConfigInterface {
 public:
  explicit FlexOobeConfig(std::unique_ptr<FileHandler> file_handler =
                              std::make_unique<FileHandler>());
  FlexOobeConfig(const FlexOobeConfig&) = delete;
  FlexOobeConfig& operator=(const FlexOobeConfig&) = delete;

  ~FlexOobeConfig() = default;

  // LoadOobeConfigInterface::GetOobeConfigJson
  bool GetOobeConfigJson(std::string* config) override;

  // Deletes the Flex config JSON. Returns true when deletion succeeds. Returns
  // false when deletion fails, saving any errors in `error`.
  bool DeleteFlexOobeConfig(brillo::ErrorPtr* error);

 private:
  std::unique_ptr<FileHandler> file_handler_;
};

}  // namespace oobe_config

#endif  // OOBE_CONFIG_FLEX_OOBE_CONFIG_H_
