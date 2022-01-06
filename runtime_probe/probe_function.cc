// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/probe_function.h"

#include <memory>
#include <utility>

#include <base/check.h>
#include <base/json/json_reader.h>
#include <base/json/json_writer.h>
#include <base/logging.h>

#include "runtime_probe/system/context.h"

namespace runtime_probe {

using DataType = typename ProbeFunction::DataType;

ProbeFunction::ProbeFunction(base::Value&& raw_value) {}

std::unique_ptr<ProbeFunction> ProbeFunction::FromValue(const base::Value& dv) {
  if (!dv.is_dict()) {
    LOG(ERROR) << "ProbeFunction::FromValue takes a dictionary as parameter";
    return nullptr;
  }

  if (dv.DictSize() == 0) {
    LOG(ERROR) << "No function name found in the ProbeFunction dictionary";
    return nullptr;
  }

  if (dv.DictSize() > 1) {
    LOG(ERROR) << "More than 1 function names specified in the ProbeFunction"
                  " dictionary";
    return nullptr;
  }

  const auto& it = dv.DictItems().begin();

  // function_name is the only key exists in the dictionary */
  const auto& function_name = it->first;
  const auto& kwargs = it->second;

  if (registered_functions_.find(function_name) ==
      registered_functions_.end()) {
    // TODO(stimim): Should report an error.
    LOG(ERROR) << "Function \"" << function_name << "\" not found";
    return nullptr;
  }

  if (!kwargs.is_dict()) {
    // TODO(stimim): implement syntax sugar.
    LOG(ERROR) << "Function argument should be a dictionary";
    return nullptr;
  }

  return static_cast<std::unique_ptr<ProbeFunction>>(
      registered_functions_[function_name](kwargs));
}

PrivilegedProbeFunction::PrivilegedProbeFunction(base::Value&& raw_value)
    : raw_value_(std::move(raw_value)) {}

bool PrivilegedProbeFunction::InvokeHelper(std::string* result) const {
  std::string probe_statement_str;
  base::JSONWriter::Write(raw_value_, &probe_statement_str);

  return Context::Get()->helper_invoker()->Invoke(
      /*probe_function=*/this, probe_statement_str, result);
}

base::Optional<base::Value> PrivilegedProbeFunction::InvokeHelperToJSON()
    const {
  std::string raw_output;
  if (!InvokeHelper(&raw_output)) {
    return base::nullopt;
  }
  VLOG(3) << "InvokeHelper raw output:\n" << raw_output;
  return base::JSONReader::Read(raw_output);
}

int ProbeFunction::EvalInHelper(std::string* output) const {
  base::Value result = static_cast<base::Value>(EvalImpl());
  if (base::JSONWriter::Write(result, output))
    return 0;
  LOG(ERROR) << "Failed to serialize probed result to json string";
  return -1;
}

PrivilegedProbeFunction::DataType PrivilegedProbeFunction::Eval() const {
  auto json_output = InvokeHelperToJSON();
  if (!json_output) {
    LOG(ERROR) << "Failed to invoke helper.";
    return {};
  }
  if (!json_output->is_list()) {
    LOG(ERROR) << "Failed to parse json output as list.";
    return {};
  }

  DataType result = std::move(*json_output).TakeList();
  PostHelperEvalImpl(&result);
  return result;
}

}  // namespace runtime_probe
