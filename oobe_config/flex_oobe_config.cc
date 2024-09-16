// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/flex_oobe_config.h"

#include <string>
#include <utility>

#include <base/logging.h>
#include <brillo/errors/error_codes.h>
#include <dbus/dbus-protocol.h>
#include <oobe_config/proto_bindings/oobe_config.pb.h>

#include "base/location.h"
#include "base/process/launch.h"
#include "oobe_config/filesystem/file_handler.h"

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
  if (file_handler_->HasEncryptedFlexOobeConfigFile()) {
    if (file_handler_->ReadFlexOobeConfigFromEncryptedStateful(config)) {
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

  // Unencrypted Flex config should have already been deleted when moved to
  // encrypted stateful partition, but check again just in case and delete if
  // present.
  if (file_handler_->HasUnencryptedFlexOobeConfigFile()) {
    file_handler_->RemoveUnencryptedFlexOobeConfig();
  }

  if (!file_handler_->HasEncryptedFlexOobeConfigFile()) {
    AddError(error, DBUS_ERROR_FILE_NOT_FOUND, "Flex OOBE config not found.");
    return false;
  }
  bool success = file_handler_->RemoveEncryptedFlexOobeConfig();
  if (!success) {
    AddError(error, DBUS_ERROR_IO_ERROR, "Failed to delete Flex OOBE config");
    return false;
  }
  return true;
}

bool FlexOobeConfig::MoveFlexOobeConfigToEncryptedStateful() {
  if (!USE_REVEN_OOBE_CONFIG) {
    return true;
  }
  if (!file_handler_->HasUnencryptedFlexOobeConfigFile()) {
    return true;
  }
  // Don't move flex config yet if we're running from a USB installer, as
  // contents in encrypted stateful partition aren't copied over to disk during
  // chromeos-install.
  if (IsRunningFromInstaller()) {
    return true;
  }
  if (file_handler_->HasEncryptedFlexOobeConfigFile()) {
    // The config file in unencrypted stateful partition wasn't deleted for some
    // reason, even though it has already been copied to encrypted stateful
    // partition. Try removing again before returning.
    LOG(WARNING) << "Flex config is present in both encrypted "
                 << "and unencrypted stateful partition.";
    file_handler_->RemoveUnencryptedFlexOobeConfig();
    return true;
  }

  std::string config;
  if (!file_handler_->ReadFlexOobeConfigFromUnencryptedStateful(&config)) {
    LOG(ERROR) << "Failed to read Flex config file from unencrypted stateful";
  }

  file_handler_->CreateFlexOobeConfigEncryptedStatefulDir();
  if (!file_handler_->WriteFlexOobeConfigToEncryptedStatefulAtomically(
          config)) {
    LOG(ERROR) << "Failed to atomically write Flex config to encrypted "
               << "stateful partition";
    return false;
  }
  if (!file_handler_->ChangeEncryptedFlexOobeConfigPermissions()) {
    LOG(ERROR) << "Failed to change permissions on Flex config file";
    return false;
  }

  file_handler_->RemoveUnencryptedFlexOobeConfig();
  return true;
}

void FlexOobeConfig::SetIsRunningFromInstallerForTesting(
    bool is_running_from_installer) {
  is_running_from_installer_for_testing_ = is_running_from_installer;
}

bool FlexOobeConfig::IsRunningFromInstaller() {
  if (is_running_from_installer_for_testing_) {
    return is_running_from_installer_for_testing_.value();
  }
  std::string output;
  if (!base::GetAppOutput({"is_running_from_installer"}, &output)) {
    LOG(ERROR) << "Failed to run is_running_from_installer: " << output;
    // Tread on the side of caution, and return true when it's unknown whether
    // we're running from an installer or not, so that flex_config isn't
    // migrated when it shouldn't be.
    return true;
  }
  return output == "yes\n";
}

}  // namespace oobe_config
