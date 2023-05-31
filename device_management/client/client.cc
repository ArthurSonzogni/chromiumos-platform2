// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_management/client/client.h"
#include "device_management/common/print_device_management_interface_proto.h"
#include "device_management/proto_bindings/device_management_interface.pb.h"
#include "device_management-client/device_management/dbus-proxies.h"

#include <utility>

namespace device_management {
namespace switches {
constexpr char kAttrNameSwitch[] = "name";
constexpr char kAttrValueSwitch[] = "value";
constexpr char kDevKeyHashSwitch[] = "developer_key_hash";
constexpr char kFlagsSwitch[] = "flags";
constexpr char kOutputFormatSwitch[] = "output-format";
constexpr struct {
  const char* name;
  const OutputFormat format;
} kOutputFormats[] = {{"default", OutputFormat::kDefault},
                      {"binary-protobuf", OutputFormat::kBinaryProtobuf}};
}  // namespace switches

namespace {
// Converts a brillo::Error* to string for printing.
std::string BrilloErrorToString(brillo::Error* err) {
  std::string result;
  if (err) {
    result = "(" + err->GetDomain() + ", " + err->GetCode() + ", " +
             err->GetMessage() + ")";
  } else {
    result = "(null)";
  }
  return result;
}

bool GetAttrName(std::unique_ptr<Printer>& printer,
                 const base::CommandLine* cl,
                 std::string* name_out) {
  *name_out = cl->GetSwitchValueASCII(switches::kAttrNameSwitch);

  if (name_out->length() == 0) {
    printer->PrintHumanOutput(
        "No install attribute name specified (--name=<name>)\n");
    return false;
  }
  return true;
}

bool GetAttrValue(std::unique_ptr<Printer>& printer,
                  const base::CommandLine* cl,
                  std::string* value_out) {
  *value_out = cl->GetSwitchValueASCII(switches::kAttrValueSwitch);

  if (value_out->length() == 0) {
    printer->PrintHumanOutput(
        "No install attribute value specified (--value=<value>)\n");
    return false;
  }
  return true;
}
}  // namespace

DeviceManagementClient::DeviceManagementClient(
    std::unique_ptr<org::chromium::DeviceManagementProxy>
        device_management_proxy,
    scoped_refptr<dbus::Bus> bus)
    : device_management_proxy_(std::move(device_management_proxy)), bus_(bus) {}

DeviceManagementClient::~DeviceManagementClient() {
  bus_->ShutdownAndBlock();
}

std::unique_ptr<DeviceManagementClient>
DeviceManagementClient::CreateDeviceManagementClient() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus = new dbus::Bus(options);
  if (!bus->Connect()) {
    LOG(ERROR) << "D-Bus system bus is not ready";
    return nullptr;
  }

  auto device_management_proxy =
      std::make_unique<org::chromium::DeviceManagementProxy>(bus);

  return std::unique_ptr<DeviceManagementClient>(
      new DeviceManagementClient(std::move(device_management_proxy), bus));
}

bool DeviceManagementClient::InitializePrinter(const base::CommandLine* cl) {
  // Use output format to construct a printer. We process this argument first
  // so that we can use the resulting printer for outputting errors when
  // processing any of the other arguments.
  OutputFormat output_format = OutputFormat::kDefault;
  if (cl->HasSwitch(switches::kOutputFormatSwitch)) {
    std::string output_format_str =
        cl->GetSwitchValueASCII(switches::kOutputFormatSwitch);
    std::optional<OutputFormat> found_output_format;
    for (const auto& value : switches::kOutputFormats) {
      if (output_format_str == value.name) {
        found_output_format = value.format;
        break;
      }
    }
    if (found_output_format) {
      output_format = *found_output_format;
    } else {
      // Do manual output here because we don't have a working printer.
      std::cerr << "Invalid output format: " << output_format_str << std::endl;
      return false;
    }
  }
  printer_ = std::make_unique<Printer>(output_format);
  return true;
}

