// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTION_TEMPLATES_NETWORK_H_
#define RUNTIME_PROBE_FUNCTION_TEMPLATES_NETWORK_H_

#include <memory>
#include <optional>
#include <string>

#include "runtime_probe/probe_function.h"
#include "runtime_probe/probe_function_argument.h"

namespace runtime_probe {
namespace internal {

bool IsValidNetworkType(const std::string& type);

}

// TODO(b/269822306): Move this class to //runtime_probe/functions after we
// remove all XXX_network functions.
class NetworkFunction : public PrivilegedProbeFunction {
  using PrivilegedProbeFunction::PrivilegedProbeFunction;

 public:
  NAME_PROBE_FUNCTION("network");

  template <typename T>
  static std::unique_ptr<T> FromKwargsValue(const base::Value& dict_value) {
    PARSE_BEGIN();
    PARSE_ARGUMENT(device_type, std::string(""));
    if (!internal::IsValidNetworkType(instance->device_type_)) {
      LOG(ERROR) << "Got an unexpected network type " << instance->device_type_;
      return nullptr;
    }
    PARSE_END();
  }

 protected:
  virtual std::optional<std::string> GetNetworkType() const;

 private:
  // PrivilegedProbeFunction overrides.
  bool PostParseArguments() final;
  DataType EvalImpl() const final;
  void PostHelperEvalImpl(DataType* helper_results) const final;

  // Type of the network device. Accepts all types if it is omitted.
  PROBE_FUNCTION_ARG_DEF(std::string, device_type, (std::string("")));
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTION_TEMPLATES_NETWORK_H_
