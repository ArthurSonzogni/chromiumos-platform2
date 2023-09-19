// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTIONS_EC_COMPONENT_H_
#define RUNTIME_PROBE_FUNCTIONS_EC_COMPONENT_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/files/scoped_file.h>

#include "runtime_probe/probe_function.h"
#include "runtime_probe/probe_function_argument.h"
#include "runtime_probe/utils/ec_component_manifest.h"

namespace ec {
class I2cReadCommand;
};

namespace runtime_probe {

class EcComponentFunction : public PrivilegedProbeFunction {
  using PrivilegedProbeFunction::PrivilegedProbeFunction;

 public:
  NAME_PROBE_FUNCTION("ec_component");

 private:
  // PrivilegedProbeFunction overrides.
  DataType EvalImpl() const override;

  virtual base::ScopedFD GetEcDevice() const;

  std::vector<EcComponentManifest::Component> GetComponentCandidates(
      std::optional<std::string> type, std::optional<std::string> name) const;

  virtual std::unique_ptr<ec::I2cReadCommand> GetI2cReadCommand(
      uint8_t port, uint8_t addr8, uint8_t offset, uint8_t read_len) const;

  bool IsValidComponent(const EcComponentManifest::Component&, int) const;

  PROBE_FUNCTION_ARG_DEF(std::optional<std::string>, type);
  PROBE_FUNCTION_ARG_DEF(std::optional<std::string>, name);
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_EC_COMPONENT_H_