bool DeviceManagementClient::IsInstallAttributesReady() {
  device_management::InstallAttributesGetStatusRequest status_request;
  device_management::InstallAttributesGetStatusReply status_reply;
  brillo::ErrorPtr error;
  if (!device_management_proxy_->InstallAttributesGetStatus(
          status_request, &status_reply, &error, timeout_ms) ||
      error) {
    printer_->PrintFormattedHumanOutput(
        "InstallAttributesGetStatus call failed: %s.\n",
        BrilloErrorToString(error.get()).c_str());
    return false;
  }

  if (status_reply.state() ==
          device_management::InstallAttributesState::UNKNOWN ||
      status_reply.state() ==
          device_management::InstallAttributesState::TPM_NOT_OWNED) {
    printer_->PrintHumanOutput("InstallAttributes() is not ready.\n");
    return false;
  }
  return true;
}

bool DeviceManagementClient::GetInstallAttributes(const base::CommandLine* cl) {
  std::string name;
  if (!GetAttrName(printer_, cl, &name)) {
    printer_->PrintHumanOutput("No attribute name specified.\n");
    return false;
  }
  // Make sure install attributes are ready.
  if (!IsInstallAttributesReady()) {
    return false;
  }

  device_management::InstallAttributesGetRequest request;
  device_management::InstallAttributesGetReply reply;
  brillo::ErrorPtr error;
  request.set_name(name);
  error.reset();
  if (!device_management_proxy_->InstallAttributesGet(request, &reply, &error,
                                                      timeout_ms) ||
      error) {
    printer_->PrintFormattedHumanOutput(
        "InstallAttributesGet call failed: %s.\n",
        BrilloErrorToString(error.get()).c_str());
    return false;
  }
  if (reply.error() == device_management::DeviceManagementErrorCode::
                           DEVICE_MANAGEMENT_ERROR_NOT_SET) {
    printer_->PrintFormattedHumanOutput("%s\n", reply.value().c_str());
  } else {
    return false;
  }
  return true;
}

bool DeviceManagementClient::SetInstallAttributes(const base::CommandLine* cl) {
  std::string name;
  if (!GetAttrName(printer_, cl, &name)) {
    printer_->PrintHumanOutput("No attribute name specified.\n");
    return false;
  }
  std::string value;
  if (!GetAttrValue(printer_, cl, &value)) {
    printer_->PrintHumanOutput("No attribute value specified.\n");
    return false;
  }
  // Make sure install attributes are ready.
  if (!IsInstallAttributesReady()) {
    return false;
  }

  device_management::InstallAttributesSetRequest req;
  device_management::InstallAttributesSetReply reply;
  brillo::ErrorPtr error;
  req.set_name(name);
  // It is expected that a null terminator is part of the value.
  value.push_back('\0');
  req.set_value(value);
  error.reset();
  if (!device_management_proxy_->InstallAttributesSet(req, &reply, &error,
                                                      timeout_ms) ||
      error) {
    printer_->PrintFormattedHumanOutput(
        "InstallAttributesSet call failed: %s.\n",
        BrilloErrorToString(error.get()).c_str());
    return false;
  }
  if (reply.error() != device_management::DeviceManagementErrorCode::
                           DEVICE_MANAGEMENT_ERROR_NOT_SET) {
    printer_->PrintHumanOutput("Call to InstallAttributesSet() failed.\n");
    return false;
  }
  return true;
}

