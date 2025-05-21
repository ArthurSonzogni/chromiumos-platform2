// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HARDWARE_VERIFIER_FACTORY_HWID_PROCESSOR_H_
#define HARDWARE_VERIFIER_FACTORY_HWID_PROCESSOR_H_

#include <map>

#include <runtime_probe/proto_bindings/runtime_probe.pb.h>

namespace hardware_verifier {

template <typename ValueType>
using CategoryMapping =
    std::map<runtime_probe::ProbeRequest_SupportCategory, ValueType>;

class FactoryHWIDProcessor {
 public:
  virtual ~FactoryHWIDProcessor() = default;
};

}  // namespace hardware_verifier

#endif  // HARDWARE_VERIFIER_FACTORY_HWID_PROCESSOR_H_
