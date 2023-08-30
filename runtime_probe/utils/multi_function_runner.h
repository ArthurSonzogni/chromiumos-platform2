// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_UTILS_MULTI_FUNCTION_RUNNER_H_
#define RUNTIME_PROBE_UTILS_MULTI_FUNCTION_RUNNER_H_

#include <memory>
#include <utility>
#include <vector>

#include <base/functional/callback.h>
#include <base/values.h>

namespace runtime_probe {

class ProbeFunction;

// A collection of probe functions that merge the results into one probe result.
class MultiFunctionRunner {
 public:
  // Append a probe function to the runner.
  void AddFunction(std::unique_ptr<ProbeFunction> probe_function) {
    functions_.push_back(std::move(probe_function));
  }

  // Run all probe functions in the runner, and the callback will receive the
  // collected results.
  void Run(base::OnceCallback<void(base::Value::List)> callback) const;

  // Return if all functions in the runner are valid.
  bool IsValid() const;

 private:
  std::vector<std::unique_ptr<ProbeFunction>> functions_;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_UTILS_MULTI_FUNCTION_RUNNER_H_
