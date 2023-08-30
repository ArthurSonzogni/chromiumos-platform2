// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/utils/multi_function_runner.h"

#include <utility>
#include <vector>

#include <base/barrier_callback.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <base/values.h>

#include "runtime_probe/probe_function.h"

namespace runtime_probe {
namespace {

void CollectProbeResults(base::OnceCallback<void(base::Value::List)> callback,
                         std::vector<base::Value::List> probe_results) {
  base::Value::List results;
  for (auto& result : probe_results) {
    for (auto& value : result) {
      results.Append(std::move(value));
    }
  }
  std::move(callback).Run(std::move(results));
}

}  // namespace

void MultiFunctionRunner::Run(
    base::OnceCallback<void(base::Value::List)> callback) const {
  if (!IsValid()) {
    LOG(ERROR) << "MultiFunctionRunner contains invalid probe functions.";
    std::move(callback).Run(base::Value::List{});
    return;
  }
  auto barrier_callback = base::BarrierCallback<base::Value::List>(
      functions_.size(),
      base::BindOnce(&CollectProbeResults, std::move(callback)));
  for (auto& function : functions_)
    function->EvalAsync(barrier_callback);
}

bool MultiFunctionRunner::IsValid() const {
  for (auto& function : functions_) {
    if (!function) {
      return false;
    }
  }
  return true;
}

}  // namespace runtime_probe
