// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>

#include "base/location.h"
#include "oobe_config/filesystem/file_handler.h"
#include "oobe_config/flex_oobe_config.h"

#include <brillo/errors/error_codes.h>
#include <dbus/dbus-protocol.h>
#include <base/logging.h>
#include <oobe_config/proto_bindings/oobe_config.pb.h>

namespace oobe_config {

namespace {

// Helper function to simplify appending errors to `error`.
void AddError(brillo::ErrorPtr* error,
              std::string_view code,
              std::string_view message) {
  brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain, code,
                       message);
}

}  // namespace

FlexOobeConfig::FlexOobeConfig(std::unique_ptr<FileHandler> file_handler)
    : file_handler_(std::move(file_handler)) {}

bool FlexOobeConfig::GetOobeConfigJson(std::string* config) {
  *config = "";
  if (file_handler_->HasFlexOobeConfigFile()) {
    if (file_handler_->ReadFlexOobeConfig(config)) {
      return true;
    } else {
      LOG(ERROR) << "Could not read Flex config.json file.";
    }
  }
  return false;
}

bool FlexOobeConfig::DeleteFlexOobeConfig(brillo::ErrorPtr* error) {
  if (!USE_REVEN_OOBE_CONFIG) {
    AddError(error, DBUS_ERROR_NOT_SUPPORTED,
             "DeleteFlexOobeConfig method is not supported on this platform.");
    return false;
  }
  if (!file_handler_->HasFlexOobeConfigFile()) {
    AddError(error, DBUS_ERROR_FILE_NOT_FOUND, "Flex OOBE config not found.");
    return false;
  }
  bool success = file_handler_->RemoveFlexOobeConfig();
  if (!success) {
    AddError(error, DBUS_ERROR_IO_ERROR, "Failed to delete Flex OOBE config");
    return false;
  }
  return true;
}

}  // namespace oobe_config
