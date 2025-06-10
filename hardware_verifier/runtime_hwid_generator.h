// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HARDWARE_VERIFIER_RUNTIME_HWID_GENERATOR_H_
#define HARDWARE_VERIFIER_RUNTIME_HWID_GENERATOR_H_

#include <memory>
#include <set>

#include <runtime_probe/proto_bindings/runtime_probe.pb.h>

#include "hardware_verifier/factory_hwid_processor.h"
#include "hardware_verifier/hardware_verifier.pb.h"

namespace hardware_verifier {

class RuntimeHWIDGenerator {
 public:
  RuntimeHWIDGenerator(const RuntimeHWIDGenerator&) = delete;
  RuntimeHWIDGenerator& operator=(const RuntimeHWIDGenerator&) = delete;

  // Factory method to create a |RuntimeHWIDGenerator|.
  // Returns |nullptr| if initialization fails.
  static std::unique_ptr<RuntimeHWIDGenerator> Create(
      std::unique_ptr<FactoryHWIDProcessor> factory_hwid_processor,
      const EncodingSpec& encoding_spec);

  // Returns a bool indicating if Runtime HWID should be generated based on the
  // Runtime Probe result and the Factory HWID decode result.
  bool ShouldGenerateRuntimeHWID(
      const runtime_probe::ProbeResult& probe_result) const;

 private:
  explicit RuntimeHWIDGenerator(
      std::unique_ptr<FactoryHWIDProcessor> factory_hwid_processor,
      const std::set<runtime_probe::ProbeRequest_SupportCategory>&
          waived_categories);
  std::unique_ptr<FactoryHWIDProcessor> factory_hwid_processor_;
  const std::set<runtime_probe::ProbeRequest_SupportCategory>
      waived_categories_;
};

}  // namespace hardware_verifier

#endif  // HARDWARE_VERIFIER_RUNTIME_HWID_GENERATOR_H_
