// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTIONS_EC_COMPONENT_H_
#define RUNTIME_PROBE_FUNCTIONS_EC_COMPONENT_H_

#include <memory>
#include <optional>
#include <string>

#include <base/files/scoped_file.h>

#include "runtime_probe/probe_function.h"
#include "runtime_probe/probe_function_argument.h"
#include "runtime_probe/utils/ec_component_manifest.h"

namespace ec {
class GetVersionCommand;
class I2cReadCommand;
};  // namespace ec

namespace runtime_probe {

class EcComponentFunction : public PrivilegedProbeFunction {
  using PrivilegedProbeFunction::PrivilegedProbeFunction;

 public:
  NAME_PROBE_FUNCTION("ec_component");

 private:
  // PrivilegedProbeFunction overrides.
  bool PostParseArguments() final;
  DataType EvalImpl() const override;

  // Virtuals for testing.
  virtual base::ScopedFD GetEcDevice() const;
  virtual std::unique_ptr<ec::I2cReadCommand> GetI2cReadCommand(
      uint8_t port, uint8_t addr8, uint8_t offset, uint8_t read_len) const;
  virtual std::unique_ptr<ec::GetVersionCommand> GetGetVersionCommand() const;

  std::optional<std::string> GetCurrentECVersion(
      const base::ScopedFD& ec_dev_fd) const;

  bool IsValidComponent(const EcComponentManifest::Component& comp,
                        const base::ScopedFD& ec_dev_fd) const;

  PROBE_FUNCTION_ARG_DEF(std::optional<std::string>, type);
  PROBE_FUNCTION_ARG_DEF(std::optional<std::string>, name);
  PROBE_FUNCTION_ARG_DEF(std::optional<std::string>, manifest_path);
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_EC_COMPONENT_H_