bool DeviceManagementClient::FinalizeInstallAttributes() {
  // Make sure install attributes are ready.
  if (!IsInstallAttributesReady()) {
    return false;
  }

  device_management::InstallAttributesFinalizeRequest req;
  device_management::InstallAttributesFinalizeReply reply;
  brillo::ErrorPtr error;
  error.reset();
  if (!device_management_proxy_->InstallAttributesFinalize(req, &reply, &error,
                                                           timeout_ms) ||
      error) {
    printer_->PrintFormattedHumanOutput(
        "InstallAttributesFinalize() failed: %s.\n",
        BrilloErrorToString(error.get()).c_str());
    return false;
  }
  bool result = reply.error() == device_management::DeviceManagementErrorCode::
                                     DEVICE_MANAGEMENT_ERROR_NOT_SET;
  printer_->PrintFormattedHumanOutput("InstallAttributesFinalize(): %d\n",
                                      static_cast<int>(result));
  return true;
}

bool DeviceManagementClient::GetStatusInstallAttributes() {
  device_management::InstallAttributesGetStatusRequest request;
  device_management::InstallAttributesGetStatusReply reply;
  brillo::ErrorPtr error;
  if (!device_management_proxy_->InstallAttributesGetStatus(
          request, &reply, &error, timeout_ms) ||
      error) {
    printer_->PrintFormattedHumanOutput(
        "InstallAttributesGetStatus() call failed: %s.\n",
        BrilloErrorToString(error.get()).c_str());
    return false;
  }
  if (reply.error() != device_management::DeviceManagementErrorCode::
                           DEVICE_MANAGEMENT_ERROR_NOT_SET) {
    printer_->PrintHumanOutput(
        "Call to InstallAttributesGetStatus() failed.\n");
    return false;
  }
  printer_->PrintFormattedHumanOutput(
      "%s\n", InstallAttributesState_Name(reply.state()).c_str());
  return true;
}

bool DeviceManagementClient::GetCountInstallAttributes() {
  device_management::InstallAttributesGetStatusRequest req;
  device_management::InstallAttributesGetStatusReply reply;
  brillo::ErrorPtr error;
  if (!device_management_proxy_->InstallAttributesGetStatus(req, &reply, &error,
                                                            timeout_ms) ||
      error) {
    printer_->PrintFormattedHumanOutput(
        "InstallAttributesGetStatus() call failed: %s.\n",
        BrilloErrorToString(error.get()).c_str());
    return false;
  }
  if (reply.error() != device_management::DeviceManagementErrorCode::
                           DEVICE_MANAGEMENT_ERROR_NOT_SET) {
    printer_->PrintHumanOutput(
        "Call to InstallAttributesGetStatus() failed.\n");
    return false;
  }
  printer_->PrintFormattedHumanOutput("InstallAttributesCount(): %d\n",
                                      reply.count());
  return true;
}

bool DeviceManagementClient::IsReadyInstallAttributes() {
  device_management::InstallAttributesGetStatusRequest req;
  device_management::InstallAttributesGetStatusReply reply;
  brillo::ErrorPtr error;
  if (!device_management_proxy_->InstallAttributesGetStatus(req, &reply, &error,
                                                            timeout_ms) ||
      error) {
    printer_->PrintFormattedHumanOutput(
        "InstallAttributesGetStatus() call failed: %s.\n",
        BrilloErrorToString(error.get()).c_str());
    return false;
  }
  if (reply.error() != device_management::DeviceManagementErrorCode::
                           DEVICE_MANAGEMENT_ERROR_NOT_SET) {
    printer_->PrintHumanOutput(
        "Call to InstallAttributesGetStatus() failed.\n");
    return false;
  }

  bool result =
      (reply.state() != device_management::InstallAttributesState::UNKNOWN &&
       reply.state() !=
           device_management::InstallAttributesState::TPM_NOT_OWNED);
  printer_->PrintFormattedHumanOutput("InstallAttributesIsReady(): %d\n",
                                      static_cast<int>(result));
  return true;
}

