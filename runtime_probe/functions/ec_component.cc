// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/ec_component.h"

#include <fcntl.h>

#include <memory>
#include <string>
#include <vector>

#include <base/files/scoped_file.h>
#include <base/values.h>
#include <libec/i2c_read_command.h>

#include "runtime_probe/utils/ec_component_manifest.h"

namespace runtime_probe {

namespace {
constexpr int kEcCmdNumAttempts = 10;
}  // namespace

base::ScopedFD EcComponentFunction::GetEcDevice() const {
  return base::ScopedFD(open(ec::kCrosEcPath, O_RDWR));
}

std::vector<EcComponentManifest::Component>
EcComponentFunction::GetComponentCandidates(
    std::optional<std::string> type, std::optional<std::string> name) const {
  std::vector<EcComponentManifest::Component> candidates;
  auto manifest = EcComponentManifestReader::Read();
  if (!manifest) {
    LOG(ERROR) << "Get component manifest failed.";
    return {};
  }
  for (const auto& comp : manifest->component_list) {
    if ((!type || comp.component_type == type) &&
        (!name || comp.component_name == name)) {
      candidates.push_back(comp);
    }
  }
  return candidates;
}

std::unique_ptr<ec::I2cReadCommand> EcComponentFunction::GetI2cReadCommand(
    uint8_t port, uint8_t addr8, uint8_t offset, uint8_t read_len) const {
  return ec::I2cReadCommand::Create(port, addr8, offset, read_len);
}

bool EcComponentFunction::IsValidComponent(
    const EcComponentManifest::Component& comp, int ec_dev_fd) const {
  // |addr| in component manifest is a 7-bit address, where
  // ec::I2cReadCommand::Create() takes 8-bit address, so we convert addresses
  // accordingly.
  const int addr8 = comp.i2c.addr << 1;
  if (comp.i2c.expect.size() == 0) {
    // No expect value. Just verify the accessibility of the component.
    auto cmd = GetI2cReadCommand(comp.i2c.port, addr8, 0u, 1u);
    if (cmd && cmd->RunWithMultipleAttempts(ec_dev_fd, kEcCmdNumAttempts) &&
        !cmd->I2cStatus()) {
      return true;
    }
  }
  for (const auto& expect : comp.i2c.expect) {
    auto cmd = GetI2cReadCommand(comp.i2c.port, addr8, expect.reg, 1u);
    if (cmd && cmd->RunWithMultipleAttempts(ec_dev_fd, kEcCmdNumAttempts) &&
        !cmd->I2cStatus()) {
      if (expect.value == cmd->Data()) {
        return true;
      }
    }
  }
  return false;
}

EcComponentFunction::DataType EcComponentFunction::EvalImpl() const {
  base::ScopedFD ec_dev = GetEcDevice();
  DataType result{};
  for (const auto& comp : GetComponentCandidates(type_, name_)) {
    if (IsValidComponent(comp, ec_dev.get())) {
      result.Append(base::Value::Dict()
                        .Set("component_type", comp.component_type)
                        .Set("component_name", comp.component_name));
    }
  }
  return result;
}

}  // namespace runtime_probe
