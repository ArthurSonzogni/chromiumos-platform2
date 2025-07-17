// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HARDWARE_VERIFIER_RUNTIME_HWID_GENERATOR_H_
#define HARDWARE_VERIFIER_RUNTIME_HWID_GENERATOR_H_

#include <optional>
#include <string>

#include <runtime_probe/proto_bindings/runtime_probe.pb.h>

namespace hardware_verifier {

constexpr char kRuntimeHWIDFilePath[] =
    "var/cache/hardware_verifier/runtime_hwid";

class RuntimeHWIDGenerator {
 public:
  virtual ~RuntimeHWIDGenerator() = default;

  // Returns a bool indicating if Runtime HWID should be generated based on the
  // Runtime Probe result and the Factory HWID decode result.
  virtual bool ShouldGenerateRuntimeHWID(
      const runtime_probe::ProbeResult& probe_result) const = 0;

  // Generates the Runtime HWID string based on the probe result.
  // Returns |std::nullopt| if generation fails.
  virtual std::optional<std::string> Generate(
      const runtime_probe::ProbeResult& probe_result) const = 0;

  // Generates the Runtime HWID string based on the probe result, and writes the
  // Runtime HWID and its checksum to the `/var/cache/runtime_hwid` file on the
  // device.
  virtual bool GenerateToDevice(
      const runtime_probe::ProbeResult& probe_result) const = 0;
};

}  // namespace hardware_verifier

#endif  // HARDWARE_VERIFIER_RUNTIME_HWID_GENERATOR_H_