bool DeviceManagementClient::IsSecureInstallAttributes() {
  device_management::InstallAttributesGetStatusRequest req;
  device_management::InstallAttributesGetStatusReply reply;
  brillo::ErrorPtr error;
  if (!device_management_proxy_->InstallAttributesGetStatus(req, &reply, &error,
                                                            timeout_ms) ||
      error) {
    printer_->PrintFormattedHumanOutput(
        "InstallAttributesGetStatus() call failed: %s.\n",
        BrilloErrorToString(error.get()).c_str());
    return false;
  }
  if (reply.error() != device_management::DeviceManagementErrorCode::
                           DEVICE_MANAGEMENT_ERROR_NOT_SET) {
    printer_->PrintHumanOutput(
        "Call to InstallAttributesGetStatus() failed.\n");
    return false;
  }

  bool result = reply.is_secure();
  printer_->PrintFormattedHumanOutput("InstallAttributesIsSecure(): %d\n",
                                      static_cast<int>(result));
  return true;
}

bool DeviceManagementClient::IsInvalidInstallAttributes() {
  device_management::InstallAttributesGetStatusRequest req;
  device_management::InstallAttributesGetStatusReply reply;
  brillo::ErrorPtr error;
  if (!device_management_proxy_->InstallAttributesGetStatus(req, &reply, &error,
                                                            timeout_ms) ||
      error) {
    printer_->PrintFormattedHumanOutput(
        "InstallAttributesGetStatus() call failed: %s.\n",
        BrilloErrorToString(error.get()).c_str());
    return false;
  }
  if (reply.error() != device_management::DeviceManagementErrorCode::
                           DEVICE_MANAGEMENT_ERROR_NOT_SET) {
    printer_->PrintHumanOutput(
        "Call to InstallAttributesGetStatus() failed.\n");
    return false;
  }

  bool result =
      (reply.state() == device_management::InstallAttributesState::INVALID);
  printer_->PrintFormattedHumanOutput("InstallAttributesIsInvalid(): %d\n",
                                      static_cast<int>(result));
  return true;
}

bool DeviceManagementClient::IsFirstInstallInstallAttributes() {
  device_management::InstallAttributesGetStatusRequest req;
  device_management::InstallAttributesGetStatusReply reply;
  brillo::ErrorPtr error;
  if (!device_management_proxy_->InstallAttributesGetStatus(req, &reply, &error,
                                                            timeout_ms) ||
      error) {
    printer_->PrintFormattedHumanOutput(
        "InstallAttributesGetStatus() call failed: %s.\n",
        BrilloErrorToString(error.get()).c_str());
    return false;
  }
  if (reply.error() != device_management::DeviceManagementErrorCode::
                           DEVICE_MANAGEMENT_ERROR_NOT_SET) {
    printer_->PrintHumanOutput(
        "Call to InstallAttributesGetStatus() failed.\n");
    return false;
  }
  bool result = (reply.state() ==
                 device_management::InstallAttributesState::FIRST_INSTALL);

  printer_->PrintFormattedHumanOutput("InstallAttributesIsFirstInstall(): %d\n",
                                      static_cast<int>(result));
  return true;
}

bool DeviceManagementClient::GetFWMP() {
  base::ElapsedTimer timer;
  device_management::GetFirmwareManagementParametersRequest request;
  device_management::GetFirmwareManagementParametersReply reply;
  brillo::ErrorPtr error;

  if (!device_management_proxy_->GetFirmwareManagementParameters(
          request, &reply, &error, timeout_ms) ||
      error) {
    printer_->PrintFormattedHumanOutput(
        "Failed to call GetFirmwareManagementParameters: %s\n",
        BrilloErrorToString(error.get()).c_str());
    return false;
  } else {
    printer_->PrintReplyProtobuf(reply);
    if (reply.error() != device_management::DeviceManagementErrorCode::
                             DEVICE_MANAGEMENT_ERROR_NOT_SET) {
      printer_->PrintFormattedHumanOutput(
          "Failed to call GetFirmwareManagementParameters: status %d\n",
          static_cast<int>(reply.error()));
      return false;
    }
  }

  printer_->PrintFormattedHumanOutput("flags=0x%08x\n", reply.fwmp().flags());
  brillo::Blob hash = brillo::BlobFromString(reply.fwmp().developer_key_hash());
  printer_->PrintFormattedHumanOutput(
      "hash=%s\n", hwsec_foundation::BlobToHex(hash).c_str());
  printer_->PrintHumanOutput("GetFirmwareManagementParameters success.\n");
  return true;
}

