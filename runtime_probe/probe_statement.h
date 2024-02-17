// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_PROBE_STATEMENT_H_
#define RUNTIME_PROBE_PROBE_STATEMENT_H_

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <base/values.h>
#include <gtest/gtest.h>

#include "runtime_probe/probe_function.h"
#include "runtime_probe/probe_result_checker.h"

namespace runtime_probe {
class Matcher;

class ProbeStatement {
  // Holds a probe statement with following JSON schema::
  //   {
  //     "eval": <function_name:string> |
  //             <func:ProbeFunction> |
  //             [<func:ProbeFunction>],
  //     "keys": [<key:string>],
  //     "expect": <see |ProbeResultChecker|>,
  //     "information": <info:DictValue>,
  //   }
  //
  // For "eval", the case "[<func:ProbeFunction>]" will be transformed into::
  //   (ProbeFunction) {
  //     "function_name": "sequence",
  //     "args": {
  //       "functions": [<func:ProbeFunction>]
  //     }
  //   }
  //
  // For "expect", the dictionary value should represent a ProbeResultChecker
  // object.  See ProbeResultChecker for more details.
  //
  // When evaluating a ProbeStatement, the ProbeFunction defined by "eval" will
  // be called.  The results will be filtered / processed by "keys" and "expect"
  // rules.  See ProbeStatement::Eval for more details.
 public:
  virtual ~ProbeStatement();

  static std::unique_ptr<ProbeStatement> FromValue(std::string component_name,
                                                   const base::Value& dv);

  // Evaluate the probe statement.
  //
  // The process can be break into following steps:
  // - Call probe function |probe_function_|
  // - Filter results by |key_|  (if |key_| is not empty)
  // - Transform and check results by |probe_result_checker_|  (if
  //   |probe_result_checker_| is not empty)
  // - Return final results.
  virtual void Eval(
      base::OnceCallback<void(ProbeFunction::DataType)> callback) const;

  virtual std::optional<base::Value> GetInformation() const {
    if (information_)
      return information_->Clone();
    return std::nullopt;
  }

  // Gets pointer to the probe function or nullptr on failure.
  const ProbeFunction* probe_function() const { return probe_function_.get(); }

  // Set mocked probe function for testing.
  void SetProbeFunctionForTesting(
      std::unique_ptr<ProbeFunction> probe_function) {
    probe_function_ = std::move(probe_function);
  }

 protected:
  ProbeStatement();

 private:
  void OnProbeFunctionEvalCompleted(
      base::OnceCallback<void(ProbeFunction::DataType)> callback,
      ProbeFunction::DataType results) const;

  std::string component_name_;
  std::unique_ptr<ProbeFunction> probe_function_;
  std::set<std::string> key_;
  std::unique_ptr<ProbeResultChecker> probe_result_checker_;
  std::unique_ptr<Matcher> matcher_;
  std::optional<base::Value> information_;
  // Must be the last member.
  base::WeakPtrFactory<ProbeStatement> weak_ptr_factory_{this};

  FRIEND_TEST(ProbeConfigTest, LoadConfig);
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_PROBE_STATEMENT_H_
