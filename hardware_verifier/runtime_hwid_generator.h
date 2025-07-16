// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HARDWARE_VERIFIER_RUNTIME_HWID_GENERATOR_H_
#define HARDWARE_VERIFIER_RUNTIME_HWID_GENERATOR_H_

#include <memory>
#include <optional>
#include <set>
#include <string>

#include <runtime_probe/proto_bindings/runtime_probe.pb.h>

#include "hardware_verifier/factory_hwid_processor.h"
#include "hardware_verifier/hardware_verifier.pb.h"

namespace hardware_verifier {

constexpr char kRuntimeHWIDFilePath[] =
    "var/cache/hardware_verifier/runtime_hwid";

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

  // Generates the Runtime HWID string based on the probe result.
  // Returns |std::nullopt| if generation fails.
  std::optional<std::string> Generate(
      const runtime_probe::ProbeResult& probe_result) const;

  // Generates the Runtime HWID string based on the probe result, and writes the
  // Runtime HWID and its checksum to the `/var/cache/runtime_hwid` file on the
  // device.
  bool GenerateToDevice(const runtime_probe::ProbeResult& probe_result) const;

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
