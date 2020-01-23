// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTION_TEMPLATES_NETWORK_H_
#define RUNTIME_PROBE_FUNCTION_TEMPLATES_NETWORK_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/optional.h>
#include <base/values.h>
#include <brillo/variant_dictionary.h>

#include "runtime_probe/probe_function.h"

namespace runtime_probe {

class NetworkFunction : public ProbeFunction {
 public:
  // Must be implemented by derived function to invoke the helper properly.
  std::string GetFunctionName() const override = 0;

  // Must be implemented by derived function instead of
  // ProbeFunction::FromDictionaryValue.
  static std::unique_ptr<ProbeFunction> FromDictionaryValue(
      const base::DictionaryValue& dict_value) = delete;

  // Override `Eval` function, which should return a list of DictionaryValue.
  DataType Eval() const final;

  int EvalInHelper(std::string* output) const override;

 protected:
  virtual std::string GetNetworkType() const = 0;

  // Eval the network indicated by |node_path| in runtime_probe_helper. Return
  // empty DictionaryValue if network type indicated by |node_path| does not
  // match the target type. On the other hand, if the network type matches the
  // target type, the return DictionaryValue must contain at least the
  // "bus_type" key.
  base::DictionaryValue EvalInHelperByPath(
      const base::FilePath& node_path) const;

  // Get paths of all physical network.
  std::vector<brillo::VariantDictionary> GetDevicesProps(
      base::Optional<std::string> type = base::nullopt) const;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTION_TEMPLATES_NETWORK_H_
