// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/component_category.h"

#include <memory>
#include <utility>

#include <base/barrier_callback.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <base/values.h>
#include <brillo/map_utils.h>

#include "runtime_probe/probe_statement.h"

namespace runtime_probe {

namespace {

// Callback to handle a single result from |ProbeStatement::EvalAsync|.
void OnProbeStatementEvalCompleted(
    base::OnceCallback<void(base::Value::List)> callback,
    const std::string& component_name,
    std::optional<base::Value> information_dv,
    base::Value::List probe_result) {
  base::Value::List results;
  for (auto& probe_statement_dv : probe_result) {
    base::Value::Dict result;
    result.Set("name", component_name);
    result.Set("values", std::move(probe_statement_dv));
    if (information_dv.has_value())
      result.Set("information", information_dv->Clone());
    results.Append(std::move(result));
  }
  std::move(callback).Run(std::move(results));
}

void CollectProbeStatementResults(
    base::OnceCallback<void(base::Value::List)> callback,
    std::vector<base::Value::List> probe_results) {
  base::Value::List results;
  for (auto& probe_result : probe_results) {
    for (auto& result : probe_result) {
      results.Append(std::move(result));
    }
  }
  std::move(callback).Run(std::move(results));
}

}  // namespace

std::unique_ptr<ComponentCategory> ComponentCategory::FromValue(
    const std::string& category_name, const base::Value& dv) {
  if (!dv.is_dict()) {
    LOG(ERROR) << "ComponentCategory::FromValue takes a dictionary as"
               << " parameter";
    return nullptr;
  }

  std::unique_ptr<ComponentCategory> instance{new ComponentCategory()};
  instance->category_name_ = category_name;

  for (const auto& entry : dv.GetDict()) {
    const auto& component_name = entry.first;
    const auto& value = entry.second;
    auto probe_statement = ProbeStatement::FromValue(component_name, value);
    if (!probe_statement) {
      LOG(ERROR) << "Component " << component_name
                 << " doesn't contain a valid probe statement.";
      return nullptr;
    }
    instance->component_[component_name] = std::move(probe_statement);
  }

  return instance;
}

void ComponentCategory::Eval(
    base::OnceCallback<void(base::Value::List)> callback) const {
  auto barrier_callback = base::BarrierCallback<base::Value::List>(
      component_.size(),
      base::BindOnce(&CollectProbeStatementResults, std::move(callback)));
  for (auto& [component_name, probe_statement] : component_)
    probe_statement->Eval(base::BindOnce(&OnProbeStatementEvalCompleted,
                                         barrier_callback, component_name,
                                         probe_statement->GetInformation()));
}

std::vector<std::string> ComponentCategory::GetComponentNames() const {
  return brillo::GetMapKeysAsVector(component_);
}

}  // namespace runtime_probe
