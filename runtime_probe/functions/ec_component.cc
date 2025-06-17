// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/ec_component.h"

#include <fcntl.h>

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <base/values.h>
#include <libec/get_version_command.h>
#include <libec/i2c_passthru_command.h>

#include "runtime_probe/system/context.h"
#include "runtime_probe/utils/ec_component_manifest.h"

namespace runtime_probe {

namespace {
constexpr int kEcCmdNumAttempts = 10;

bool IsMatchExpect(EcComponentManifest::Component::I2c::Expect expect,
                   base::span<const uint8_t> resp_data) {
  if (expect.value->size() != resp_data.size()) {
    LOG(WARNING) << "The response data length is different from the expect "
                    "value length.";
    return false;
  }
  if (!expect.mask.has_value()) {
    return expect.value == resp_data;
  }

  for (int i = 0; i < resp_data.size(); i++) {
    if ((resp_data[i] & (*expect.mask)[i]) != (*expect.value)[i]) {
      return false;
    }
  }
  return true;
}

bool RunI2cCommandAndCheckSuccess(const base::ScopedFD& ec_dev_fd,
                                  ec::I2cPassthruCommand* cmd) {
  return cmd != nullptr &&
         cmd->RunWithMultipleAttempts(ec_dev_fd.get(), kEcCmdNumAttempts) &&
         !cmd->I2cStatus();
}

}  // namespace

base::ScopedFD EcComponentFunction::GetEcDevice() const {
  return base::ScopedFD(open(ec::kCrosEcPath, O_RDWR));
}

std::unique_ptr<ec::I2cPassthruCommand> EcComponentFunction::GetI2cReadCommand(
    uint8_t port,
    uint8_t addr7,
    uint8_t offset,
    const std::vector<uint8_t>& write_data,
    uint8_t read_len) const {
  std::vector<uint8_t> passthru_data(1, offset);
  passthru_data.insert(passthru_data.end(), write_data.begin(),
                       write_data.end());
  return ec::I2cPassthruCommand::Create(port, addr7, passthru_data, read_len);
}

std::unique_ptr<ec::GetVersionCommand>
EcComponentFunction::GetGetVersionCommand() const {
  return std::make_unique<ec::GetVersionCommand>();
}

std::optional<std::string> EcComponentFunction::GetCurrentECVersion(
    const base::ScopedFD& ec_dev_fd) const {
  auto cmd = GetGetVersionCommand();
  if (!cmd->RunWithMultipleAttempts(ec_dev_fd.get(), kEcCmdNumAttempts)) {
    LOG(ERROR) << "Failed to get EC version.";
    return std::nullopt;
  }
  switch (cmd->Image()) {
    case EC_IMAGE_UNKNOWN:
      LOG(ERROR) << "Got unknown EC image.";
      return std::nullopt;
    case EC_IMAGE_RO:
    case EC_IMAGE_RO_B:
      LOG(WARNING) << "EC is currently running RO image.";
      return cmd->ROVersion();
    case EC_IMAGE_RW:
    case EC_IMAGE_RW_B:
      return cmd->RWVersion();
  }
}

bool EcComponentFunction::IsValidComponent(
    const EcComponentManifest::Component& comp,
    const base::ScopedFD& ec_dev_fd) const {
  if (comp.i2c.expect.size() == 0) {
    // No expect value. Just verify the accessibility of the component.
    auto cmd = GetI2cReadCommand(comp.i2c.port, comp.i2c.addr, 0u, {}, 1u);
    return RunI2cCommandAndCheckSuccess(ec_dev_fd, cmd.get());
  }
  for (const auto& expect : comp.i2c.expect) {
    auto cmd = GetI2cReadCommand(comp.i2c.port, comp.i2c.addr, expect.reg,
                                 expect.write_data, expect.bytes);
    if (!RunI2cCommandAndCheckSuccess(ec_dev_fd, cmd.get())) {
      return false;
    }
    if (expect.value.has_value() && !IsMatchExpect(expect, cmd->RespData())) {
      return false;
    }
  }
  return true;
}

bool EcComponentFunction::PostParseArguments() {
  if (manifest_path_ && !Context::Get()->factory_mode()) {
    LOG(ERROR) << "manifest_path can only be set in factory_runtime_probe.";
    return false;
  }
  return true;
}

EcComponentFunction::DataType EcComponentFunction::EvalImpl() const {
  base::ScopedFD ec_dev = GetEcDevice();

  std::optional<EcComponentManifest> manifest;
  auto ec_version = GetCurrentECVersion(ec_dev);
  if (!ec_version) {
    LOG(ERROR) << "Get EC version failed.";
    return {};
  }

  auto ec_manifest_reader = EcComponentManifestReader(*ec_version);
  if (manifest_path_) {
    manifest = ec_manifest_reader.ReadFromFilePath(
        base::FilePath(manifest_path_.value()));
  } else {
    manifest = ec_manifest_reader.Read();
  }
  if (!manifest) {
    LOG(ERROR) << "Get component manifest failed.";
    return {};
  }

  DataType result{};
  for (const auto& comp : manifest->component_list) {
    // If type_ or name_ is set, skip those component which doesn't match the
    // specified type / name.
    if ((type_ && comp.component_type != type_) ||
        (name_ && comp.component_name != name_)) {
      continue;
    }
    if (IsValidComponent(comp, ec_dev)) {
      result.Append(base::Value::Dict()
                        .Set("component_type", comp.component_type)
                        .Set("component_name", comp.component_name));
    }
  }
  return result;
}

}  // namespace runtime_probe