bool DeviceManagementClient::SetFWMP(const base::CommandLine* cl) {
  base::ElapsedTimer timer;
  device_management::SetFirmwareManagementParametersRequest request;
  device_management::SetFirmwareManagementParametersReply reply;

  if (cl->HasSwitch(switches::kFlagsSwitch)) {
    std::string flags_str = cl->GetSwitchValueASCII(switches::kFlagsSwitch);
    char* end = NULL;
    int32_t flags = strtol(flags_str.c_str(), &end, 0);
    if (end && *end != '\0') {
      printer_->PrintHumanOutput("Bad flags value.\n");
      return false;
    }
    request.mutable_fwmp()->set_flags(flags);
  } else {
    printer_->PrintHumanOutput(
        "Use --flags (and optionally --developer_key_hash).\n");
    return false;
  }

  if (cl->HasSwitch(switches::kDevKeyHashSwitch)) {
    std::string hash_str = cl->GetSwitchValueASCII(switches::kDevKeyHashSwitch);
    brillo::Blob hash;
    if (!base::HexStringToBytes(hash_str, &hash)) {
      printer_->PrintHumanOutput("Bad hash value.\n");
      return false;
    }
    if (hash.size() != SHA256_DIGEST_LENGTH) {
      printer_->PrintHumanOutput("Bad hash size.\n");
      return false;
    }

    request.mutable_fwmp()->set_developer_key_hash(brillo::BlobToString(hash));
  }

  brillo::ErrorPtr error;

  if (!device_management_proxy_->SetFirmwareManagementParameters(
          request, &reply, &error, timeout_ms) ||
      error) {
    printer_->PrintFormattedHumanOutput(
        "Failed to call SetFirmwareManagementParameters: %s\n",
        BrilloErrorToString(error.get()).c_str());
    return false;
  } else {
    printer_->PrintReplyProtobuf(reply);
    if (reply.error() != device_management::DeviceManagementErrorCode::
                             DEVICE_MANAGEMENT_ERROR_NOT_SET) {
      printer_->PrintFormattedHumanOutput(
          "Failed to call SetFirmwareManagementParameters: status %d\n",
          static_cast<int>(reply.error()));
      return false;
    }
  }

  printer_->PrintHumanOutput("SetFirmwareManagementParameters success.\n");
  return true;
}

bool DeviceManagementClient::RemoveFWMP() {
  base::ElapsedTimer timer;
  device_management::RemoveFirmwareManagementParametersRequest request;
  device_management::RemoveFirmwareManagementParametersReply reply;
  brillo::ErrorPtr error;

  if (!device_management_proxy_->RemoveFirmwareManagementParameters(
          request, &reply, &error, timeout_ms) ||
      error) {
    printer_->PrintFormattedHumanOutput(
        "Failed to call RemoveFirmwareManagementParameters: %s\n",
        BrilloErrorToString(error.get()).c_str());
    return false;
  } else {
    printer_->PrintReplyProtobuf(reply);
    if (reply.error() != device_management::DeviceManagementErrorCode::
                             DEVICE_MANAGEMENT_ERROR_NOT_SET) {
      printer_->PrintFormattedHumanOutput(
          "Failed to call RemoveFirmwareManagementParameters: status %d\n",
          static_cast<int>(reply.error()));
      return false;
    }
  }

  printer_->PrintHumanOutput("RemoveFirmwareManagementParameters success.\n");
  return true;
}

}  // namespace device_management
