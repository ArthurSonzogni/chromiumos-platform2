// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A tool that can be used to access device management related
// functionalities. Please see the usage message for details.

#include "device_management/client/client.h"
#include "device_management/proto_bindings/device_management_interface.pb.h"
#include "device_management-client/device_management/dbus-proxies.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdlib.h>
#include <string>

#include <base/logging.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

namespace device_management {
namespace actions {
constexpr char kGetFirmwareManagementParameters[] =
    "get_firmware_management_parameters";
constexpr char kSetFirmwareManagementParameters[] =
    "set_firmware_management_parameters";
constexpr char kRemoveFirmwareManagementParameters[] =
    "remove_firmware_management_parameters";
constexpr char kInstallAttributesGet[] = "install_attributes_get";
constexpr char kInstallAttributesSet[] = "install_attributes_set";
constexpr char kInstallAttributesFinalize[] = "install_attributes_finalize";
constexpr char kInstallAttributesGetStatus[] = "install_attributes_get_status";
constexpr char kInstallAttributesCount[] = "install_attributes_count";
constexpr char kInstallAttributesIsReady[] = "install_attributes_is_ready";
constexpr char kInstallAttributesIsSecure[] = "install_attributes_is_secure";
constexpr char kInstallAttributesIsInvalid[] = "install_attributes_is_invalid";
constexpr char kInstallAttributesIsFirstInstall[] =
    "install_attributes_is_first_install";

constexpr char kActionList[] = R"(
  get_firmware_management_parameters
  set_firmware_management_parameters
  remove_firmware_management_parameters
  install_attributes_get
  install_attributes_set
  install_attributes_finalize
  install_attributes_get_status
  install_attributes_count
  install_attributes_is_ready
  install_attributes_is_secure
  install_attributes_is_invalid
  install_attributes_is_first_install
)";

constexpr char kUsage[] = R"(
Usage: device_management_client --action=<command> [<arguments>]
Commands:
  get_firmware_management_parameters
      Retrieves firmware management parameters.
  set_firmware_management_parameters --flags=XXX [--developer_key_hash=YYY]
      Sets firmware management parameters.
      `XXX`: flags as a 32-bit value
      `YYY`: [optional] SHA-256 developer key hash digest
             as a 64-character hexadecimal string.
  remove_firmware_management_parameters
      Removes firmware management parameters.
  install_attributes_get --name=XXX
      Retrieves the value of name `XXX` from install attributes.
  install_attributes_set --name=XXX --value=YYY
      Sets the value `YYY` against the name `XXX` from install attributes.
  install_attributes_finalize
      Finalizes the install attributes storage.
      After finalization, the data becomes read-only.
  install_attributes_get_status
      Retrives current status of install attributes.
      Status list:
        UNKNOWN
        TPM_NOT_OWNED
        FIRST_INSTALL
        VALID
        INVALID
  install_attributes_count
      Retrieves the number of entries in the install attributes storage.
  install_attributes_is_ready
      Prints 1 if the current status is not UNKNOWN and TPM_NOT_OWNED,
      0 otherwise.
  install_attributes_is_secure
      Prints 1 if the attribute storage is securely stored, 0 otherwise.
  install_attributes_is_invalid
      Prints 1 if the status is INVALID, 0 otherwise.
  install_attributes_is_first_install
      Prints 1 if the status is FIRST_INSTALL, 0 otherwise.
)";
}  // namespace actions
}  // namespace device_management

