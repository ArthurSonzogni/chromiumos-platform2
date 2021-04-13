// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <pcrecpp.h>

#include <string>
#include <utility>
#include <vector>

#include <base/process/launch.h>
#include <base/values.h>

#include "runtime_probe/functions/ectool_i2cread.h"

using std::to_string;

namespace runtime_probe {

namespace {
constexpr auto kEctoolBinaryPath = "/usr/sbin/ectool";
constexpr auto kEctoolSubcommand = "i2cread";
constexpr auto kRegexPattern =
    R"(^Read from I2C port [\d]+ at .* offset .* = (.+)$)";
}  // namespace

EctoolI2Cread::DataType EctoolI2Cread::EvalImpl() const {
  DataType result{};

  std::string ectool_output;
  if (!GetEctoolOutput(&ectool_output))
    return {};

  pcrecpp::RE re(kRegexPattern);
  std::string reg_value;
  if (re.PartialMatch(ectool_output, &reg_value)) {
    base::Value dict_value(base::Value::Type::DICTIONARY);
    dict_value.SetStringKey(key_, reg_value);
    result.push_back(std::move(dict_value));
  }
  return result;
}

bool EctoolI2Cread::GetEctoolOutput(std::string* output) const {
  std::vector<std::string> ectool_cmd_arg{
      kEctoolBinaryPath, kEctoolSubcommand, to_string(size_),
      to_string(port_),  to_string(addr_),  to_string(offset_)};

  return base::GetAppOutput(ectool_cmd_arg, output);
}

}  // namespace runtime_probe
