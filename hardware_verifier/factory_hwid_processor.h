// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HARDWARE_VERIFIER_FACTORY_HWID_PROCESSOR_H_
#define HARDWARE_VERIFIER_FACTORY_HWID_PROCESSOR_H_

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <runtime_probe/proto_bindings/runtime_probe.pb.h>

namespace hardware_verifier {

template <typename ValueType>
using CategoryMapping =
    std::map<runtime_probe::ProbeRequest_SupportCategory, ValueType>;

class FactoryHWIDProcessor {
 public:
  virtual ~FactoryHWIDProcessor() = default;

  // Decode the Factory HWID of the device, and returns a |CategoryMapping| that
  // maps component categories to the decoded component names.
  // If the decode fails, return |std::nullopt|.
  virtual std::optional<CategoryMapping<std::vector<std::string>>>
  DecodeFactoryHWID() const = 0;

  // Returns a set of component categories that should be skipped when
  // processing 0-bit components. A category is skipped if the position of its
  // first 0-bit is greater than the length of the Factory HWID component bits
  // (i.e. exceeds the component bits).
  virtual std::set<runtime_probe::ProbeRequest_SupportCategory>
  GetSkipZeroBitCategories() const = 0;

  // Returns a masked Factory HWID. This HWID will have the RACC-related
  // component bits masked out, while preserving other information (e.g. image
  // ID).
  // Returns |std::nullopt| on failure.
  virtual std::optional<std::string> GenerateMaskedFactoryHWID() const = 0;
};

}  // namespace hardware_verifier

#endif  // HARDWARE_VERIFIER_FACTORY_HWID_PROCESSOR_H_