// TODO(b/289757208): Modernize the client tool of device_management service.
int main(int argc, char** argv) {
  DEFINE_string(action, "", device_management::actions::kActionList);
  DEFINE_string(flags, "", "flags as a 32-bit value");
  DEFINE_string(developer_key_hash, "",
                "[optional] SHA-256 developer key hash digest as a "
                "64-character hexadecimal string");
  DEFINE_string(name, "", "install attributes name as string");
  DEFINE_string(value, "", "install attributes value as string");
  brillo::FlagHelper::Init(argc, argv, "device_management");
  brillo::OpenLog("device_management_client", true);
  base::CommandLine::Init(argc, argv);
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderr);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();

  std::unique_ptr<device_management::DeviceManagementClient>
      device_management_client = device_management::DeviceManagementClient::
          CreateDeviceManagementClient();

  if (!device_management_client->InitializePrinter(cl)) {
    LOG(ERROR) << __func__ << ": failed to initialize the printer";
  }

  std::string action = cl->GetSwitchValueASCII("action");

  if (action == device_management::actions::kGetFirmwareManagementParameters) {
    if (!device_management_client->GetFWMP()) {
      LOG(ERROR) << __func__ << ": failed to call GetFWMP()";
      return EXIT_FAILURE;
    }
  } else if (action ==
             device_management::actions::kSetFirmwareManagementParameters) {
    if (!device_management_client->SetFWMP(cl)) {
      LOG(ERROR) << __func__ << ": failed to call SetFWMP()";
      return EXIT_FAILURE;
    }
  } else if (action ==
             device_management::actions::kRemoveFirmwareManagementParameters) {
    if (!device_management_client->RemoveFWMP()) {
      LOG(ERROR) << __func__ << ": failed to call RemoveFWMP()";
      return EXIT_FAILURE;
    }
  } else if (action == device_management::actions::kInstallAttributesGet) {
    if (!device_management_client->GetInstallAttributes(cl)) {
      LOG(ERROR) << __func__ << ": failed to call GetInstallAttributes()";
      return EXIT_FAILURE;
    }
  } else if (action == device_management::actions::kInstallAttributesSet) {
    if (!device_management_client->SetInstallAttributes(cl)) {
      LOG(ERROR) << __func__ << ": failed to call SetInstallAttributes()";
      return EXIT_FAILURE;
    }
  } else if (action == device_management::actions::kInstallAttributesFinalize) {
    if (!device_management_client->FinalizeInstallAttributes()) {
      LOG(ERROR) << __func__ << ": failed to call FinalizeInstallAttributes()";
      return EXIT_FAILURE;
    }
  } else if (action ==
             device_management::actions::kInstallAttributesGetStatus) {
    if (!device_management_client->GetStatusInstallAttributes()) {
      LOG(ERROR) << __func__ << ": failed to call GetStatusInstallAttributes()";
      return EXIT_FAILURE;
    }
  } else if (action == device_management::actions::kInstallAttributesCount) {
    if (!device_management_client->GetCountInstallAttributes()) {
      LOG(ERROR) << __func__ << ": failed to call GetCountInstallAttributes()";
      return EXIT_FAILURE;
    }
  } else if (action == device_management::actions::kInstallAttributesIsReady) {
    if (!device_management_client->IsReadyInstallAttributes()) {
      LOG(ERROR) << __func__ << ": failed to call IsReadyInstallAttributes()";
      return EXIT_FAILURE;
    }
  } else if (action == device_management::actions::kInstallAttributesIsSecure) {
    if (!device_management_client->IsSecureInstallAttributes()) {
      LOG(ERROR) << __func__ << ": failed to call IsSecureInstallAttributes()";
      return EXIT_FAILURE;
    }
  } else if (action ==
             device_management::actions::kInstallAttributesIsInvalid) {
    if (!device_management_client->IsInvalidInstallAttributes()) {
      LOG(ERROR) << __func__ << ": failed to call IsInvalidInstallAttributes()";
      return EXIT_FAILURE;
    }
  } else if (action ==
             device_management::actions::kInstallAttributesIsFirstInstall) {
    if (!device_management_client->IsFirstInstallInstallAttributes()) {
      LOG(ERROR) << __func__
                 << ": failed to call IsFirstInstallInstallAttributes()";
      return EXIT_FAILURE;
    }
  } else {
    LOG(ERROR) << "No matching action found. Check the usage message: "
               << device_management::actions::kUsage;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
