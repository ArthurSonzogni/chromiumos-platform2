// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <optional>
#include <vector>

#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <base/values.h>

#include "runtime_probe/probe_function.h"
#include "runtime_probe/probe_result_checker.h"
#include "runtime_probe/probe_statement.h"

namespace runtime_probe {

namespace {

void FilterValueByKey(base::Value* dv, const std::set<std::string>& keys) {
  std::vector<std::string> keys_to_delete;
  for (const auto& entry : dv->GetDict()) {
    if (keys.find(entry.first) == keys.end()) {
      keys_to_delete.push_back(entry.first);
    }
  }
  for (const auto& k : keys_to_delete) {
    dv->GetDict().Remove(k);
  }
}

}  // namespace

std::unique_ptr<ProbeStatement> ProbeStatement::FromValue(
    std::string component_name, const base::Value& dv) {
  if (!dv.is_dict()) {
    LOG(ERROR) << "ProbeStatement::FromValue takes a dictionary as parameter";
    return nullptr;
  }
  const auto& dict = dv.GetDict();

  // Parse required field "eval"
  const auto* eval_value = dict.Find("eval");
  if (!eval_value) {
    LOG(ERROR) << "\"eval\" does not exist.";
    return nullptr;
  }
  if (!eval_value->is_dict()) {
    LOG(ERROR) << "\"eval\" should be a dictionary.";
    return nullptr;
  }
  auto function = ProbeFunction::FromValue(*eval_value);
  if (!function) {
    LOG(ERROR) << "Component " << component_name
               << " doesn't contain a valid probe function.";
    return nullptr;
  }
  std::unique_ptr<ProbeStatement> instance{new ProbeStatement()};
  instance->component_name_ = component_name;
  instance->probe_function_ = std::move(function);

  // Parse optional field "keys"
  const auto* keys_value = dict.FindList("keys");
  if (!keys_value) {
    VLOG(3) << "\"keys\" does not exist or is not a list";
  } else {
    for (const auto& v : *keys_value) {
      // Currently, destroy all previously inserted valid elems
      if (!v.is_string()) {
        LOG(ERROR) << "\"keys\" should be a list of string: " << *keys_value;
        instance->key_.clear();
        break;
      }
      instance->key_.insert(v.GetString());
    }
  }

  // Parse optional field "expect"
  // TODO(b:121354690): Make expect useful
  const auto* expect_value = dict.Find("expect");
  if (!expect_value) {
    VLOG(3) << "\"expect\" does not exist.";
  } else {
    instance->probe_result_checker_ =
        ProbeResultChecker::FromValue(*expect_value);
    if (!instance->probe_result_checker_) {
      LOG(ERROR) << "Failed to parse \"expect\".";
      return nullptr;
    }
  }

  // Parse optional field "information"
  const auto* information = dict.FindDict("information");
  if (!information) {
    VLOG(3) << "\"information\" does not exist or is not a dictionary";
  } else {
    instance->information_ = base::Value(information->Clone());
  }

  return instance;
}

void ProbeStatement::Eval(
    base::OnceCallback<void(ProbeFunction::DataType)> callback) const {
  probe_function_->Eval(
      base::BindOnce(&ProbeStatement::OnProbeFunctionEvalCompleted,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void ProbeStatement::OnProbeFunctionEvalCompleted(
    base::OnceCallback<void(ProbeFunction::DataType)> callback,
    ProbeFunction::DataType results) const {
  if (!key_.empty()) {
    std::for_each(results.begin(), results.end(),
                  [this](auto& result) { FilterValueByKey(&result, key_); });
  }

  if (probe_result_checker_) {
    results.EraseIf([&](base::Value& result) {
      return !probe_result_checker_->Apply(&result);
    });
  }
  std::move(callback).Run(std::move(results));
}

}  // namespace runtime_probe
