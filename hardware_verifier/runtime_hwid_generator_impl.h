// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HARDWARE_VERIFIER_RUNTIME_HWID_GENERATOR_IMPL_H_
#define HARDWARE_VERIFIER_RUNTIME_HWID_GENERATOR_IMPL_H_

#include <memory>
#include <optional>
#include <set>
#include <string>

#include <runtime_probe/proto_bindings/runtime_probe.pb.h>

#include "hardware_verifier/factory_hwid_processor.h"
#include "hardware_verifier/runtime_hwid_generator.h"

namespace hardware_verifier {

class RuntimeHWIDGeneratorImpl : public RuntimeHWIDGenerator {
 public:
  RuntimeHWIDGeneratorImpl(const RuntimeHWIDGeneratorImpl&) = delete;
  RuntimeHWIDGeneratorImpl& operator=(const RuntimeHWIDGeneratorImpl&) = delete;

  // Factory method to create a |RuntimeHWIDGeneratorImpl|.
  // Returns |nullptr| if initialization fails.
  static std::unique_ptr<RuntimeHWIDGeneratorImpl> Create();

  bool ShouldGenerateRuntimeHWID(
      const runtime_probe::ProbeResult& probe_result) const override;

  std::optional<std::string> Generate(
      const runtime_probe::ProbeResult& probe_result) const override;

  bool GenerateToDevice(
      const runtime_probe::ProbeResult& probe_result) const override;

 protected:
  explicit RuntimeHWIDGeneratorImpl(
      std::unique_ptr<FactoryHWIDProcessor> factory_hwid_processor,
      const std::set<runtime_probe::ProbeRequest_SupportCategory>&
          waived_categories);

 private:
  std::unique_ptr<FactoryHWIDProcessor> factory_hwid_processor_;
  const std::set<runtime_probe::ProbeRequest_SupportCategory>
      waived_categories_;
};

}  // namespace hardware_verifier

#endif  // HARDWARE_VERIFIER_RUNTIME_HWID_GENERATOR_IMPL_H_
